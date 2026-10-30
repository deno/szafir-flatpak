#pragma once

#include <QObject>
#include <QString>

// Sends freedesktop desktop notifications for smart card hotplug events.
// Calls org.freedesktop.Notifications directly (the same channel that
// KStatusNotifierItem::showMessage uses) so notifications are independent of
// the tray item's lifetime and keep working before the tray exists.
class SmartCardNotifier : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled
                   NOTIFY notificationsEnabledChanged)

public:
    explicit SmartCardNotifier(QObject *parent = nullptr);

    bool notificationsEnabled() const { return m_notificationsEnabled; }
    void setNotificationsEnabled(bool enabled);

public Q_SLOTS:
    void cardInserted(const QString &readerName);
    void cardRemoved(const QString &readerName);

Q_SIGNALS:
    void notificationsEnabledChanged();

private:
    void loadSettings();
    void saveSettings();
    void notify(const QString &summary, const QString &body);

    bool m_notificationsEnabled = true;
};
