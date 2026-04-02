#include "NativeMessagingService.h"

#include <QDBusMessage>

#ifdef BUNDLED_HOST
#include "AppSettings.h"
#include "LandlockSandbox.h"

#include <QDebug>
#include <QProcess>
#include <QProcessEnvironment>

#include <KConfig>
#include <KConfigGroup>

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#else // !BUNDLED_HOST
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QProcessEnvironment>

#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

// a{uh}: Flatpak host-command fd map — uint (target fd number) → Unix fd handle
using FdMap = QMap<quint32, QDBusUnixFileDescriptor>;
Q_DECLARE_METATYPE(FdMap)
#endif // BUNDLED_HOST

// ---- NativeMessagingService ------------------------------------------------

NativeMessagingService::NativeMessagingService(QObject *parent)
    : QObject(parent)
{
#ifndef BUNDLED_HOST
    qDBusRegisterMetaType<FdMap>();
    qDBusRegisterMetaType<QByteArrayList>();
#endif

    // Create the D-Bus adaptor as a child; Qt will export it automatically
    // when this object is registered on the bus.
    new NativeMessagingAdaptor(this);

#ifndef BUNDLED_HOST
    // Subscribe to HostCommandExited so we can log child process terminations.
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.Flatpak"),
        QStringLiteral("/org/freedesktop/Flatpak/Development"),
        QStringLiteral("org.freedesktop.Flatpak.Development"),
        QStringLiteral("HostCommandExited"),
        this,
        SLOT(onSpawnExited(quint32, quint32)));
#endif
}

NativeMessagingService::~NativeMessagingService()
{
    qDebug() << "NativeMessagingService shutting down, quitting spawned processes gracefully...";
#ifdef BUNDLED_HOST
    for (QProcess *process : m_activeClients.keys()) {
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            process->waitForFinished(3000);
        }
        delete process;
    }
    m_activeClients.clear();
#else
    for (quint32 pid : m_activeClients.keys()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.Flatpak"),
            QStringLiteral("/org/freedesktop/Flatpak/Development"),
            QStringLiteral("org.freedesktop.Flatpak.Development"),
            QStringLiteral("HostCommandSignal"));
        msg << quint32(pid) << quint32(15); // SIGTERM
        QDBusConnection::sessionBus().call(msg);
    }
#endif
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

QList<ClientInfo> NativeMessagingService::clients() const
{
    return m_activeClients.values();
}

QString NativeMessagingService::currentDbusSender() const
{
    if (!calledFromDBus())
        return {};
    return message().service();
}

#ifdef BUNDLED_HOST
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
#endif

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

#ifdef BUNDLED_HOST
    if (!fdIn.isValid() || !fdOut.isValid() || !fdErr.isValid()) {
        qWarning() << "Invalid file descriptor(s), refusing to spawn SzafirHost";
        return;
    }

    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("/app/bin/start-szafir-host-native.sh"));
    process->setArguments(args);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    const int inFd = fdIn.fileDescriptor();
    const int outFd = fdOut.fileDescriptor();
    const int errFd = fdErr.fileDescriptor();

    // Pre-capture paths in the parent before fork
    const QByteArray homeEnv = qgetenv("HOME");
    const QByteArray xdgDataHomeEnv = qgetenv("XDG_DATA_HOME");
    const std::string launcherHome = homeEnv.isEmpty() ? std::string("/") : homeEnv.toStdString();
    const std::string launcherXdgDataHome = xdgDataHomeEnv.isEmpty()
        ? (launcherHome + "/.local/share")
        : xdgDataHomeEnv.toStdString();

    process->setChildProcessModifier([inFd, outFd, errFd, launcherHome, launcherXdgDataHome]() {
        LandlockSandbox::applyLauncherRestrictions(launcherHome.c_str(), launcherXdgDataHome.c_str());

        dup2OrExit(inFd, STDIN_FILENO, "stdin");
        dup2OrExit(outFd, STDOUT_FILENO, "stdout");
        dup2OrExit(errFd, STDERR_FILENO, "stderr");
    });

    connect(process, &QProcess::finished, this,
        [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
            qDebug() << "SzafirHost process" << process->processId()
                     << "finished:" << exitStatus << "exit code:" << exitCode;

            if (m_activeClients.remove(process) > 0) {
                Q_EMIT activeHostCountChanged(m_activeClients.size());
                Q_EMIT clientsChanged();
            }
            process->deleteLater();
        });

    // Apply GDK_SCALE preference stored in the local override file.
    {
        KConfig overrideConfig(PathUtils::toQString(bundledHostOverridePath()), KConfig::SimpleConfig);
        KConfigGroup envGroup = overrideConfig.group(QStringLiteral("Environment"));
        const QString gdkScale = envGroup.readEntry(QStringLiteral("GDK_SCALE"), QString());
        if (!gdkScale.isEmpty()) {
            qDebug() << "Applying GDK_SCALE=" << gdkScale << "to bundled SzafirHost";
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("GDK_SCALE"), gdkScale);
            process->setProcessEnvironment(env);
        }
    }

    process->start();
    if (!process->waitForStarted()) {
        qWarning() << "Failed to start bundled SzafirHost:" << process->errorString();
        process->deleteLater();
        return;
    }

    ClientInfo ci = clientInfo;
    ci.pid = process->processId();
    m_activeClients.insert(process, ci);
    qDebug() << "SzafirHost spawned, PID:" << process->processId();
    Q_EMIT activeHostCountChanged(m_activeClients.size());
    Q_EMIT clientsChanged();

#else // !BUNDLED_HOST
    // Build the Flatpak Development HostCommand call.
    // Signature: HostCommand(ay cwd, aay argv, a{uh} fds, a{ss} envs, u flags) → u pid
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Flatpak"),
        QStringLiteral("/org/freedesktop/Flatpak/Development"),
        QStringLiteral("org.freedesktop.Flatpak.Development"),
        QStringLiteral("HostCommand"));

    // cwd_path (ay) — null-terminated byte path on the host.
    QByteArray cwd("/\0", 2);

    // argv (aay) — null-terminated byte-array list
    QByteArrayList argv;
    for (const QByteArray &arg : {
             QByteArray("/usr/bin/flatpak"),
             QByteArray("run"),
             QByteArray("pl.kir.szafirhost")
         }) {
        QByteArray value = arg;
        value.append('\0');
        argv.append(value);
    }
    for (const QString &arg : args) {
        QByteArray a = arg.toLocal8Bit();
        a.append('\0');
        argv.append(a);
    }

    qDebug() << "cwd_path:" << cwd.toHex() << "(" << cwd << ")";
    qDebug() << "argv[0]:" << argv.at(0).toHex() << "(" << argv.at(0) << ")";
    qDebug() << "argv count:" << argv.size();

    // fds (a{uh}) — map target fd numbers to the received Unix fds
    FdMap fds;
    fds[0] = fdIn;   // stdin
    fds[1] = fdOut;  // stdout
    fds[2] = fdErr;  // stderr

    qDebug() << "fds map: {0:" << fds[0].fileDescriptor()
             << ", 1:" << fds[1].fileDescriptor()
             << ", 2:" << fds[2].fileDescriptor() << "}";

    // Propagate critical environment variables so the host flatpak run
    // correctly identifies display parameters for the new sandbox.
    // However, do NOT propagate XAUTHORITY from the proxy sandbox as it
    // points to /run/flatpak/Xauthority which is an invalid path when
    // evaluated on the host system.
    QMap<QString, QString> envs;
    const QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
    for (const QString &key : sysEnv.keys()) {
        if (key == "DISPLAY" || key == "WAYLAND_DISPLAY" || key == "XDG_RUNTIME_DIR" || key == "DBUS_SESSION_BUS_ADDRESS") {
            envs.insert(key, sysEnv.value(key));
        }
    }

    // flags: 2 = FLATPAK_HOST_COMMAND_FLAGS_WATCH_BUS
    //   Kill spawned processes if this proxy loses its D-Bus connection.
    const quint32 flags = 2;

    msg << cwd                                         // cwd_path  (ay)
        << QVariant::fromValue(argv)                   // argv      (aay)
        << QVariant::fromValue(fds)                    // fds       (a{uh})
        << QVariant::fromValue(envs)                   // envs      (a{ss})
        << flags;                                      // flags     (u)

    qDebug() << "Sending HostCommand call, msg signature:" << msg.signature();
    qDebug() << "msg arguments count:" << msg.arguments().size();

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, clientInfo](QDBusPendingCallWatcher *self) {
            QDBusPendingReply<quint32> reply = *self;
            if (reply.isError()) {
                qWarning() << "Flatpak HostCommand failed:"
                           << reply.error().name()
                           << reply.error().message();
            } else {
                quint32 pid = reply.value();
                qDebug() << "SzafirHost spawned, host PID:" << pid;
                ClientInfo ci = clientInfo;
                ci.pid = pid;
                m_activeClients.insert(pid, ci);
                Q_EMIT activeHostCountChanged(m_activeClients.size());
                Q_EMIT clientsChanged();
            }
            self->deleteLater();
        });
#endif // BUNDLED_HOST
}

void NativeMessagingService::stopClient(qint64 pid)
{
#ifdef BUNDLED_HOST
    for (auto it = m_activeClients.begin(); it != m_activeClients.end(); ++it) {
        if (it.key()->processId() == pid) {
            qDebug() << "Terminating bundled SzafirHost process" << pid;
            it.key()->terminate();
            return;
        }
    }
    qWarning() << "stopClient: no bundled process with PID" << pid;
#else
    if (!m_activeClients.contains(quint32(pid))) {
        qWarning() << "stopClient: no tracked process with PID" << pid;
        return;
    }
    qDebug() << "Sending SIGTERM to host PID" << pid << "via Flatpak HostCommandSignal";
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Flatpak"),
        QStringLiteral("/org/freedesktop/Flatpak/Development"),
        QStringLiteral("org.freedesktop.Flatpak.Development"),
        QStringLiteral("HostCommandSignal"));
    msg << quint32(pid) << quint32(15); // SIGTERM
    QDBusConnection::sessionBus().asyncCall(msg);
#endif
}

#ifndef BUNDLED_HOST
void NativeMessagingService::onSpawnExited(quint32 pid, quint32 exitStatus)
{
    // exit_status is the raw wait status from waitpid(2)
    if (WIFEXITED(exitStatus)) {
        qDebug() << "SzafirHost PID" << pid
                 << "exited normally, exit code:" << WEXITSTATUS(exitStatus);
    } else if (WIFSIGNALED(exitStatus)) {
        qDebug() << "SzafirHost PID" << pid
                 << "killed by signal:" << WTERMSIG(exitStatus);
    } else {
        qDebug() << "SzafirHost PID" << pid
                 << "exited, raw wait status:" << exitStatus;
    }

    if (m_activeClients.remove(pid) > 0) {
        Q_EMIT activeHostCountChanged(m_activeClients.size());
        Q_EMIT clientsChanged();
    }
}
#endif // !BUNDLED_HOST

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
