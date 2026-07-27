#!/usr/bin/env python3
"""Render Flatpak manifests and metainfo XML files from Jinja2 templates.

Usage:
    render_manifest.py all                  Render all variants
    render_manifest.py clean                Remove all generated output files
    render_manifest.py list                 List all variants and their outputs
    render_manifest.py outputs              Print output file paths (one per line)
    render_manifest.py <variant> [...]      Render one or more specific variants
"""
import sys
import json
import textwrap
from pathlib import Path
from typing import Any

import yaml
from jinja2 import Environment, FileSystemLoader, StrictUndefined

ROOT = Path(__file__).resolve().parents[1]


def _load_permissions() -> dict[str, Any]:
    return yaml.safe_load(
        (ROOT / "szafir-host-proxy" / "permissions.yml").read_text(encoding="utf-8")
    )



import re


def _strip_flatpak_exclude(text: str) -> str:
    """Remove [fe]...[/fe] blocks from text."""
    return re.sub(r"\[fe\].*?\[/fe\]", "", text, flags=re.DOTALL)

def _comment_lines(description: str, width: int = 90) -> list[str]:
    """Render a block description into wrapped single-line YAML comments, skipping flatpak-exclude blocks."""
    filtered = _strip_flatpak_exclude(description)
    normalized = " ".join(filtered.split())
    if not normalized:
        return []
    return textwrap.wrap(normalized, width=width)


def _build_finish_arg_groups(permissions: dict[str, Any]) -> list[dict[str, Any]]:
    """Build ordered finish-args grouped by permission group with comment text."""
    groups = permissions["permission_groups"]
    browsers: list[dict[str, Any]] = permissions["browsers"]

    def flatpak_args(group_name: str) -> list[str]:
        g = groups[group_name]
        return [e["arg"] for e in g.get("flatpak_args", [])]

    def filesystem_args(group_name: str) -> list[str]:
        g = groups[group_name]
        result = []
        for p in g.get("paths", []):
            if "flatpak_note" in p:
                continue  # Landlock-only path; no Flatpak --filesystem entry
            if perm := p.get("flatpak_permission"):
                result.append(f"--filesystem={p['path']}:{perm}")
            if persist := p.get("flatpak_persist"):
                result.append(f"--persist={persist}")
        return result

    finish_groups: list[dict[str, Any]] = []

    def add_group(group_name: str, args: list[str]) -> None:
        if not args:
            return
        description = groups[group_name].get("description", "")
        finish_groups.append(
            {
                "group_name": group_name,
                "description_lines": _comment_lines(description),
                "args": args,
            }
        )

    # Display / IPC sockets
    add_group("display_ipc", flatpak_args("display_ipc"))

    # D-Bus name ownership and talk permissions
    add_group("dbus_names", flatpak_args("dbus_names"))

    # Browser config dirs — unique (config_dir, config_layout) pairs, in browser order
    seen_dirs: set[tuple[str, str]] = set()
    browser_config_args: list[str] = []
    for browser in browsers:
        key = (browser["config_dir"], browser["config_layout"])
        if key not in seen_dirs:
            seen_dirs.add(key)
            layout = browser["config_layout"]
            config_dir = browser["config_dir"]
            if layout == "home_relative":
                fp = f"~/{config_dir}"
            elif layout == "xdg_config":
                fp = f"xdg-config/{config_dir}"
            else:
                raise ValueError(f"Unknown config_layout: {layout!r} for browser {browser['id']!r}")
            browser_config_args.append(f"--filesystem={fp}:create")
    add_group("browser_config", browser_config_args)

    # Flatpak overrides directory
    add_group("flatpak_overrides", filesystem_args("flatpak_overrides"))

    # Flatpak icon and app metadata (read-only)
    add_group("flatpak_metadata", filesystem_args("flatpak_metadata"))

    # Browser Flatpak sandbox dirs (~/.var/app/<id>) — all browsers
    browser_var_app_args = [f"--filesystem=~/.var/app/{browser['id']}:create" for browser in browsers]
    add_group("browser_var_app", browser_var_app_args)

    # Bundled-only extras: x11 socket, pcsc, network (post-filesystem, bundled only)
    add_group("bundled_extras", flatpak_args("bundled_extras"))

    # Java runtime: --persist + --env (post-filesystem, bundled only)
    java_args = filesystem_args("java_runtime") + flatpak_args("java_runtime")
    add_group("java_runtime", java_args)

    return finish_groups


def _build_finish_args(permissions: dict[str, Any]) -> list[str]:
    """Build the ordered finish-args list for the proxy manifest."""
    return [arg for group in _build_finish_arg_groups(permissions) for arg in group["args"]]


PERMISSIONS: dict[str, Any] = _load_permissions()


def _load_releases() -> list[dict[str, Any]]:
    data = yaml.safe_load(
        (ROOT / "szafir-host-proxy" / "releases.yml").read_text(encoding="utf-8")
    )
    releases = data["releases"]
    if not isinstance(releases, list) or not releases:
        raise RuntimeError("missing releases in szafir-host-proxy/releases.yml")
    first_release = releases[0]
    if "version" not in first_release:
        raise RuntimeError("missing version in first release entry in releases.yml")
    return releases


RELEASES: list[dict[str, Any]] = _load_releases()
APP_VERSION: str = RELEASES[0]["version"]


def _load_system_components() -> list[dict[str, Any]]:
    data = json.loads(
        (ROOT / "szafir-host-proxy" / "system_components.json").read_text(encoding="utf-8")
    )
    return data["system_components"]


def _get_system_component(component_id: str) -> dict[str, Any]:
    for comp in SYSTEM_COMPONENTS:
        if comp.get("id") == component_id:
            return comp
    raise RuntimeError(f"component '{component_id}' not found in system_components.json")


SYSTEM_COMPONENTS: list[dict[str, Any]] = _load_system_components()


VARIANTS: dict[str, dict[str, Any]] = {
    # Flatpak manifests
    "proxy.manifest": {
        "output": "pl.deno.kir.szafirhostproxy.yml",
        "template_root": "manifests",
        "template": "proxy.yml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "metainfo_file": "pl.deno.kir.szafirhostproxy.metainfo.xml",
            "app_version": APP_VERSION,
            "szafir_dev": False,
            "finish_args": _build_finish_args(PERMISSIONS),
            "finish_arg_groups": _build_finish_arg_groups(PERMISSIONS),
            "system_components": [_get_system_component("pcsc-lite")],
        },
    },
    "proxy.manifest.dev": {
        "output": "pl.deno.kir.szafirhostproxy.dev.yml",
        "template_root": "manifests",
        "template": "proxy.yml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "metainfo_file": "pl.deno.kir.szafirhostproxy.metainfo.xml",
            "app_version": APP_VERSION,
            "szafir_dev": True,
            "finish_args": _build_finish_args(PERMISSIONS),
            "finish_arg_groups": _build_finish_arg_groups(PERMISSIONS),
            "system_components": [_get_system_component("pcsc-lite")],
        },
    },
    "szafir.manifest": {
        "output": "pl.kir.szafir.yml",
        "template_root": "manifests",
        "template": "szafir.yml.j2",
        "context": {
            "system_components": [_get_system_component("pcsc-lite")],
        },
    },
    # Metainfo XML files
    "proxy.meta": {
        "output": "pl.deno.kir.szafirhostproxy.metainfo.xml",
        "template_root": "metainfo",
        "template": "proxy.metainfo.xml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "releases": RELEASES,
            "summary_en": "Browser bridge for Szafir website signing",
            "summary_pl": "Most przeglądarkowy dla podpisu Szafir na stronach WWW",
        },
    },
    "proxy.meta.local": {
        "output": "szafir-host-proxy/pl.deno.kir.szafirhostproxy.metainfo.xml",
        "template_root": "metainfo",
        "template": "proxy.metainfo.xml.j2",
        "context": {
            "app_id": "pl.deno.kir.szafirhostproxy",
            "releases": RELEASES,
            "summary_en": "Browser bridge for Szafir website signing",
            "summary_pl": "Most przeglądarkowy dla podpisu Szafir na stronach WWW",
        },
    },
}

_COMMANDS = {"all", "clean", "list", "outputs"}


def _make_env(template_root: str) -> Environment:
    return Environment(
        loader=FileSystemLoader(str(ROOT / "templates" / template_root)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
    )


def render_variant(name: str) -> None:
    spec = VARIANTS[name]
    env = _make_env(spec["template_root"])
    context = dict(spec["context"])
    rendered = env.get_template(spec["template"]).render(**context)
    output_path = ROOT / spec["output"]
    output_path.write_text(rendered, encoding="utf-8")
    print(f"  rendered  {spec['output']}")


def cmd_all() -> None:
    for name in VARIANTS:
        render_variant(name)


def cmd_clean() -> None:
    for spec in VARIANTS.values():
        p = ROOT / spec["output"]
        if p.exists():
            p.unlink()
            print(f"  removed   {spec['output']}")


def cmd_list() -> None:
    max_name = max(len(n) for n in VARIANTS)
    for name, spec in VARIANTS.items():
        print(f"  {name:<{max_name}}  →  {spec['output']}")


def cmd_outputs() -> None:
    for spec in VARIANTS.values():
        print(spec["output"])


def main() -> None:
    targets = sys.argv[1:]
    if not targets:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    if targets == ["all"]:
        cmd_all()
    elif targets == ["clean"]:
        cmd_clean()
    elif targets == ["list"]:
        cmd_list()
    elif targets == ["outputs"]:
        cmd_outputs()
    else:
        unknown = [t for t in targets if t not in VARIANTS]
        if unknown:
            print(f"error: unknown target(s): {', '.join(unknown)}", file=sys.stderr)
            print("       run 'list' to see available variants", file=sys.stderr)
            sys.exit(1)
        for name in targets:
            render_variant(name)


if __name__ == "__main__":
    main()
