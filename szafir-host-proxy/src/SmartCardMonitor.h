#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <winscard.h>

class SmartCardMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(bool cardPresent READ cardPresent NOTIFY statusChanged)
    Q_PROPERTY(QVariantList readers READ readers NOTIFY statusChanged)

public:
    enum class Mode { Live, Empty, Mock };

    explicit SmartCardMonitor(Mode mode = Mode::Live, QObject *parent = nullptr);
    ~SmartCardMonitor() override;

    bool available() const { return m_available; }
    bool cardPresent() const { return m_cardPresent; }
    QVariantList readers() const { return m_readers; }

Q_SIGNALS:
    void statusChanged();

private:
    bool ensureContext();
    void releaseContext();
    void poll();
    void updateState(bool available, bool cardPresent, const QVariantList &readers);

    SCARDCONTEXT m_context = 0;
    QTimer m_timer;
    bool m_available = false;
    bool m_cardPresent = false;
    QVariantList m_readers;
};
