#include "SmartCardNotifier.h"

#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include "config.h"

namespace {
// Negative timeouts ask the notification server to use its default display
// duration (org.freedesktop.Notifications spec).
constexpr int kExpireTimeoutDefault = -1;
}

SmartCardNotifier::SmartCardNotifier(QObject *parent)
    : QObject(parent)
{
    loadSettings();
}

void SmartCardNotifier::setNotificationsEnabled(bool enabled)
{
    if (m_notificationsEnabled == enabled)
        return;

    m_notificationsEnabled = enabled;
    saveSettings();
    Q_EMIT notificationsEnabledChanged();
}

void SmartCardNotifier::cardInserted(const QString &readerName)
{
    notify(i18n("Smart card inserted"), i18n("Reader: %1", readerName));
}

void SmartCardNotifier::cardRemoved(const QString &readerName)
{
    notify(i18n("Smart card removed"), i18n("Reader: %1", readerName));
}

void SmartCardNotifier::loadSettings()
{
    const KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Notifications"));
    m_notificationsEnabled = group.readEntry(QStringLiteral("Enabled"), true);
}

void SmartCardNotifier::saveSettings()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Notifications"));
    group.writeEntry(QStringLiteral("Enabled"), m_notificationsEnabled);
    group.sync();
}

void SmartCardNotifier::notify(const QString &summary, const QString &body)
{
    if (!m_notificationsEnabled)
        return;
    // org.freedesktop.Notifications.Notify argument order: app_name,
    // replaces_id, app_icon, summary, body, actions, hints, expire_timeout.
    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral(APP_ID));

    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));
    message.setArguments({
        QGuiApplication::applicationDisplayName(),
        uint(0),
        QStringLiteral(APP_ID),
        summary,
        body,
        QStringList(),
        hints,
        kExpireTimeoutDefault,
    });

    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<uint> reply = *w;
        if (reply.isError())
            qWarning().noquote() << "Smart card notification failed:" << reply.error().message();
    });
}
