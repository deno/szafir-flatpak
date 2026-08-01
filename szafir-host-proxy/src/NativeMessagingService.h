#pragma once

#include "config.h"

#include <QAbstractListModel>
#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QDBusUnixFileDescriptor>
#include <QMap>
#include <QList>
#include <QStringList>
#include <QVariantMap>

class QProcess;

struct ClientInfo
{
    QString clientName;
    QString icon;
    QString flatpakId;
    QString executable;
    QString browserType;  // "chrome", "firefox", "webkit", "generic", or empty
    QString dbusHandle;
    qint64  pid = 0;
};

// ---------------------------------------------------------------------------
// NativeMessagingService
//
// The real service object registered on the session bus.  It owns a
// NativeMessagingAdaptor child (created in the constructor), which Qt's
// D-Bus infrastructure detects automatically when the object is registered
// via QDBusConnection::registerObject().
// ---------------------------------------------------------------------------
class NativeMessagingService : public QAbstractListModel, protected QDBusContext
{
    Q_OBJECT
public:
    enum class Mode { Live, Mock };

    enum Role {
        ClientNameRole = Qt::UserRole + 1,
        IconRole,
        FlatpakIdRole,
        ExecutableRole,
        BrowserTypeRole,
        DbusHandleRole,
        PidRole,
    };

    explicit NativeMessagingService(Mode mode = Mode::Live, QObject *parent = nullptr);
    ~NativeMessagingService() override;

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool isAcceptingConnections() const { return m_acceptingConnections; }
    void setAcceptingConnections(bool accepting);

    // Called by the adaptor; spawns SzafirHost with fdIn/fdOut/fdErr
    // forwarded as stdin/stdout/stderr via a local QProcess.
    void spawnHost(const QStringList &args,
                   const QDBusUnixFileDescriptor &fdIn,
                   const QDBusUnixFileDescriptor &fdOut,
                   const QDBusUnixFileDescriptor &fdErr,
                   const ClientInfo &clientInfo);

    int activeHostCount() const { return m_mode == Mode::Mock ? m_clientList.size() : m_activeClients.size(); }
    QString currentDbusSender() const;
    void stopClient(qint64 pid);
    void stopAllClients();

Q_SIGNALS:
    void activeHostCountChanged(int count);
    void allClientsStopped();

private:
    Mode m_mode = Mode::Live;
    bool m_acceptingConnections = false;

    QMap<QProcess *, ClientInfo> m_activeClients;
    QList<ClientInfo> m_clientList; // ordered list mirroring m_activeClients for the model

    int clientListIndexByPid(qint64 pid) const;
};

// ---------------------------------------------------------------------------
// NativeMessagingAdaptor
//
// Thin QDBusAbstractAdaptor that exposes the D-Bus interface and delegates
// the Link() call to NativeMessagingService::spawnHost().
// ---------------------------------------------------------------------------
class NativeMessagingAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", DBUS_INTERFACE)
public:
    explicit NativeMessagingAdaptor(NativeMessagingService *parent);

public Q_SLOTS:
    // NoReply: the browser wrapper does not wait for a return value.
    Q_NOREPLY void Link(const QStringList &args,
                        const QDBusUnixFileDescriptor &fd_in,
                        const QDBusUnixFileDescriptor &fd_out,
                        const QDBusUnixFileDescriptor &fd_err);

    // Optional metadata fields in metadata map:
    //  - client-name (s)
    //  - icon (s)
    //  - flatpak-id (s)
    //  - executable (s)
    Q_NOREPLY void LinkWithMetadata(const QStringList &args,
                                    const QDBusUnixFileDescriptor &fd_in,
                                    const QDBusUnixFileDescriptor &fd_out,
                                    const QDBusUnixFileDescriptor &fd_err,
                                    const QVariantMap &metadata);
};
