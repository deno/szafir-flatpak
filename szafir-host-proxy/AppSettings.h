#pragma once

#include <QString>
#include <QStandardPaths>

#include <filesystem>
#include <string_view>

#include "PathUtils.h"

using namespace std::literals::string_view_literals;

// Flatpak app IDs.
inline constexpr std::string_view kSzafirAppId    = "pl.kir.szafir"sv;
inline constexpr std::string_view kSzafirHostAppId = "pl.kir.szafirhost"sv;

// QSettings key for the GDK_SCALE value in Flatpak override-style keyfiles.
// Used in both bundled and non-bundled modes to store HiDPI scaling preferences.
inline const QString kGdkScaleKey = QStringLiteral("Environment/GDK_SCALE");

// Returns the path to a Flatpak override keyfile for appId.
// Uses $HOME/.local/share (XDG default), which is bind-mounted at that path inside the sandbox.
inline std::filesystem::path hostOverridePath(const QString &appId)
{
    const std::filesystem::path hostDataHome =
        PathUtils::toFsPath(qEnvironmentVariable("HOME")) / ".local" / "share";

    return hostDataHome / "flatpak" / "overrides" / PathUtils::toFsPath(appId);
}

#ifdef BUNDLED_HOST

// Returns the path to a local override file for the bundled SzafirHost.
// Mirrors the Flatpak per-app override keyfile layout but lives inside
// the sandbox's XDG_CONFIG_HOME so it persists across runs.
inline std::filesystem::path bundledHostOverridePath()
{
    const QString configHomeEnv = qEnvironmentVariable("XDG_CONFIG_HOME");
    const std::filesystem::path configHome = configHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        : PathUtils::toFsPath(configHomeEnv);

    return configHome / "flatpak-overrides" / "pl.kir.szafirhost";
}

// Returns the path to the marker file that records the user's acceptance of the
// Szafir license agreement.  Stored inside the sandbox's XDG_CONFIG_HOME so it
// survives Flatpak updates but is not visible to the host system.
inline std::filesystem::path licenseAcceptedMarkerPath()
{
    const QString configHomeEnv = qEnvironmentVariable("XDG_CONFIG_HOME");
    const std::filesystem::path configHome = configHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        : PathUtils::toFsPath(configHomeEnv);

    return configHome / "szafir-host-proxy" / "license-accepted";
}

// Returns the fallback directory for components downloaded at runtime when
// the app is not built with extra-data (noextra variant).  Lives inside
// XDG_DATA_HOME so it persists across Flatpak updates.
inline std::filesystem::path downloadedExtraPath()
{
    const QString dataHomeEnv = qEnvironmentVariable("XDG_DATA_HOME");
    const std::filesystem::path dataHome = dataHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        : PathUtils::toFsPath(dataHomeEnv);

    return dataHome / "szafir-host-proxy" / "extra";
}

// Returns the dedicated directory for verified runtime components consumed by
// the bundled SzafirHost launcher. Lives inside XDG_DATA_HOME.
inline std::filesystem::path verifiedComponentsPath()
{
    const QString dataHomeEnv = qEnvironmentVariable("XDG_DATA_HOME");
    const std::filesystem::path dataHome = dataHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        : PathUtils::toFsPath(dataHomeEnv);

    return dataHome / "szafir-host-proxy" / "components";
}

// Returns a temporary, non-persistent directory for in-progress downloads
// pending checksum verification. Lives inside XDG_CACHE_HOME and is
// intentionally not exposed to the SzafirHost runtime sandbox.
inline std::filesystem::path componentDownloadPath()
{
    const QString cacheHomeEnv = qEnvironmentVariable("XDG_CACHE_HOME");
    const std::filesystem::path cacheHome = cacheHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation))
        : PathUtils::toFsPath(cacheHomeEnv);

    return cacheHome / "szafir-host-proxy" / "download";
}

// Returns the path to the JSON file where state of downloaded and verified
// components is kept. Lives inside XDG_DATA_HOME.
inline std::filesystem::path componentStatePath()
{
    const QString dataHomeEnv = qEnvironmentVariable("XDG_DATA_HOME");
    const std::filesystem::path dataHome = dataHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        : PathUtils::toFsPath(dataHomeEnv);

    return dataHome / "szafir-host-proxy" / "components-state.json";
}

// Returns the path to the external providers XML consumed by SzafirHost.
// Lives in $HOME so SzafirHost can read it across the sandbox boundary.
inline std::filesystem::path externalProvidersXmlPath()
{
    return PathUtils::toFsPath(qEnvironmentVariable("HOME")) / "external_providers.xml";
}

#endif // BUNDLED_HOST
