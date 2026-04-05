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

#ifdef BUNDLED_HOST
class QProcess;
#endif

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
    enum Role {
        ClientNameRole = Qt::UserRole + 1,
        IconRole,
        FlatpakIdRole,
        ExecutableRole,
        BrowserTypeRole,
        DbusHandleRole,
        PidRole,
    };

    explicit NativeMessagingService(QObject *parent = nullptr);
    ~NativeMessagingService() override;

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool isAcceptingConnections() const { return m_acceptingConnections; }
    void setAcceptingConnections(bool accepting);

    // Called by the adaptor; spawns SzafirHost with fdIn/fdOut/fdErr
    // forwarded as stdin/stdout/stderr.
    // When BUNDLED_HOST is defined, runs the host as a local QProcess.
    // Otherwise, delegates to a separate Flatpak via the Development API.
    void spawnHost(const QStringList &args,
                   const QDBusUnixFileDescriptor &fdIn,
                   const QDBusUnixFileDescriptor &fdOut,
                   const QDBusUnixFileDescriptor &fdErr,
                   const ClientInfo &clientInfo);

    int activeHostCount() const { return m_activeClients.size(); }
    QString currentDbusSender() const;
    void stopClient(qint64 pid);

Q_SIGNALS:
    void activeHostCountChanged(int count);

#ifndef BUNDLED_HOST
private Q_SLOTS:
    void onSpawnExited(quint32 pid, quint32 exitStatus);
#endif

private:
    bool m_acceptingConnections = false;

#ifdef BUNDLED_HOST
    QMap<QProcess *, ClientInfo> m_activeClients;
#else
    QMap<quint32, ClientInfo> m_activeClients;
#endif
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
