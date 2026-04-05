#!/usr/bin/env python3
"""Generate szafir-host-proxy/generated_permissions.h from szafir-host-proxy/permissions.yml.

Usage:
    generate_permissions_header.py          Generate the header (default paths)
    generate_permissions_header.py <input> <output>   Explicit paths

The output file is a C++ header with constexpr arrays and an inline template for:
  Permissions::kBrowsers                  — all supported browsers with metadata
  Permissions::kUniqueConfigDirs          — deduplicated (configDir, configLayout) pairs
                                            for filesystem access rule construction
  Permissions::kLauncherStaticRules       — static-path rules for the launcher child
  Permissions::forEachLauncherDynamicRule — runtime-computed launcher path rules
"""
import sys
from pathlib import Path
from typing import Any

import yaml

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "szafir-host-proxy" / "permissions.yml"
DEFAULT_OUTPUT = ROOT / "szafir-host-proxy" / "generated_permissions.h"

# ── YAML config_layout → C++ enum literal ────────────────────────────────────

_LAYOUT_MAP = {
    "home_relative": "ConfigLayout::HomeRelative",
    "xdg_config":    "ConfigLayout::XdgConfig",
}

_BASE_MAP = {
    "firefox":  "BrowserBase::Firefox",
    "chromium": "BrowserBase::Chromium",
}


def _cpp_string_view(s: str) -> str:
    """Wrap s in a std::string_view literal."""
    escaped = s.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"sv'


def _render_browser_entry(b: dict[str, Any]) -> str:
    flatpak_id   = _cpp_string_view(b["id"])
    base         = _BASE_MAP[b["base"]]
    config_dir   = _cpp_string_view(b["config_dir"])
    config_layout = _LAYOUT_MAP[b["config_layout"]]
    install_in_host = "true" if b["install_in_host"] else "false"
    display_name = _cpp_string_view(b["display_name"])
    icon         = _cpp_string_view(b["icon"])

    # Align columns for readability using fixed widths
    return (
        f"    {{{flatpak_id:<60s}, {base:<28s}, "
        f"{config_dir:<28s}, {config_layout:<30s}, "
        f"{install_in_host:<5s}, {display_name:<26s}, {icon}}}"
    )


def _render_config_dir_entry(config_dir: str, config_layout: str) -> str:
    cd     = _cpp_string_view(config_dir)
    layout = _LAYOUT_MAP[config_layout]
    return f"    {{{cd:<28s}, {layout}}}"


# Launcher rule generation

# Maps YAML landlock_access tokens → Landlock flag constant names.
_ACCESS_TO_FLAG: dict[str, str] = {
    "read_only":         "Landlock::kReadOnly",
    "read_only_file":    "Landlock::kReadOnlyFile",
    "read_exec":         "Landlock::kReadExec",
    "read_exec_file":    "Landlock::kReadExecFile",
    "read_write":        "Landlock::kReadWrite",
    "read_write_create": "Landlock::kReadWriteCreate",
    "read_dir_only":     "Landlock::kReadDirOnly",
    "read_exec_write":   "Landlock::kReadExecWrite",
    "overrides_dir":     "Landlock::kOverridesDirOps",
    "override_file":     "Landlock::kOverrideFileAccess",
}

# Maps YAML template tokens → C parameter names in forEachLauncherDynamicRule.
_TEMPLATE_PARAMS: dict[str, str] = {
    "{home}":          "home",
    "{xdg_data_home}": "xdgDataHome",
}

# ── System rule generation ───────────────────────────────────────────────────

# Permission groups whose paths form the base system rules (Phase 1 & Phase 2).
_SYSTEM_RULE_GROUPS: list[str] = [
    "system_sandbox",
    "flatpak_metadata",
    "app_xdg_data",
    "external_providers",
    "java_runtime",
]

# Template tokens that may appear inside system-rule paths (after ~ stripping).
_SYSTEM_TEMPLATE_PARAMS: dict[str, str] = {
    "{app_id}": "appId",
}

_CONDITION_TO_MACRO: dict[str, str] = {
    "bundled": "BUNDLED_HOST",
}


def _render_launcher_static_entry(rule: dict[str, Any]) -> str:
    path = rule["path"]
    flag = _ACCESS_TO_FLAG[rule["landlock_access"]]
    escaped = path.replace("\\", "\\\\").replace('"', '\\"')
    return '    {"' + escaped + '", ' + flag + '}'


def _render_buf_snprintf(buf_name: str, template: str) -> str:
    """Return a snprintf() call that expands template substitutions into buf_name."""
    fmt = template
    args: list[str] = []
    for tok, param in _TEMPLATE_PARAMS.items():
        if tok in fmt:
            fmt = fmt.replace(tok, "%s")
            args.append(param)
    return f'snprintf({buf_name}, sizeof({buf_name}), "{fmt}", {", ".join(args)})'


def _render_dynamic_rule(entry: dict[str, Any], buf_idx: int) -> str:
    """Return C++ code (indented for a function body) that applies one dynamic rule."""
    template = entry["template"]
    flag = _ACCESS_TO_FLAG[entry["landlock_access"]]
    fallback = entry.get("fallback", "")

    if template == "{xauthority}":
        # Conditional: use the passed xauthority if non-empty, else fall back to
        # $HOME/.Xauthority.  Scoped block limits buffer lifetime.
        fallback_snprintf = _render_buf_snprintf("_xauth_buf", fallback)
        return (
            "    {\n"
            "        char _xauth_buf[4096];\n"
            "        const char *_xauth;\n"
            "        if (xauthority && xauthority[0] != '\\0') {\n"
            "            _xauth = xauthority;\n"
            "        } else {\n"
            f"            {fallback_snprintf};\n"
            "            _xauth = _xauth_buf;\n"
            "        }\n"
            f"        fn(_xauth, {flag});\n"
            "    }"
        )

    if template in _TEMPLATE_PARAMS:
        # Bare parameter reference — pass the pointer directly, no snprintf needed.
        param = _TEMPLATE_PARAMS[template]
        return f"    fn({param}, {flag});"

    # General case: build the path in a numbered stack buffer via snprintf.
    buf = f"_buf{buf_idx}"
    snprintf_call = _render_buf_snprintf(buf, template)
    return (
        f"    char {buf}[4096];\n"
        f"    {snprintf_call};\n"
        f"    fn({buf}, {flag});"
    )


# ── System rule helpers ──────────────────────────────────────────────────────


def _collect_system_rules(data: dict[str, Any]) -> tuple[list[dict], list[dict]]:
    """Partition system-rule paths into static (absolute) and dynamic (home-relative)."""
    groups = data["permission_groups"]
    static_rules: list[dict[str, Any]] = []
    dynamic_rules: list[dict[str, Any]] = []

    for group_name in _SYSTEM_RULE_GROUPS:
        group = groups[group_name]
        condition = group.get("condition")

        for path_entry in group.get("paths", []):
            path = path_entry["path"]
            access_token = path_entry["landlock_access"]
            if access_token not in _ACCESS_TO_FLAG:
                raise ValueError(
                    f"Unknown landlock_access '{access_token}' in {group_name}.paths")
            rule = {
                "path": path,
                "flag": _ACCESS_TO_FLAG[access_token],
                "condition": condition,
                "group": group_name,
            }
            if path.startswith("/"):
                static_rules.append(rule)
            elif path.startswith("~"):
                dynamic_rules.append(rule)
            else:
                raise ValueError(f"Unsupported path format '{path}' in {group_name}")

    return static_rules, dynamic_rules


def _build_system_static_entries(rules: list[dict]) -> str:
    """Render constexpr array entries for static system rules."""
    lines: list[str] = []
    current_group = None
    for rule in rules:
        if rule["group"] != current_group:
            lines.append(f"    // {rule['group']}")
            current_group = rule["group"]
        escaped = rule["path"].replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'    {{"{escaped}", {rule["flag"]}}},')
    # Strip trailing comma from last data line
    for i in range(len(lines) - 1, -1, -1):
        if lines[i].endswith(","):
            lines[i] = lines[i][:-1]
            break
    return "\n".join(lines)


def _build_system_dynamic_body(rules: list[dict]) -> str:
    """Render the function body for forEachSystemDynamicRule."""
    parts: list[str] = []
    buf_idx = 0
    current_condition = None
    current_group = None

    for rule in rules:
        condition = rule.get("condition")
        # Open/close #ifdef blocks on condition transitions
        if condition != current_condition:
            if current_condition is not None:
                parts.append("#endif")
                parts.append("")
            if condition is not None:
                macro = _CONDITION_TO_MACRO[condition]
                parts.append(f"#ifdef {macro}")
            current_condition = condition

        if rule["group"] != current_group:
            parts.append(f"    // {rule['group']}")
            current_group = rule["group"]

        suffix = rule["path"][1:]  # strip leading ~
        flag = rule["flag"]

        if not suffix:
            # Bare ~ → pass home directly
            parts.append(f"    fn(home, {flag});")
        else:
            fmt = suffix
            args = ["home"]
            for tok, param in _SYSTEM_TEMPLATE_PARAMS.items():
                if tok in fmt:
                    fmt = fmt.replace(tok, "%s")
                    args.append(param)
            buf = f"_buf{buf_idx}"
            parts.append(f"    char {buf}[4096];")
            parts.append(f'    snprintf({buf}, sizeof({buf}), "%s{fmt}", {", ".join(args)});')
            parts.append(f"    fn({buf}, {flag});")
            buf_idx += 1
        parts.append("")  # blank line between rules

    if current_condition is not None:
        parts.append("#endif")

    # Remove trailing blank lines
    while parts and parts[-1] == "":
        parts.pop()
    return "\n".join(parts)


def generate(input_path: Path, output_path: Path) -> None:
    data = yaml.safe_load(input_path.read_text(encoding="utf-8"))

    browsers: list[dict[str, Any]] = data["browsers"]
    num_browsers = len(browsers)

    # Build deduplicated unique config dir list (preserving browser order)
    seen_config: set[tuple[str, str]] = set()
    unique_dirs: list[dict[str, str]] = []
    for b in browsers:
        key = (b["config_dir"], b["config_layout"])
        if key not in seen_config:
            seen_config.add(key)
            unique_dirs.append({"config_dir": b["config_dir"], "config_layout": b["config_layout"]})
    num_unique = len(unique_dirs)

    # Validate enum values
    for b in browsers:
        if b["base"] not in _BASE_MAP:
            raise ValueError(f"Unknown browser base '{b['base']}' for id '{b['id']}'")
        if b["config_layout"] not in _LAYOUT_MAP:
            raise ValueError(f"Unknown config_layout '{b['config_layout']}' for id '{b['id']}'")

    browser_entries = "\n".join(_render_browser_entry(b) + "," for b in browsers)
    # Strip trailing comma from last entry for cleaner C++
    browser_entries_lines = [_render_browser_entry(b) for b in browsers]
    browser_entries = ",\n".join(browser_entries_lines)

    config_dir_entries_lines = [_render_config_dir_entry(d["config_dir"], d["config_layout"]) for d in unique_dirs]
    config_dir_entries = ",\n".join(config_dir_entries_lines)

    # Launcher section
    launcher_group = data["permission_groups"]["launcher_sandbox"]
    static_rules: list[dict[str, Any]] = launcher_group["paths"]
    dynamic_rules: list[dict[str, Any]] = launcher_group["dynamic_paths"]
    num_static = len(static_rules)

    for r in static_rules:
        if r["landlock_access"] not in _ACCESS_TO_FLAG:
            raise ValueError(f"Unknown landlock_access '{r['landlock_access']}' in launcher_sandbox.paths")
    for e in dynamic_rules:
        if e["landlock_access"] not in _ACCESS_TO_FLAG:
            raise ValueError(f"Unknown landlock_access '{e['landlock_access']}' in launcher_sandbox.dynamic_paths")

    static_entries = ",\n".join(_render_launcher_static_entry(r) for r in static_rules)

    buf_idx = 0
    dynamic_parts: list[str] = []
    for entry in dynamic_rules:
        code = _render_dynamic_rule(entry, buf_idx)
        dynamic_parts.append(code)
        # Increment buffer counter for entries that allocate a _bufN stack variable.
        if entry["template"] != "{xauthority}" and entry["template"] not in _TEMPLATE_PARAMS:
            buf_idx += 1
    dynamic_body = "\n\n".join(dynamic_parts)

    # ── System rules ─────────────────────────────────────────────────────────
    system_static, system_dynamic = _collect_system_rules(data)
    num_system_static = len(system_static)
    system_static_entries = _build_system_static_entries(system_static)
    system_dynamic_body = _build_system_dynamic_body(system_dynamic)
    system_groups_str = ", ".join(_SYSTEM_RULE_GROUPS)

    # ── Overrides directory suffix ───────────────────────────────────────────
    overrides_path = data["permission_groups"]["flatpak_overrides"]["paths"][0]["path"]
    if not overrides_path.startswith("~"):
        raise ValueError(f"Expected flatpak_overrides path to start with '~', got '{overrides_path}'")
    overrides_suffix = overrides_path[1:]

    rel_input = input_path.relative_to(ROOT) if input_path.is_relative_to(ROOT) else input_path
    rel_output = output_path.relative_to(ROOT) if output_path.is_relative_to(ROOT) else output_path

    header = f"""\
// GENERATED FILE — DO NOT EDIT.
// Source:    {rel_input}
// Generator: scripts/generate_permissions_header.py
// Run 'make permissions' to regenerate after editing permissions.yml.

#pragma once

#include <array>
#include <cstdio>
#include <string_view>

#include "LandlockFlags.h"

namespace Permissions {{

using namespace std::literals::string_view_literals;

/// Configuration base directory layout for a browser's config root.
enum class ConfigLayout {{
    HomeRelative,  ///< config dir is directly under $HOME  (e.g. ~/.mozilla)
    XdgConfig,     ///< config dir is under $XDG_CONFIG_HOME (e.g. ~/.config/chromium)
}};

/// Browser family; determines the native messaging subfolder name.
enum class BrowserBase {{
    Firefox,   ///< uses native-messaging-hosts/
    Chromium,  ///< uses NativeMessagingHosts/
}};

/// Full metadata for a supported browser.
struct BrowserEntry {{
    std::string_view flatpakId;
    BrowserBase      base;
    std::string_view configDir;      ///< directory name relative to the config base
    ConfigLayout     configLayout;
    bool             installInHost;  ///< false → Flatpak sandbox side only; skip host install
    std::string_view displayName;
    std::string_view icon;
}};

/// All supported browsers, in display order.
/// Source: szafir-host-proxy/permissions.yml — browsers[]
inline constexpr std::array<BrowserEntry, {num_browsers}> kBrowsers = {{{{
{browser_entries}
}}}};

/// Unique browser config directory entries for filesystem access rule construction.
/// Derived from kBrowsers with deduplication on (configDir, configLayout).
/// Use this array when building path-based rules to avoid duplicate entries
/// (e.g. org.chromium.Chromium and io.github.ungoogled_software.ungoogled_chromium
/// share config_dir "chromium" and produce a single entry here).
struct ConfigDirEntry {{
    std::string_view configDir;
    ConfigLayout     configLayout;
}};

/// Deduplicated (configDir, configLayout) pairs, preserving the order of first
/// occurrence in kBrowsers.
inline constexpr std::array<ConfigDirEntry, {num_unique}> kUniqueConfigDirs = {{{{
{config_dir_entries}
}}}};

// Launcher rules
// Source: szafir-host-proxy/permissions.yml — permission_groups.launcher_sandbox

/// One static (absolute, compile-time-constant path) launcher Landlock rule.
struct LauncherStaticRule {{
    const char *path;
    __u64       access;
}};

/// Static-path Landlock rules for the SzafirHost child process.
inline constexpr std::array<LauncherStaticRule, {num_static}> kLauncherStaticRules = {{{{
{static_entries}
}}}};

/// Calls fn(path, access) for each runtime-computed launcher Landlock rule.
/// home, xdgDataHome, and xauthority must remain valid for the duration of the call.
/// Uses only stack buffers and snprintf (POSIX async-signal-safe) — safe to call
/// post-fork as long as fn() also uses only async-signal-safe operations.
template<typename Fn>
inline void forEachLauncherDynamicRule(
    const char *home, const char *xdgDataHome, const char *xauthority, Fn fn)
{{
{dynamic_body}
}}

// ── System rules ────────────────────────────────────────────────────────────
// Source: permissions.yml — {system_groups_str}

/// One static (absolute, compile-time-constant path) system Landlock rule.
struct SystemStaticRule {{
    const char *path;
    __u64       access;
}};

/// Static-path Landlock rules for the proxy process (Phase 1 & 2 base rules).
inline constexpr std::array<SystemStaticRule, {num_system_static}> kSystemStaticRules = {{{{
{system_static_entries}
}}}};

/// Calls fn(path, access) for each home-relative system Landlock rule.
/// home and appId must remain valid for the duration of the call.
template<typename Fn>
inline void forEachSystemDynamicRule(const char *home, const char *appId, Fn fn)
{{
{system_dynamic_body}
}}

/// Suffix appended to $HOME to form the Flatpak overrides directory path.
inline constexpr std::string_view kOverridesDirSuffix = "{overrides_suffix}"sv;

}} // namespace Permissions
"""

    output_path.write_text(header, encoding="utf-8")
    print(f"  generated  {rel_output}  "
          f"({num_browsers} browsers, {num_unique} config dirs, "
          f"{num_static} launcher static rules, {num_system_static} system static rules)")


def main() -> None:
    args = sys.argv[1:]
    if len(args) == 0:
        input_path, output_path = DEFAULT_INPUT, DEFAULT_OUTPUT
    elif len(args) == 2:
        input_path, output_path = Path(args[0]), Path(args[1])
    else:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    generate(input_path, output_path)


if __name__ == "__main__":
    main()
