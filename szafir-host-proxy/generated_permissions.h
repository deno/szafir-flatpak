// GENERATED FILE — DO NOT EDIT.
// Source:    szafir-host-proxy/permissions.yml
// Generator: scripts/generate_permissions_header.py
// Run 'make permissions' to regenerate after editing permissions.yml.

#pragma once

#include <array>
#include <cstdio>
#include <string_view>

#include "LandlockFlags.h"

namespace Permissions {

using namespace std::literals::string_view_literals;

/// Configuration base directory layout for a browser's config root.
enum class ConfigLayout {
    HomeRelative,  ///< config dir is directly under $HOME  (e.g. ~/.mozilla)
    XdgConfig,     ///< config dir is under $XDG_CONFIG_HOME (e.g. ~/.config/chromium)
};

/// Browser family; determines the native messaging subfolder name.
enum class BrowserBase {
    Firefox,   ///< uses native-messaging-hosts/
    Chromium,  ///< uses NativeMessagingHosts/
};

/// Full metadata for a supported browser.
struct BrowserEntry {
    std::string_view flatpakId;
    BrowserBase      base;
    std::string_view configDir;      ///< directory name relative to the config base
    ConfigLayout     configLayout;
    bool             installInHost;  ///< false → Flatpak sandbox side only; skip host install
    std::string_view displayName;
    std::string_view icon;
};

/// All supported browsers, in display order.
/// Source: szafir-host-proxy/permissions.yml — browsers[]
inline constexpr std::array<BrowserEntry, 7> kBrowsers = {{
    {"org.mozilla.firefox"sv                                     , BrowserBase::Firefox        , ".mozilla"sv                , ConfigLayout::HomeRelative    , true , "Mozilla Firefox"sv       , "firefox"sv},
    {"io.gitlab.librewolf-community"sv                           , BrowserBase::Firefox        , ".librewolf"sv              , ConfigLayout::HomeRelative    , true , "LibreWolf"sv             , "firefox"sv},
    {"net.waterfox.waterfox"sv                                   , BrowserBase::Firefox        , ".waterfox"sv               , ConfigLayout::HomeRelative    , true , "Waterfox"sv              , "firefox"sv},
    {"com.google.Chrome"sv                                       , BrowserBase::Chromium       , "google-chrome"sv           , ConfigLayout::XdgConfig       , true , "Google Chrome"sv         , "google-chrome"sv},
    {"com.google.ChromeDev"sv                                    , BrowserBase::Chromium       , "google-chrome-unstable"sv  , ConfigLayout::XdgConfig       , true , "Google Chrome Dev"sv     , "google-chrome"sv},
    {"org.chromium.Chromium"sv                                   , BrowserBase::Chromium       , "chromium"sv                , ConfigLayout::XdgConfig       , true , "Chromium"sv              , "chromium-browser"sv},
    {"io.github.ungoogled_software.ungoogled_chromium"sv         , BrowserBase::Chromium       , "chromium"sv                , ConfigLayout::XdgConfig       , false, "Ungoogled Chromium"sv    , "chromium-browser"sv}
}};

/// Unique browser config directory entries for filesystem access rule construction.
/// Derived from kBrowsers with deduplication on (configDir, configLayout).
/// Use this array when building path-based rules to avoid duplicate entries
/// (e.g. org.chromium.Chromium and io.github.ungoogled_software.ungoogled_chromium
/// share config_dir "chromium" and produce a single entry here).
struct ConfigDirEntry {
    std::string_view configDir;
    ConfigLayout     configLayout;
};

/// Deduplicated (configDir, configLayout) pairs, preserving the order of first
/// occurrence in kBrowsers.
inline constexpr std::array<ConfigDirEntry, 6> kUniqueConfigDirs = {{
    {".mozilla"sv                , ConfigLayout::HomeRelative},
    {".librewolf"sv              , ConfigLayout::HomeRelative},
    {".waterfox"sv               , ConfigLayout::HomeRelative},
    {"google-chrome"sv           , ConfigLayout::XdgConfig},
    {"google-chrome-unstable"sv  , ConfigLayout::XdgConfig},
    {"chromium"sv                , ConfigLayout::XdgConfig}
}};

// Launcher rules
// Source: szafir-host-proxy/permissions.yml — permission_groups.launcher_sandbox

/// One static (absolute, compile-time-constant path) launcher Landlock rule.
struct LauncherStaticRule {
    const char *path;
    __u64       access;
};

/// Static-path Landlock rules for the SzafirHost child process.
inline constexpr std::array<LauncherStaticRule, 9> kLauncherStaticRules = {{
    {"/app/jre", Landlock::kReadExec},
    {"/app/bin/start-szafir-host-native.sh", Landlock::kReadExecFile},
    {"/usr", Landlock::kReadExec},
    {"/etc", Landlock::kReadOnly},
    {"/run/pcscd", Landlock::kReadWriteCreate},
    {"/tmp", Landlock::kReadWriteCreate},
    {"/dev", Landlock::kReadWriteCreate},
    {"/proc", Landlock::kReadWrite},
    {"/sys", Landlock::kReadOnly}
}};

/// Calls fn(path, access) for each runtime-computed launcher Landlock rule.
/// home, xdgDataHome, and xauthority must remain valid for the duration of the call.
/// Uses only stack buffers and snprintf (POSIX async-signal-safe) — safe to call
/// post-fork as long as fn() also uses only async-signal-safe operations.
template<typename Fn>
inline void forEachLauncherDynamicRule(
    const char *home, const char *xdgDataHome, const char *xauthority, Fn fn)
{
    char _buf0[4096];
    snprintf(_buf0, sizeof(_buf0), "%s/external_providers.xml", home);
    fn(_buf0, Landlock::kReadOnlyFile);

    {
        char _xauth_buf[4096];
        const char *_xauth;
        if (xauthority && xauthority[0] != '\0') {
            _xauth = xauthority;
        } else {
            snprintf(_xauth_buf, sizeof(_xauth_buf), "%s/.Xauthority", home);
            _xauth = _xauth_buf;
        }
        fn(_xauth, Landlock::kReadOnlyFile);
    }

    char _buf1[4096];
    snprintf(_buf1, sizeof(_buf1), "%s/szafir-host-proxy/components", xdgDataHome);
    fn(_buf1, Landlock::kReadOnly);

    char _buf2[4096];
    snprintf(_buf2, sizeof(_buf2), "%s/szafir_host", xdgDataHome);
    fn(_buf2, Landlock::kReadWriteCreate);

    fn(xdgDataHome, Landlock::kReadDirOnly);

    char _buf3[4096];
    snprintf(_buf3, sizeof(_buf3), "%s/.java", home);
    fn(_buf3, Landlock::kReadWriteCreate);
}

// ── System rules ────────────────────────────────────────────────────────────
// Source: permissions.yml — system_sandbox, flatpak_metadata, app_xdg_data, external_providers, java_runtime

/// One static (absolute, compile-time-constant path) system Landlock rule.
struct SystemStaticRule {
    const char *path;
    __u64       access;
};

/// Static-path Landlock rules for the proxy process (Phase 1 & 2 base rules).
inline constexpr std::array<SystemStaticRule, 10> kSystemStaticRules = {{
    // system_sandbox
    {"/app", Landlock::kReadExecWrite},
    {"/usr", Landlock::kReadExec},
    {"/etc", Landlock::kReadOnly},
    {"/run", Landlock::kReadWriteCreate},
    {"/tmp", Landlock::kReadWriteCreate},
    {"/proc", Landlock::kReadWrite},
    {"/dev", Landlock::kReadWriteCreate},
    {"/sys", Landlock::kReadOnly},
    // flatpak_metadata
    {"/var/lib/flatpak/exports/share/icons", Landlock::kReadOnly},
    {"/var/lib/flatpak/app", Landlock::kReadOnly}
}};

/// Calls fn(path, access) for each home-relative system Landlock rule.
/// home and appId must remain valid for the duration of the call.
template<typename Fn>
inline void forEachSystemDynamicRule(const char *home, const char *appId, Fn fn)
{
    // flatpak_metadata
    char _buf0[4096];
    snprintf(_buf0, sizeof(_buf0), "%s/.local/share/flatpak/exports/share/icons", home);
    fn(_buf0, Landlock::kReadOnly);

    char _buf1[4096];
    snprintf(_buf1, sizeof(_buf1), "%s/.local/share/flatpak/app", home);
    fn(_buf1, Landlock::kReadOnly);

    // app_xdg_data
    char _buf2[4096];
    snprintf(_buf2, sizeof(_buf2), "%s/.var/app/%s", home, appId);
    fn(_buf2, Landlock::kReadWriteCreate);

    // external_providers
    fn(home, Landlock::kOverridesDirOps);

    char _buf3[4096];
    snprintf(_buf3, sizeof(_buf3), "%s/external_providers.xml", home);
    fn(_buf3, Landlock::kOverrideFileAccess);

#ifdef BUNDLED_HOST
    // java_runtime
    char _buf4[4096];
    snprintf(_buf4, sizeof(_buf4), "%s/.java", home);
    fn(_buf4, Landlock::kReadWriteCreate);

#endif
}

/// Suffix appended to $HOME to form the Flatpak overrides directory path.
inline constexpr std::string_view kOverridesDirSuffix = "/.local/share/flatpak/overrides"sv;

} // namespace Permissions
