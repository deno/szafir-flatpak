#include "NativeMessagingService.h"

#include <QDBusMessage>

#include "AppSettings.h"
#include "config.h"
#include "LandlockEnv.h"
#include "LandlockSandbox.h"
#include "BwrapSandbox.h"

#include <QDebug>
#include <QDir>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#include <vector>

#include <KConfig>
#include <KConfigGroup>

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

// ---- NativeMessagingService ------------------------------------------------

NativeMessagingService::NativeMessagingService(QObject *parent)
    : QAbstractListModel(parent)
{
    // Create the D-Bus adaptor as a child; Qt will export it automatically
    // when this object is registered on the bus.
    new NativeMessagingAdaptor(this);
}

NativeMessagingService::~NativeMessagingService()
{
    qDebug() << "NativeMessagingService shutting down, quitting spawned processes gracefully...";
    for (QProcess *process : m_activeClients.keys()) {
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            process->waitForFinished(3000);
        }
        delete process;
    }
    m_activeClients.clear();
}

void NativeMessagingService::setAcceptingConnections(bool accepting)
{
    m_acceptingConnections = accepting;
    qDebug() << "NativeMessagingService: accepting connections:" << accepting;
}

static void logFd(const char *label, const QDBusUnixFileDescriptor &fd)
{
    int raw = fd.fileDescriptor();
    int flags = fcntl(raw, F_GETFL);
    int fdflags = fcntl(raw, F_GETFD);
    qDebug() << label << "fd:" << raw
             << "valid:" << fd.isValid()
             << "F_GETFL:" << flags
             << "F_GETFD:" << fdflags;
}

// ---- QAbstractListModel ---------------------------------------------------

int NativeMessagingService::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_clientList.size();
}

QVariant NativeMessagingService::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clientList.size())
        return {};
    const ClientInfo &ci = m_clientList.at(index.row());
    switch (role) {
    case ClientNameRole:  return ci.clientName;
    case IconRole:        return ci.icon;
    case FlatpakIdRole:   return ci.flatpakId;
    case ExecutableRole:  return ci.executable;
    case BrowserTypeRole: return ci.browserType;
    case DbusHandleRole:  return ci.dbusHandle;
    case PidRole:         return ci.pid;
    default:              return {};
    }
}

QHash<int, QByteArray> NativeMessagingService::roleNames() const
{
    return {
        { ClientNameRole,  "clientName"  },
        { IconRole,        "browserIcon" },
        { FlatpakIdRole,   "flatpakId"   },
        { ExecutableRole,  "executable"  },
        { BrowserTypeRole, "browserType" },
        { DbusHandleRole,  "dbusHandle"  },
        { PidRole,         "pid"         },
    };
}

int NativeMessagingService::clientListIndexByPid(qint64 pid) const
{
    for (int i = 0; i < m_clientList.size(); ++i) {
        if (m_clientList.at(i).pid == pid)
            return i;
    }
    return -1;
}

QString NativeMessagingService::currentDbusSender() const
{
    if (!calledFromDBus())
        return {};
    return message().service();
}

static void dup2OrExit(int srcFd, int dstFd, const char *label)
{
    if (dup2(srcFd, dstFd) < 0) {
        const char prefix[] = "dup2(";
        const char middle[] = ") failed: ";
        [[maybe_unused]] const ssize_t w1 = ::write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
        [[maybe_unused]] const ssize_t w2 = ::write(STDERR_FILENO, label, strlen(label));
        [[maybe_unused]] const ssize_t w3 = ::write(STDERR_FILENO, middle, sizeof(middle) - 1);
        const char *err = strerror(errno);
        [[maybe_unused]] const ssize_t w4 = ::write(STDERR_FILENO, err, strlen(err));
        [[maybe_unused]] const ssize_t w5 = ::write(STDERR_FILENO, "\n", 1);
        _exit(127);
    }
}

void NativeMessagingService::spawnHost(const QStringList &args,
                                        const QDBusUnixFileDescriptor &fdIn,
                                        const QDBusUnixFileDescriptor &fdOut,
                                        const QDBusUnixFileDescriptor &fdErr,
                                        const ClientInfo &clientInfo)
{
    if (!m_acceptingConnections) {
        qDebug() << "Link called but connections are not accepted yet (wizard in progress), ignoring";
        return;
    }

    qDebug() << "=== Link called ===";
    qDebug() << "args:" << args;
    qDebug() << "client:" << clientInfo.clientName
             << "dbus:" << clientInfo.dbusHandle
             << "icon:" << clientInfo.icon
             << "flatpak-id:" << clientInfo.flatpakId
             << "executable:" << clientInfo.executable;

    logFd("fdIn",  fdIn);
    logFd("fdOut", fdOut);
    logFd("fdErr", fdErr);

    if (!fdIn.isValid() || !fdOut.isValid() || !fdErr.isValid()) {
        qWarning() << "Invalid file descriptor(s), refusing to spawn SzafirHost";
        return;
    }

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    const int inFd = fdIn.fileDescriptor();
    const int outFd = fdOut.fileDescriptor();
    const int errFd = fdErr.fileDescriptor();

    const std::string startScript = std::string(RUNTIME_PREFIX) + "/bin/start-szafir-host-native.sh";

    // Pre-capture paths in the parent before fork (used by the non-bwrap branch and
    // to ensure the install dir exists below).
    const QByteArray homeEnv = qgetenv("HOME");
    const QByteArray xdgDataHomeEnv = qgetenv("XDG_DATA_HOME");
    const QByteArray xauthorityEnv = qgetenv("XAUTHORITY");
    const std::string launcherHome = homeEnv.isEmpty() ? std::string("/") : homeEnv.toStdString();
    const std::string launcherXdgDataHome = xdgDataHomeEnv.isEmpty()
        ? (launcherHome + "/.local/share")
        : xdgDataHomeEnv.toStdString();
    const std::string launcherXauthority = xauthorityEnv.toStdString(); // empty = use ~/.Xauthority fallback

    if (BwrapSandbox::childWrappingEnabled()) {
        // Wrap the runtime in its own bwrap namespace. Landlock is applied *inside*
        // the namespace by the --launch-host shim (it must come after bwrap's mount
        // setup), so the fork handler only wires the browser FDs onto stdio — bwrap
        // passes 0/1/2 through to the sandboxed command.
        std::vector<std::string> scriptArgs;
        scriptArgs.reserve(args.size());
        for (const QString &arg : args)
            scriptArgs.push_back(arg.toStdString());

        const std::vector<std::string> bwrapArgs = BwrapSandbox::childSandboxArgs(
            BwrapSandbox::selfExePath(), startScript, scriptArgs);

        QStringList qArgs;
        qArgs.reserve(static_cast<int>(bwrapArgs.size()));
        for (const std::string &a : bwrapArgs)
            qArgs.append(QString::fromStdString(a));

        process->setProgram(QString::fromStdString(BwrapSandbox::bwrapPath()));
        process->setArguments(qArgs);

        process->setChildProcessModifier([inFd, outFd, errFd]() {
            dup2OrExit(inFd, STDIN_FILENO, "stdin");
            dup2OrExit(outFd, STDOUT_FILENO, "stdout");
            dup2OrExit(errFd, STDERR_FILENO, "stderr");
        });
    } else {
        // Flatpak (or bwrap unavailable): exec the start script directly and apply the
        // Landlock launcher restrictions in the fork handler.
        process->setProgram(QString::fromStdString(startScript));
        process->setArguments(args);

        const bool launcherLandlockEnabled = LandlockEnv::isModuleEnabled("LANDLOCK_LAUNCHER");
        if (!launcherLandlockEnabled)
            qInfo() << "Landlock launcher restrictions disabled by environment.";

        process->setChildProcessModifier([inFd, outFd, errFd, launcherHome, launcherXdgDataHome, launcherXauthority, launcherLandlockEnabled]() {
            if (launcherLandlockEnabled) {
                LandlockSandbox::applyLauncherRestrictions(launcherHome.c_str(), launcherXdgDataHome.c_str(), launcherXauthority.c_str());
            }

            dup2OrExit(inFd, STDIN_FILENO, "stdin");
            dup2OrExit(outFd, STDOUT_FILENO, "stdout");
            dup2OrExit(errFd, STDERR_FILENO, "stderr");
        });
    }

    connect(process, &QProcess::finished, this,
        [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
            qDebug() << "SzafirHost process" << process->processId()
                     << "finished:" << exitStatus << "exit code:" << exitCode;

            // Capture pre-dup2 stderr (Landlock / dup2 errors written before exec).
            const QByteArray stderrOutput = process->readAllStandardError();
            if (!stderrOutput.isEmpty()) {
                qWarning() << "SzafirHost stderr:" << stderrOutput.trimmed();
            }

            if (m_activeClients.contains(process)) {
                const qint64 pid = m_activeClients.value(process).pid;
                m_activeClients.remove(process);
                const int idx = clientListIndexByPid(pid);
                if (idx >= 0) {
                    beginRemoveRows({}, idx, idx);
                    m_clientList.removeAt(idx);
                    endRemoveRows();
                }
                Q_EMIT activeHostCountChanged(m_activeClients.size());
                if (m_activeClients.isEmpty())
                    Q_EMIT allClientsStopped();
            }
            process->deleteLater();
        });

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    env.insert(QStringLiteral("SZAFIR_HOST_DEBUG"),
               QLoggingCategory::defaultCategory()->isDebugEnabled()
                   ? QStringLiteral("1") : QStringLiteral("0"));

    // Apply GDK_SCALE preference stored in the local override file.
    {
        KConfig overrideConfig(PathUtils::toQString(bundledHostOverridePath()), KConfig::SimpleConfig);
        KConfigGroup envGroup = overrideConfig.group(QStringLiteral("Environment"));
        const QString gdkScale = envGroup.readEntry(QStringLiteral("GDK_SCALE"), QString());
        if (!gdkScale.isEmpty()) {
            qDebug() << "Applying GDK_SCALE=" << gdkScale << "to bundled SzafirHost";
            env.insert(QStringLiteral("GDK_SCALE"), gdkScale);
        }
    }

    process->setProcessEnvironment(env);

    // Ensure the install dir exists before the child runs
    QDir().mkpath(QString::fromStdString(launcherXdgDataHome + "/szafir-host-proxy/szafir_host"));

    process->start();
    if (!process->waitForStarted()) {
        qWarning() << "Failed to start bundled SzafirHost:" << process->errorString();
        process->deleteLater();
        return;
    }

    ClientInfo ci = clientInfo;
    ci.pid = process->processId();
    m_activeClients.insert(process, ci);
    const int pos = m_clientList.size();
    beginInsertRows({}, pos, pos);
    m_clientList.append(ci);
    endInsertRows();
    qDebug() << "SzafirHost spawned, PID:" << process->processId();
    Q_EMIT activeHostCountChanged(m_activeClients.size());
}

void NativeMessagingService::stopClient(qint64 pid)
{
    for (auto it = m_activeClients.begin(); it != m_activeClients.end(); ++it) {
        if (it.key()->processId() == pid) {
            qDebug() << "Terminating bundled SzafirHost process" << pid;
            it.key()->terminate();
            return;
        }
    }
    qWarning() << "stopClient: no bundled process with PID" << pid;
}

void NativeMessagingService::stopAllClients()
{
    if (m_activeClients.isEmpty()) {
        Q_EMIT allClientsStopped();
        return;
    }

    qDebug() << "NativeMessagingService: stopping all" << m_activeClients.size() << "client(s)";

    for (QProcess *process : m_activeClients.keys())
        process->terminate();

    QTimer::singleShot(5000, this, [this]() {
        for (QProcess *process : m_activeClients.keys()) {
            if (process->state() != QProcess::NotRunning) {
                qWarning() << "Force-killing SzafirHost process" << process->processId();
                process->kill();
            }
        }
    });
}

// ---- NativeMessagingAdaptor ------------------------------------------------

NativeMessagingAdaptor::NativeMessagingAdaptor(NativeMessagingService *parent)
    : QDBusAbstractAdaptor(parent)
{
    setAutoRelaySignals(false);
}

void NativeMessagingAdaptor::Link(const QStringList &args,
                                   const QDBusUnixFileDescriptor &fd_in,
                                   const QDBusUnixFileDescriptor &fd_out,
                                   const QDBusUnixFileDescriptor &fd_err)
{
    auto *service = static_cast<NativeMessagingService *>(parent());

    qDebug() << "=== Link called (legacy, no metadata) ===";

    ClientInfo clientInfo;
    clientInfo.dbusHandle = service->currentDbusSender();
    clientInfo.clientName = clientInfo.dbusHandle;

    qDebug() << "Created ClientInfo with D-Bus handle:" << clientInfo.dbusHandle;

    service->spawnHost(args, fd_in, fd_out, fd_err, clientInfo);
}

void NativeMessagingAdaptor::LinkWithMetadata(const QStringList &args,
                                              const QDBusUnixFileDescriptor &fd_in,
                                              const QDBusUnixFileDescriptor &fd_out,
                                              const QDBusUnixFileDescriptor &fd_err,
                                              const QVariantMap &metadata)
{
    auto *service = static_cast<NativeMessagingService *>(parent());

    qDebug() << "=== LinkWithMetadata called ===";
    qDebug() << "Raw metadata map keys:" << metadata.keys();
    for (const QString &key : metadata.keys()) {
        qDebug() << "  " << key << ":" << metadata.value(key);
    }

    ClientInfo clientInfo;
    clientInfo.clientName = metadata.value(QStringLiteral("client-name")).toString().trimmed();
    clientInfo.icon = metadata.value(QStringLiteral("icon")).toString().trimmed();
    clientInfo.flatpakId = metadata.value(QStringLiteral("flatpak-id")).toString().trimmed();
    clientInfo.executable = metadata.value(QStringLiteral("executable")).toString().trimmed();
    clientInfo.browserType = metadata.value(QStringLiteral("browser-type")).toString().trimmed();
    clientInfo.dbusHandle = service->currentDbusSender();

    qDebug() << "Extracted metadata:"
             << "client-name=" << clientInfo.clientName
             << "icon=" << clientInfo.icon
             << "flatpak-id=" << clientInfo.flatpakId
             << "executable=" << clientInfo.executable
             << "browser-type=" << clientInfo.browserType
             << "dbus-handle=" << clientInfo.dbusHandle;

    if (clientInfo.clientName.isEmpty()) {
        clientInfo.clientName = clientInfo.dbusHandle;
        qDebug() << "Client name was empty, using D-Bus handle as name:" << clientInfo.clientName;
    }

    service->spawnHost(args, fd_in, fd_out, fd_err, clientInfo);
}
