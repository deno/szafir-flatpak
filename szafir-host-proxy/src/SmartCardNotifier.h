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

public:
    explicit SmartCardNotifier(QObject *parent = nullptr);

public Q_SLOTS:
    void cardInserted(const QString &readerName);
    void cardRemoved(const QString &readerName);

private:
    void notify(const QString &summary, const QString &body);
};
