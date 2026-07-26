#include "UpdateController.h"
#include "AppSettings.h"
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

constexpr const char *kDiscoveryUrl = "https://www.elektronicznypodpis.pl/aplikacje-i-sterowniki";
constexpr int kMaxPageBytes = 4 * 1024 * 1024;
constexpr int kInstallerTimeoutMs = 5 * 60 * 1000;

std::filesystem::path updateSettingsPath()
{
    const QString configHomeEnv = qEnvironmentVariable("XDG_CONFIG_HOME");
    const std::filesystem::path configHome = configHomeEnv.isEmpty()
        ? PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        : PathUtils::toFsPath(configHomeEnv);
    return configHome / "szafir-host-proxy" / "update-settings.json";
}

DiscoveredRuntime parseDiscoveryPage(const QByteArray &html)
{
    DiscoveredRuntime result;

    static const QRegularExpression linkRe(
        QStringLiteral(R"re(href\s*=\s*"(https?://[^"]*/([0-9a-fA-F]{32})/szafirhost-install\.jar)")re"));

    QRegularExpressionMatchIterator it = linkRe.globalMatch(QString::fromUtf8(html));

    struct Candidate {
        QUrl url;
        QString hash;
        int pos;
    };
    QList<Candidate> candidates;

    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QUrl url(m.captured(1));
        if (url.scheme() != QLatin1String("https"))
            continue;
        if (url.host() != QLatin1String("www.elektronicznypodpis.pl"))
            continue;
        candidates.append({url, m.captured(2), m.capturedStart()});
    }

    if (candidates.isEmpty())
        return result;

    const Candidate *best = &candidates.first();
    for (const Candidate &c : candidates) {
        int ctxStart = qMax(0, c.pos - 400);
        int ctxEnd = qMin(html.size(), c.pos + 400);
        QString ctx = QString::fromUtf8(html.mid(ctxStart, ctxEnd - ctxStart)).toLower();
        if (ctx.contains(QLatin1String("macos")) || ctx.contains(QLatin1String("linux"))) {
            best = &c;
            break;
        }
    }

    result.url = best->url;
    result.urlHash = best->hash;
    result.valid = true;

    int ctxStart = qMax(0, best->pos - 400);
    int ctxEnd = qMin(html.size(), best->pos + 400);
    QString ctx = QString::fromUtf8(html.mid(ctxStart, ctxEnd - ctxStart));

    static const QRegularExpression versionRe(
        QStringLiteral(R"(wersja\s+([0-9]+(?:\.[0-9]+)+))"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch vm = versionRe.match(ctx);
    if (vm.hasMatch())
        result.version = vm.captured(1);

    return result;
}

} // namespace

UpdateController::UpdateController(ComponentDownloader *downloader,
                                   NativeMessagingService *service,
                                   QObject *parent)
    : QObject(parent)
    , m_downloader(downloader)
    , m_service(service)
    , m_nam(new QNetworkAccessManager(this))
{
    loadSettings();
    loadRuntimeState();
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
    m_checkIntervalHours = obj[QStringLiteral("checkIntervalHours")].toInt(24);
}

void UpdateController::saveSettings()
{
    QJsonObject obj;
    obj[QStringLiteral("autoUpdate")] = m_autoUpdate;
    obj[QStringLiteral("allowDowngrades")] = m_allowDowngrades;
    obj[QStringLiteral("lastCheckTime")] = m_lastCheckTime;
    obj[QStringLiteral("lastDeclinedUrlHash")] = m_lastDeclinedUrlHash;
    obj[QStringLiteral("checkIntervalHours")] = m_checkIntervalHours;

    std::error_code ec;
    std::filesystem::create_directories(updateSettingsPath().parent_path(), ec);
    QSaveFile f(PathUtils::toQString(updateSettingsPath()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson());
        f.commit();
    }
}

void UpdateController::loadRuntimeState()
{
    QFile f(PathUtils::toQString(componentStatePath()));
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    QJsonObject runtime = obj[QStringLiteral("runtime")].toObject();
    m_installedUrlHash = runtime[QStringLiteral("installedUrlHash")].toString();
    m_installedVersion = runtime[QStringLiteral("installedVersion")].toString();
    m_installedSource = runtime[QStringLiteral("source")].toString();

    if (m_installedVersion.isEmpty() && m_installedUrlHash.isEmpty()) {
        QJsonObject components = obj[QStringLiteral("components")].toObject();
        QJsonObject installer = components[QStringLiteral("szafirhost-installer")].toObject();
        if (installer.contains(QStringLiteral("urlHash"))) {
            m_installedUrlHash = installer[QStringLiteral("urlHash")].toString();
            m_installedVersion = installer[QStringLiteral("version")].toString();
        }
    }
}

void UpdateController::saveRuntimeState()
{
    QFile f(PathUtils::toQString(componentStatePath()));
    QJsonObject obj;
    if (f.open(QIODevice::ReadOnly))
        obj = QJsonDocument::fromJson(f.readAll()).object();

    QJsonObject runtime;
    runtime[QStringLiteral("installedUrlHash")] = m_installedUrlHash;
    runtime[QStringLiteral("installedVersion")] = m_installedVersion;
    runtime[QStringLiteral("installedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    runtime[QStringLiteral("source")] = m_installedSource;
    obj[QStringLiteral("runtime")] = runtime;

    QSaveFile sf(PathUtils::toQString(componentStatePath()));
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(obj).toJson());
        sf.commit();
    }

    Q_EMIT runtimeInfoChanged();
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

    QUrl url{QString::fromLatin1(kDiscoveryUrl)};
    QNetworkRequest request = SecureNetwork::makeSecureRequest(url);
    QNetworkReply *reply = m_nam->get(request);
    SecureNetwork::attachSslAbort(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() {
        onDiscoveryFinished(reply, manual);
    });
}

void UpdateController::onDiscoveryFinished(QNetworkReply *reply, bool manual)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (manual) {
            setError(i18n("Update check failed: %1. You can install from a local file instead.",
                          reply->errorString()));
        } else {
            qWarning() << "UpdateController: automatic check failed:" << reply->errorString();
            setState(Idle);
        }
        return;
    }

    QByteArray body = reply->read(kMaxPageBytes);
    m_lastCheckTime = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    saveSettings();

    DiscoveredRuntime discovered = parseDiscoveryPage(body);
    if (!discovered.valid) {
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

void UpdateController::evaluateDiscovery(const DiscoveredRuntime &discovered, bool manual)
{
    bool changed = (discovered.urlHash != m_installedUrlHash)
                || (!discovered.version.isEmpty() && discovered.version != m_installedVersion);

    if (!changed && !m_forceMode) {
        if (manual)
            setState(UpToDate);
        else
            setState(Idle);
        return;
    }

    if (!m_allowDowngrades && !m_forceMode
        && !discovered.version.isEmpty() && !m_installedVersion.isEmpty()) {
        QVersionNumber discoveredV = QVersionNumber::fromString(discovered.version);
        QVersionNumber installedV = QVersionNumber::fromString(m_installedVersion);
        if (!discoveredV.isNull() && !installedV.isNull() && discoveredV < installedV) {
            qDebug() << "UpdateController: suppressing downgrade" << discovered.version << "<" << m_installedVersion;
            setState(manual ? UpToDate : Idle);
            return;
        }
    }

    if (!manual && discovered.urlHash == m_lastDeclinedUrlHash) {
        qDebug() << "UpdateController: skipping previously declined update";
        setState(Idle);
        return;
    }

    m_pendingUpdate = discovered;
    m_availableVersion = discovered.version;

    if (m_autoUpdate && !manual) {
        applyUpdate();
        return;
    }

    bool isDowngrade = false;
    if (!discovered.version.isEmpty() && !m_installedVersion.isEmpty()) {
        QVersionNumber dV = QVersionNumber::fromString(discovered.version);
        QVersionNumber iV = QVersionNumber::fromString(m_installedVersion);
        isDowngrade = !dV.isNull() && !iV.isNull() && dV < iV;
    }

    setState(UpdateAvailable);
    Q_EMIT updateAvailable(discovered.version, isDowngrade);
}

void UpdateController::applyUpdate()
{
    if (!m_pendingUpdate.valid)
        return;

    if (m_service->activeHostCount() > 0) {
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

    m_downloader->overrideComponentSource(
        QStringLiteral("szafirhost-installer"),
        m_pendingUpdate.url,
        m_pendingUpdate.version,
        m_pendingUpdate.urlHash);

    connect(m_downloader, &ComponentDownloader::allDownloadsComplete, this,
        [this]() {
            disconnect(m_downloader, &ComponentDownloader::allDownloadsComplete, this, nullptr);
            disconnect(m_downloader, &ComponentDownloader::downloadFailed, this, nullptr);
            onHostsStopped();
        }, Qt::SingleShotConnection);

    connect(m_downloader, &ComponentDownloader::downloadFailed, this,
        [this](const QString &id, const QString &err) {
            if (id == QLatin1String("szafirhost-installer")) {
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

    QDir().mkpath(QString::fromStdString(launcherXdgDataHome + "/szafir_host"));
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
        m_installedUrlHash = m_pendingUpdate.urlHash;
        m_installedVersion = m_pendingUpdate.version;
        m_installedSource = m_forceMode ? QStringLiteral("force") : QStringLiteral("web");
        saveRuntimeState();

        qDebug() << "UpdateController: runtime updated to" << m_installedVersion
                 << "hash:" << m_installedUrlHash;
    }

    m_service->setAcceptingConnections(true);
    m_pendingUpdate = {};
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

    bool hasLocalInstaller = false;
    for (const auto &e : m_downloader->components()) {
        if (e.info.id == QLatin1String("szafirhost-installer") && e.present) {
            hasLocalInstaller = true;
            break;
        }
    }

    if (hasLocalInstaller) {
        m_pendingUpdate.valid = true;
        m_pendingUpdate.urlHash = m_installedUrlHash;
        m_pendingUpdate.version = m_installedVersion;
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
    m_lastDeclinedUrlHash = m_pendingUpdate.urlHash;
    saveSettings();
    m_pendingUpdate = {};
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
    m_installedSource = QStringLiteral("file");

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
