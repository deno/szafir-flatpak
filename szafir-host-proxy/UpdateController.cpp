#include "UpdateController.h"
#include "AppSettings.h"
#include "ComponentDiscovery.h"
#include "ComponentDownloader.h"
#include "NativeMessagingService.h"
#include "SecureNetwork.h"
#include "config.h"
#include "LandlockEnv.h"
#include "LandlockSandbox.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVersionNumber>

#include <filesystem>

namespace {

constexpr int kInstallerTimeoutMs = 5 * 60 * 1000;

std::filesystem::path updateSettingsPath()
{
    const QString configHomeEnv = qEnvironmentVariable("XDG_CONFIG_HOME");
    const std::filesystem::path configHome = configHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        : PathUtils::toFsPath(configHomeEnv);
    return configHome / "szafir-host-proxy" / "update-settings.json";
}

} // namespace

UpdateController::UpdateController(ComponentDownloader *downloader,
                                   NativeMessagingService *service,
                                   QObject *parent)
    : QObject(parent)
    , m_downloader(downloader)
    , m_service(service)
{
    loadSettings();
}

void UpdateController::setAutoUpdate(bool enabled)
{
    if (m_autoUpdate == enabled)
        return;
    m_autoUpdate = enabled;
    saveSettings();
    Q_EMIT settingsChanged();
}

void UpdateController::setAllowDowngrades(bool allowed)
{
    if (m_allowDowngrades == allowed)
        return;
    m_allowDowngrades = allowed;
    saveSettings();
    Q_EMIT settingsChanged();
}

void UpdateController::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    Q_EMIT stateChanged();
}

void UpdateController::setProgress(qreal p)
{
    if (qFuzzyCompare(m_progress, p))
        return;
    m_progress = p;
    Q_EMIT progressChanged();
}

void UpdateController::setError(const QString &msg)
{
    m_errorString = msg;
    setState(Error);
}

void UpdateController::loadSettings()
{
    QFile f(PathUtils::toQString(updateSettingsPath()));
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    m_autoUpdate = obj[QStringLiteral("autoUpdate")].toBool(false);
    m_allowDowngrades = obj[QStringLiteral("allowDowngrades")].toBool(true);
    m_lastCheckTime = obj[QStringLiteral("lastCheckTime")].toString();
    m_lastDeclinedUrlHash = obj[QStringLiteral("lastDeclinedUrlHash")].toString();
    m_lastDeclinedLibUrlHash = obj[QStringLiteral("lastDeclinedLibUrlHash")].toString();
    m_checkIntervalHours = obj[QStringLiteral("checkIntervalHours")].toInt(24);
}

void UpdateController::saveSettings()
{
    QJsonObject obj;
    obj[QStringLiteral("autoUpdate")] = m_autoUpdate;
    obj[QStringLiteral("allowDowngrades")] = m_allowDowngrades;
    obj[QStringLiteral("lastCheckTime")] = m_lastCheckTime;
    obj[QStringLiteral("lastDeclinedUrlHash")] = m_lastDeclinedUrlHash;
    obj[QStringLiteral("lastDeclinedLibUrlHash")] = m_lastDeclinedLibUrlHash;
    obj[QStringLiteral("checkIntervalHours")] = m_checkIntervalHours;

    std::error_code ec;
    std::filesystem::create_directories(updateSettingsPath().parent_path(), ec);
    QSaveFile f(PathUtils::toQString(updateSettingsPath()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson());
        f.commit();
    }
}

QString UpdateController::installedVersion() const
{
    for (const auto &e : m_downloader->components()) {
        if (e.info.id == QLatin1String("szafirhost-installer"))
            return e.info.version;
    }
    return {};
}

void UpdateController::checkForUpdates(bool manual)
{
    if (m_state == Checking || m_state == Downloading || m_state == Installing || m_state == StoppingHosts)
        return;

    m_manualCheck = manual;
    m_forceMode = false;
    setState(Checking);
    setProgress(-1);
    m_errorString.clear();

    connect(m_downloader, &ComponentDownloader::discoveryFinished, this,
        [this, manual](const DiscoveryResult &result) {
            onDiscoveryFinished(result, manual);
        }, Qt::SingleShotConnection);

    m_downloader->discoverComponents();
}

void UpdateController::onDiscoveryFinished(const DiscoveryResult &discovered, bool manual)
{
    m_lastCheckTime = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    saveSettings();

    if (!discovered.runtime.valid && !discovered.library.valid) {
        if (manual) {
            setError(i18n("Could not find SzafirHost download link on the website. "
                          "The site layout may have changed. You can install from a local file instead."));
        } else {
            qWarning() << "UpdateController: failed to parse discovery page";
            setState(Idle);
        }
        return;
    }

    evaluateDiscovery(discovered, manual);
}

void UpdateController::evaluateDiscovery(const DiscoveryResult &discovered, bool manual)
{
    // --- Runtime evaluation ---
    bool runtimeChanged = false;
    if (discovered.runtime.valid) {
        const ComponentDownloader::ComponentEntry *rtEntry = nullptr;
        for (const auto &e : m_downloader->components()) {
            if (e.info.id == QLatin1String("szafirhost-installer")) {
                rtEntry = &e;
                break;
            }
        }

        const QString installedHash = rtEntry ? rtEntry->urlHash : QString();
        const QString installedVersion = rtEntry ? rtEntry->info.version : QString();

        runtimeChanged = (discovered.runtime.urlHash != installedHash)
                      || (!discovered.runtime.version.isEmpty() && discovered.runtime.version != installedVersion);

        if (runtimeChanged && !m_allowDowngrades && !m_forceMode
            && !discovered.runtime.version.isEmpty() && !installedVersion.isEmpty()) {
            QVersionNumber discoveredV = QVersionNumber::fromString(discovered.runtime.version);
            QVersionNumber installedV = QVersionNumber::fromString(installedVersion);
            if (!discoveredV.isNull() && !installedV.isNull() && discoveredV < installedV) {
                qDebug() << "UpdateController: suppressing runtime downgrade" << discovered.runtime.version << "<" << installedVersion;
                runtimeChanged = false;
            }
        }

        if (runtimeChanged && !manual && discovered.runtime.urlHash == m_lastDeclinedUrlHash) {
            qDebug() << "UpdateController: skipping previously declined runtime update";
            runtimeChanged = false;
        }
    }

    // --- Library evaluation ---
    bool libraryChanged = false;
    if (discovered.library.valid) {
        // Only offer library updates when the component is already installed (user opted in).
        const ComponentDownloader::ComponentEntry *libEntry = nullptr;
        for (const auto &e : m_downloader->components()) {
            if (e.info.id == QLatin1String("libccgraphite")) {
                libEntry = &e;
                break;
            }
        }

        if (libEntry && libEntry->present) {
            libraryChanged = (!discovered.library.version.isEmpty() && discovered.library.version != libEntry->info.version)
                          || (!libEntry->urlHash.isEmpty() && discovered.library.urlHash != libEntry->urlHash);

            if (libraryChanged && !m_allowDowngrades && !m_forceMode
                && !discovered.library.version.isEmpty() && !libEntry->info.version.isEmpty()) {
                QVersionNumber discoveredV = QVersionNumber::fromString(discovered.library.version);
                QVersionNumber installedV = QVersionNumber::fromString(libEntry->info.version);
                if (!discoveredV.isNull() && !installedV.isNull() && discoveredV < installedV) {
                    qDebug() << "UpdateController: suppressing library downgrade" << discovered.library.version << "<" << libEntry->info.version;
                    libraryChanged = false;
                }
            }

            if (libraryChanged && !manual && discovered.library.urlHash == m_lastDeclinedLibUrlHash) {
                qDebug() << "UpdateController: skipping previously declined library update";
                libraryChanged = false;
            }
        }
    }

    if (!runtimeChanged && !libraryChanged && !m_forceMode) {
        if (manual)
            setState(UpToDate);
        else
            setState(Idle);
        return;
    }

    if (runtimeChanged)
        m_pendingUpdate = discovered.runtime;
    else
        m_pendingUpdate = {};

    if (libraryChanged)
        m_pendingLibrary = discovered.library;
    else
        m_pendingLibrary = {};

    m_availableVersion = m_pendingUpdate.valid ? m_pendingUpdate.version : QString();
    m_availableLibraryVersion = m_pendingLibrary.valid ? m_pendingLibrary.version : QString();

    if (m_autoUpdate && !manual) {
        applyUpdate();
        return;
    }

    bool isDowngrade = false;
    const QString curVersion = installedVersion();
    if (m_pendingUpdate.valid && !m_pendingUpdate.version.isEmpty() && !curVersion.isEmpty()) {
        QVersionNumber dV = QVersionNumber::fromString(m_pendingUpdate.version);
        QVersionNumber iV = QVersionNumber::fromString(curVersion);
        isDowngrade = !dV.isNull() && !iV.isNull() && dV < iV;
    }

    setState(UpdateAvailable);
    Q_EMIT updateAvailable(m_pendingUpdate.valid ? m_pendingUpdate.version : QString(), isDowngrade);
}

void UpdateController::applyUpdate()
{
    if (!m_pendingUpdate.valid && !m_pendingLibrary.valid)
        return;

    // Only runtime updates require stopping hosts; library updates are file-only.
    if (m_pendingUpdate.valid && m_service->activeHostCount() > 0) {
        m_deferUntilIdle = true;
        Q_EMIT interruptionConfirmationNeeded();
        return;
    }

    beginInstallSequence();
}

void UpdateController::confirmInterruption(bool proceed)
{
    if (!proceed) {
        m_deferUntilIdle = true;
        connect(m_service, &NativeMessagingService::activeHostCountChanged, this,
            [this](int count) {
                if (count == 0 && m_deferUntilIdle) {
                    m_deferUntilIdle = false;
                    disconnect(m_service, &NativeMessagingService::activeHostCountChanged, this, nullptr);
                    beginInstallSequence();
                }
            });
        return;
    }

    m_deferUntilIdle = false;
    beginInstallSequence();
}

void UpdateController::beginInstallSequence()
{
    setState(Downloading);
    setProgress(0);

    if (m_pendingUpdate.valid) {
        m_downloader->overrideComponentSource(
            QStringLiteral("szafirhost-installer"),
            m_pendingUpdate.url,
            m_pendingUpdate.version,
            m_pendingUpdate.urlHash);
    }

    if (m_pendingLibrary.valid) {
        m_downloader->overrideComponentSource(
            QStringLiteral("libccgraphite"),
            m_pendingLibrary.url,
            m_pendingLibrary.version,
            m_pendingLibrary.urlHash,
            m_pendingLibrary.filename);
    }

    connect(m_downloader, &ComponentDownloader::allDownloadsComplete, this,
        [this]() {
            disconnect(m_downloader, &ComponentDownloader::allDownloadsComplete, this, nullptr);
            disconnect(m_downloader, &ComponentDownloader::downloadFailed, this, nullptr);
            if (m_pendingUpdate.valid)
                onHostsStopped();
            else
                finalizeUpdate(true);
        }, Qt::SingleShotConnection);

    connect(m_downloader, &ComponentDownloader::downloadFailed, this,
        [this](const QString &id, const QString &err) {
            if (id == QLatin1String("szafirhost-installer") || id == QLatin1String("libccgraphite")) {
                disconnect(m_downloader, &ComponentDownloader::allDownloadsComplete, this, nullptr);
                disconnect(m_downloader, &ComponentDownloader::downloadFailed, this, nullptr);
                setError(i18n("Download failed: %1", err));
            }
        }, Qt::SingleShotConnection);

    m_downloader->startDownloads();
}

void UpdateController::onHostsStopped()
{
    setState(StoppingHosts);
    setProgress(-1);

    m_service->setAcceptingConnections(false);

    if (m_service->activeHostCount() == 0) {
        runInstaller();
        return;
    }

    connect(m_service, &NativeMessagingService::allClientsStopped, this,
        [this]() {
            disconnect(m_service, &NativeMessagingService::allClientsStopped, this, nullptr);
            runInstaller();
        }, Qt::SingleShotConnection);

    m_service->stopAllClients();
}

void UpdateController::runInstaller()
{
    setState(Installing);
    setProgress(-1);

    QString installerHash;
    for (const auto &e : m_downloader->components()) {
        if (e.info.id == QLatin1String("szafirhost-installer")) {
            installerHash = e.info.hash;
            break;
        }
    }

    const QByteArray homeEnv = qgetenv("HOME");
    const QByteArray xdgDataHomeEnv = qgetenv("XDG_DATA_HOME");
    const QByteArray xauthorityEnv = qgetenv("XAUTHORITY");
    const std::string launcherHome = homeEnv.isEmpty() ? std::string("/") : homeEnv.toStdString();
    const std::string launcherXdgDataHome = xdgDataHomeEnv.isEmpty()
        ? (launcherHome + "/.local/share")
        : xdgDataHomeEnv.toStdString();
    const std::string launcherXauthority = xauthorityEnv.toStdString();
    const bool launcherLandlockEnabled = LandlockEnv::isModuleEnabled("LANDLOCK_LAUNCHER");

    m_installerProcess = new QProcess(this);
    m_installerProcess->setProgram(
        QString::fromStdString(std::string(RUNTIME_PREFIX) + "/bin/start-szafir-host-native.sh"));
    m_installerProcess->setArguments({QStringLiteral("--install-only")});

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SZAFIR_FORCE_INSTALL"), QStringLiteral("1"));
    if (!installerHash.isEmpty())
        env.insert(QStringLiteral("SZAFIR_INSTALLER_SHA256"), installerHash);
    m_installerProcess->setProcessEnvironment(env);

    m_installerProcess->setChildProcessModifier(
        [launcherHome, launcherXdgDataHome, launcherXauthority, launcherLandlockEnabled]() {
            if (launcherLandlockEnabled) {
                LandlockSandbox::applyLauncherRestrictions(
                    launcherHome.c_str(), launcherXdgDataHome.c_str(), launcherXauthority.c_str());
            }
        });

    connect(m_installerProcess, &QProcess::finished, this,
        [this](int exitCode, QProcess::ExitStatus) {
            onInstallerFinished(exitCode);
        });

    connect(m_installerProcess, &QProcess::errorOccurred, this,
        [this](QProcess::ProcessError err) {
            qWarning() << "UpdateController: installer process error:" << err;
        });

    QDir().mkpath(QString::fromStdString(launcherXdgDataHome + "/szafir-host-proxy/szafir_host"));
    m_installerProcess->start();

    QTimer::singleShot(kInstallerTimeoutMs, this, [this]() {
        if (m_installerProcess && m_installerProcess->state() != QProcess::NotRunning) {
            qWarning() << "UpdateController: installer timed out, killing";
            m_installerProcess->kill();
        }
    });
}

void UpdateController::onInstallerFinished(int exitCode)
{
    if (m_installerProcess) {
        const QByteArray stderrOut = m_installerProcess->readAllStandardError();
        if (!stderrOut.isEmpty())
            qDebug() << "UpdateController: installer stderr:" << stderrOut.trimmed();
        m_installerProcess->deleteLater();
        m_installerProcess = nullptr;
    }

    finalizeUpdate(exitCode == 0);
}

void UpdateController::finalizeUpdate(bool success)
{
    if (success) {
        if (m_pendingUpdate.valid) {
            qDebug() << "UpdateController: runtime updated to" << m_pendingUpdate.version
                     << "hash:" << m_pendingUpdate.urlHash;
            Q_EMIT runtimeInfoChanged();
        }
        if (m_pendingLibrary.valid) {
            qDebug() << "UpdateController: library updated to" << m_pendingLibrary.version
                     << "hash:" << m_pendingLibrary.urlHash;
        }
    }

    m_service->setAcceptingConnections(true);
    m_pendingUpdate = {};
    m_pendingLibrary = {};
    m_availableLibraryVersion.clear();
    setProgress(-1);

    if (success) {
        setState(Idle);
    } else {
        setError(i18n("Installation failed. The previous runtime may still be usable."));
    }

    Q_EMIT updateFinished(success);
}

void UpdateController::forceReinstall()
{
    m_forceMode = true;
    m_manualCheck = true;

    const ComponentDownloader::ComponentEntry *rtEntry = nullptr;
    for (const auto &e : m_downloader->components()) {
        if (e.info.id == QLatin1String("szafirhost-installer") && e.present) {
            rtEntry = &e;
            break;
        }
    }

    if (rtEntry) {
        m_pendingUpdate.valid = true;
        m_pendingUpdate.urlHash = rtEntry->urlHash;
        m_pendingUpdate.version = rtEntry->info.version;
        onHostsStopped();
    } else {
        checkForUpdates(true);
        connect(this, &UpdateController::stateChanged, this, [this]() {
            if (m_state == UpdateAvailable && m_forceMode) {
                disconnect(this, &UpdateController::stateChanged, this, nullptr);
                applyUpdate();
            }
        });
    }
}

void UpdateController::dismissOffer()
{
    if (m_pendingUpdate.valid)
        m_lastDeclinedUrlHash = m_pendingUpdate.urlHash;
    if (m_pendingLibrary.valid)
        m_lastDeclinedLibUrlHash = m_pendingLibrary.urlHash;
    saveSettings();
    m_pendingUpdate = {};
    m_pendingLibrary = {};
    m_availableLibraryVersion.clear();
    setState(Idle);
}

void UpdateController::installFromFile(const QUrl &localFile)
{
    const QString path = localFile.toLocalFile();
    if (path.isEmpty()) {
        setError(i18n("Invalid file path."));
        return;
    }

    QFileInfo fi(path);
    if (!fi.exists() || !fi.isReadable()) {
        setError(i18n("File not found or not readable: %1", path));
        return;
    }

    if (fi.size() < 1024 || fi.size() > 500LL * 1024 * 1024) {
        setError(i18n("File size is outside expected range (1 KB – 500 MB)."));
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setError(i18n("Cannot open file: %1", f.errorString()));
        return;
    }

    const QByteArray magic = f.read(4);
    if (magic.size() < 4 || magic[0] != 'P' || magic[1] != 'K'
        || magic[2] != '\x03' || magic[3] != '\x04') {
        setError(i18n("The selected file does not appear to be a valid JAR/ZIP archive."));
        return;
    }

    f.seek(0);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        setError(i18n("Failed to compute file checksum."));
        return;
    }
    f.close();

    const QString sha256 = QString::fromLatin1(hash.result().toHex());

    if (!m_downloader->adoptLocalFile(QStringLiteral("szafirhost-installer"), path, sha256)) {
        setError(i18n("Failed to import file into component store."));
        return;
    }

    m_forceMode = true;
    m_pendingUpdate = {};
    m_pendingUpdate.valid = true;
    m_pendingUpdate.urlHash = QString();
    m_pendingUpdate.version = QString();

    onHostsStopped();
}

void UpdateController::startScheduling()
{
    QTimer::singleShot(15000, this, [this]() {
        checkForUpdates(false);
    });

    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(m_checkIntervalHours * 3600 * 1000);
    connect(m_checkTimer, &QTimer::timeout, this, [this]() {
        checkForUpdates(false);
    });
    m_checkTimer->start();
}
