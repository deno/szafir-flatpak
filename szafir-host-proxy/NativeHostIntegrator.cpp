#include "NativeHostIntegrator.h"
#include "AppSettings.h"
#include "config.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <QTextStream>
#include <QDebug>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <KConfig>
#include <KConfigGroup>

namespace {

namespace fs = std::filesystem;
using namespace std::literals::string_view_literals;

// Manifest name the browser extension expects. Careful when renaming!
constexpr std::string_view kManifestName = "pl.com.kir.szafirhost"sv;

constexpr std::string_view kDbusService = DBUS_APP_ID;
constexpr std::string_view kInstalledWrapperPath = "/app/share/szafir-host-proxy/flatpak-host-wrapper"sv;
constexpr std::string_view kInstalledWrapperDDir = "/app/share/szafir-host-proxy/flatpak-host-wrapper.d"sv;

enum class BrowserBase {
    Firefox,
    Chromium,
};

enum class ConfigRootLayout {
    HomeRelative, // config dir sits directly under ~ (host) or ~/.var/app/<id> (Flatpak)
    XdgConfig,    // config dir sits under ~/.config (host) or ~/.var/app/<id>/config (Flatpak)
};

QString browserTypeFor(BrowserBase base)
{
    switch (base) {
    case BrowserBase::Firefox:  return QStringLiteral("firefox");
    case BrowserBase::Chromium: return QStringLiteral("chrome");
    }
    Q_UNREACHABLE();
}

fs::path nativeMessagingFolderFor(BrowserBase base)
{
    switch (base) {
    case BrowserBase::Firefox:  return fs::path("native-messaging-hosts");
    case BrowserBase::Chromium: return fs::path("NativeMessagingHosts");
    }
    Q_UNREACHABLE();
}

struct BrowserInfo {
    BrowserBase base;
    ConfigRootLayout configLayout;
    QString flatpakId;
    // Browser-specific config directory (e.g. ".mozilla", "chromium") under the base's config prefix.
    fs::path configDir;
    // When false, only Flatpak-side native messaging is installed.
    bool installInHost;
    QString displayName;
    QString icon;
};

QList<BrowserInfo> browsers()
{
    const QList<BrowserInfo> list = {
        {BrowserBase::Firefox,  ConfigRootLayout::HomeRelative, QStringLiteral("org.mozilla.firefox"),
            fs::path(".mozilla"),               true,
            QStringLiteral("Mozilla Firefox"),        QStringLiteral("firefox")},
        {BrowserBase::Firefox,  ConfigRootLayout::HomeRelative, QStringLiteral("io.gitlab.librewolf-community"),
            fs::path(".librewolf"),              true,
            QStringLiteral("LibreWolf"),               QStringLiteral("firefox")},
        {BrowserBase::Firefox,  ConfigRootLayout::HomeRelative, QStringLiteral("net.waterfox.waterfox"),
            fs::path(".waterfox"),               true,
            QStringLiteral("Waterfox"),                QStringLiteral("firefox")},
        {BrowserBase::Chromium, ConfigRootLayout::XdgConfig,    QStringLiteral("com.google.Chrome"),
            fs::path("google-chrome"),           true,
            QStringLiteral("Google Chrome"),           QStringLiteral("google-chrome")},
        {BrowserBase::Chromium, ConfigRootLayout::XdgConfig,    QStringLiteral("com.google.ChromeDev"),
            fs::path("google-chrome-unstable"),  true,
            QStringLiteral("Google Chrome Dev"),       QStringLiteral("google-chrome")},
        {BrowserBase::Chromium, ConfigRootLayout::XdgConfig,    QStringLiteral("org.chromium.Chromium"),
            fs::path("chromium"),                true,
            QStringLiteral("Chromium"),                QStringLiteral("chromium-browser")},
        {BrowserBase::Chromium, ConfigRootLayout::XdgConfig,    QStringLiteral("io.github.ungoogled_software.ungoogled_chromium"),
            fs::path("chromium"),                false,
            QStringLiteral("Ungoogled Chromium"),      QStringLiteral("chromium-browser")},
    };

    QSet<QString> seenHostConfigDirs;
    for (const BrowserInfo &browser : list) {
        if (!browser.installInHost)
            continue;

        const QString dir = PathUtils::toQString(browser.configDir);
        Q_ASSERT_X(!seenHostConfigDirs.contains(dir),
                   "browsers",
                   "Duplicate host config dir across browser entries");
        seenHostConfigDirs.insert(dir);
    }

    return list;
}

bool ensureParentDir(const fs::path &filePath, bool dryRun)
{
    const fs::path dirPath = filePath.parent_path();
    if (dirPath.empty() || fs::exists(dirPath))
        return true;

    qInfo() << "file-op:" << "mkdir -p" << PathUtils::toQString(dirPath);
    if (dryRun)
        return true;

    std::error_code ec;
    fs::create_directories(dirPath, ec);
    return !ec;
}

bool setBrowserTalkPermission(const QString &browserId, bool allowTalk, bool dryRun)
{
    const fs::path path = hostOverridePath(browserId);
    if (!ensureParentDir(path, dryRun)) {
        qWarning() << "Failed to create override parent for" << PathUtils::toQString(path);
        return false;
    }

    qInfo() << "perm-op:" << (allowTalk ? "grant" : "revoke") << QString::fromLatin1(kDbusService.data(), static_cast<qsizetype>(kDbusService.size()))
            << "for" << browserId << "via" << PathUtils::toQString(path);
    if (dryRun)
        return true;

    KConfig config(PathUtils::toQString(path), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Session Bus Policy"));

    const QString serviceId = QString::fromLatin1(kDbusService.data(), static_cast<qsizetype>(kDbusService.size()));
    if (allowTalk) {
        group.writeEntry(serviceId, QStringLiteral("talk"));
    } else {
        group.deleteEntry(serviceId);
    }
    
    if (!config.sync()) {
        qWarning() << "Failed to write Flatpak override for" << browserId;
        return false;
    }

    return true;
}

bool writeExecutableFile(const fs::path &path, const QString &content, bool dryRun)
{
    if (!ensureParentDir(path, dryRun)) {
        qWarning() << "Failed to create parent directory for" << PathUtils::toQString(path);
        return false;
    }

    qInfo() << "file-op:" << "write" << PathUtils::toQString(path);
    qInfo() << "perm-op:" << "chmod 755" << PathUtils::toQString(path);
    if (dryRun)
        return true;

    QSaveFile f(PathUtils::toQString(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "Failed to open for writing:" << PathUtils::toQString(path);
        return false;
    }

    QTextStream out(&f);
    out << content;

    if (!f.commit()) {
        qWarning() << "Failed to commit file:" << PathUtils::toQString(path);
        return false;
    }

    QFile::setPermissions(path,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                          QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                          QFileDevice::ReadOther | QFileDevice::ExeOther);
    return true;
}

bool installDropInTemplates(const fs::path &wrapperPath, bool dryRun)
{
    fs::path dDir = wrapperPath;
    dDir += ".d";
    // Only install drop-in templates if the .d directory doesn't already exist;
    // this preserves user customizations across reinstalls.
    if (fs::exists(dDir))
        return true;

    fs::path templateDir{std::string(kInstalledWrapperDDir)};
    if (!fs::exists(templateDir))
        return true;

    bool allOk = true;
    std::error_code dirEc;
    for (const auto &entry : fs::directory_iterator(templateDir, dirEc)) {
        if (!entry.is_regular_file(dirEc)) continue;
        
        QFile src(entry.path());
        if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to read drop-in template:" << PathUtils::toQString(entry.path());
            allOk = false;
            continue;
        }
        const QString content = QString::fromUtf8(src.readAll());
        const fs::path dstPath = dDir / entry.path().filename();

        if (!ensureParentDir(dstPath, dryRun)) {
            qWarning() << "Failed to create parent directory for" << PathUtils::toQString(dstPath);
            allOk = false;
            continue;
        }

        qInfo() << "file-op:" << "write" << PathUtils::toQString(dstPath);
        if (dryRun)
            continue;

        QSaveFile f(PathUtils::toQString(dstPath));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qWarning() << "Failed to open for writing:" << PathUtils::toQString(dstPath);
            allOk = false;
            continue;
        }
        QTextStream out(&f);
        out << content;
        if (!f.commit()) {
            qWarning() << "Failed to commit:" << PathUtils::toQString(dstPath);
            allOk = false;
            continue;
        }
        QFile::setPermissions(dstPath,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }
    return allOk;
}

bool writeJsonFile(const fs::path &path, const QJsonObject &obj, bool dryRun)
{
    if (!ensureParentDir(path, dryRun)) {
        qWarning() << "Failed to create parent directory for" << PathUtils::toQString(path);
        return false;
    }

    qInfo() << "file-op:" << "write" << PathUtils::toQString(path);
    if (dryRun)
        return true;

    QSaveFile f(PathUtils::toQString(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open for writing:" << PathUtils::toQString(path);
        return false;
    }

    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        qWarning() << "Failed to commit JSON file:" << PathUtils::toQString(path);
        return false;
    }
    return true;
}

int installedWrapperVersion(const fs::path &wrapperPath)
{
    QFile f(wrapperPath);
    return f.open(QIODevice::ReadOnly | QIODevice::Text)
           ? QString::fromUtf8(f.readAll()).split("TEMPLATE_VERSION=")[1]
                .split(QRegularExpression(R"([\"'\n])"))[0].toInt()
           : 0;
}

QJsonObject manifestFor(const BrowserInfo &browser, const fs::path &wrapperPath)
{
    QJsonObject obj {
        {QStringLiteral("name"), QString::fromLatin1(kManifestName.data(), static_cast<qsizetype>(kManifestName.size()))},
        {QStringLiteral("description"), QStringLiteral("Szafir Native Messaging Host")},
        {QStringLiteral("path"), PathUtils::toQString(wrapperPath)},
        {QStringLiteral("type"), QStringLiteral("stdio")},
    };

    if (browser.base == BrowserBase::Firefox) {
        obj.insert(QStringLiteral("allowed_extensions"), QJsonArray {
            QStringLiteral("{5e118bad-a840-4256-bd31-296194533aac}")
        });
    } else {
        obj.insert(QStringLiteral("allowed_origins"), QJsonArray {
            QStringLiteral("chrome-extension://gjalhnomhafafofonpdihihjnbafkipc/"),
            QStringLiteral("chrome-extension://bikmiknjdohdfmehchjpbiemekemgndp/")
        });
    }

    return obj;
}

fs::path hostConfigRoot(ConfigRootLayout layout)
{
    switch (layout) {
    case ConfigRootLayout::HomeRelative: return fs::path();
    case ConfigRootLayout::XdgConfig:   return fs::path(".config");
    }
    Q_UNREACHABLE();
}

fs::path flatpakConfigRoot(ConfigRootLayout layout)
{
    switch (layout) {
    case ConfigRootLayout::HomeRelative: return fs::path();
    case ConfigRootLayout::XdgConfig:   return fs::path("config");
    }
    Q_UNREACHABLE();
}

fs::path userHomePath()
{
    return PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
}

fs::path flatpakWrapperPath(const QString &browserId)
{
    return userHomePath() / ".var" / "app" / PathUtils::toFsPath(browserId)
        / "szafir-host-proxy" / "flatpak-host-wrapper";
}

std::optional<fs::path> hostWrapperPath(const BrowserInfo &browser)
{
    if (!browser.installInHost)
        return std::nullopt;

    return userHomePath()
        / hostConfigRoot(browser.configLayout)
        / browser.configDir
        / "szafir-host-proxy"
        / "flatpak-host-wrapper";
}

std::optional<fs::path> hostManifestPath(const BrowserInfo &browser)
{
    if (!browser.installInHost)
        return std::nullopt;

    return userHomePath()
        / hostConfigRoot(browser.configLayout)
        / browser.configDir
        / nativeMessagingFolderFor(browser.base)
        / fs::path("pl.com.kir.szafirhost.json");
}

fs::path flatpakManifestPath(const BrowserInfo &browser)
{
    return userHomePath()
        / ".var" / "app" / PathUtils::toFsPath(browser.flatpakId)
        / flatpakConfigRoot(browser.configLayout)
        / browser.configDir
        / nativeMessagingFolderFor(browser.base)
        / fs::path("pl.com.kir.szafirhost.json");
}

} // namespace

NativeHostIntegrator::NativeHostIntegrator(bool dryRun)
    : m_dryRun(dryRun)
{
}

bool NativeHostIntegrator::installIfNeeded()
{
    if (!ensureWrapperTemplateLoaded())
        return false;

    return installAll(/*force=*/false);
}

bool NativeHostIntegrator::installNow()
{
    if (!ensureWrapperTemplateLoaded())
        return false;

    return installAll(/*force=*/true);
}

bool NativeHostIntegrator::uninstall()
{
    return removeAll();
}

bool NativeHostIntegrator::installAll(bool force)
{
    bool allOk = true;
    const auto browserList = browsers();

    for (const BrowserInfo &browser : browserList) {
        const std::optional<fs::path> nativeWrapperPath = hostWrapperPath(browser);
        const fs::path sandboxWrapperPath = flatpakWrapperPath(browser.flatpakId);

        // Create wrapper content with browser-specific values
        QString wrapperContent = m_wrapperTemplate;
        wrapperContent.replace(QStringLiteral("{{BROWSER_TYPE}}"), browserTypeFor(browser.base));
        wrapperContent.replace(QStringLiteral("{{CLIENT_NAME}}"), browser.displayName);
        wrapperContent.replace(QStringLiteral("{{ICON}}"), browser.icon);

        if (nativeWrapperPath.has_value()) {
            if (force || installedWrapperVersion(nativeWrapperPath.value()) < WRAPPER_TEMPLATE_VERSION) {
                allOk = writeExecutableFile(nativeWrapperPath.value(), wrapperContent, m_dryRun) && allOk;
                allOk = installDropInTemplates(nativeWrapperPath.value(), m_dryRun) && allOk;
            } else {
                qInfo() << "Native wrapper up to date for" << browser.displayName << ", skipping";
            }
        }

        if (force || installedWrapperVersion(sandboxWrapperPath) < WRAPPER_TEMPLATE_VERSION) {
            allOk = writeExecutableFile(sandboxWrapperPath, wrapperContent, m_dryRun) && allOk;
            allOk = installDropInTemplates(sandboxWrapperPath, m_dryRun) && allOk;
        } else {
            qInfo() << "Flatpak wrapper up to date for" << browser.displayName << ", skipping";
        }

        if (const auto hostManifest = hostManifestPath(browser);
            hostManifest.has_value() && nativeWrapperPath.has_value()) {
            allOk = writeJsonFile(hostManifest.value(), manifestFor(browser, nativeWrapperPath.value()), m_dryRun) && allOk;
        }
        allOk = writeJsonFile(flatpakManifestPath(browser), manifestFor(browser, sandboxWrapperPath), m_dryRun) && allOk;

        allOk = setBrowserTalkPermission(browser.flatpakId, true, m_dryRun) && allOk;
    }

    return allOk;
}

bool NativeHostIntegrator::removeAll()
{
    bool allOk = true;
    const auto browserList = browsers();

    std::error_code ec;
    for (const BrowserInfo &browser : browserList) {
        if (const auto nativeWrapperPath = hostWrapperPath(browser); nativeWrapperPath.has_value()) {
            qInfo() << "file-op:" << "remove" << PathUtils::toQString(nativeWrapperPath.value());
            if (!m_dryRun)
                fs::remove(nativeWrapperPath.value(), ec);
        }

        const fs::path sandboxWrapperPath = flatpakWrapperPath(browser.flatpakId);
        qInfo() << "file-op:" << "remove" << PathUtils::toQString(sandboxWrapperPath);
        if (!m_dryRun)
            fs::remove(sandboxWrapperPath, ec);

        const std::optional<fs::path> hostPath = hostManifestPath(browser);
        const fs::path flatpakPath = flatpakManifestPath(browser);
        if (hostPath.has_value())
            qInfo() << "file-op:" << "remove" << PathUtils::toQString(hostPath.value());
        qInfo() << "file-op:" << "remove" << PathUtils::toQString(flatpakPath);
        if (!m_dryRun) {
            if (hostPath.has_value())
                fs::remove(hostPath.value(), ec);
            fs::remove(flatpakPath, ec);
        }

        allOk = setBrowserTalkPermission(browser.flatpakId, false, m_dryRun) && allOk;
    }

    return allOk;
}

bool NativeHostIntegrator::ensureWrapperTemplateLoaded()
{
    if (!m_wrapperTemplate.isEmpty())
        return true;

    const fs::path installedPath{std::string(kInstalledWrapperPath)};
    qInfo() << "file-op:" << "read" << PathUtils::toQString(installedPath);
    QFile f(installedPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to read wrapper template:" << f.fileName();
        return false;
    }

    m_wrapperTemplate = QString::fromUtf8(f.readAll());
    return !m_wrapperTemplate.isEmpty();
}
