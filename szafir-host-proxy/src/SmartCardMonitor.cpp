#include "SmartCardMonitor.h"

#include <QVariantMap>

#include <cstring>

namespace {

QVariantList mockReaders()
{
    return {
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Cherry SmartTerminal XX44 [Smart Terminal xx44] 00 00")},
                    {QStringLiteral("present"), true}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Gemalto PC Twin Reader 01 00")},
                    {QStringLiteral("present"), false}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("ACS ACR122U PICC Interface 02 00")},
                    {QStringLiteral("present"), true}},
    };
}

} // namespace

SmartCardMonitor::SmartCardMonitor(Mode mode, QObject *parent)
    : QObject(parent)
{
    if (mode == Mode::Empty) {
        updateState(true, false, {});
        return;
    }
    if (mode == Mode::Mock) {
        const QVariantList readers = mockReaders();
        updateState(true, true, readers);
        return;
    }

    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &SmartCardMonitor::poll);
    m_timer.start();
    poll();
}

SmartCardMonitor::~SmartCardMonitor()
{
    releaseContext();
}

bool SmartCardMonitor::ensureContext()
{
    if (m_context)
        return true;
    const LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &m_context);
    if (rv != SCARD_S_SUCCESS) {
        m_context = 0;
        return false;
    }
    return true;
}

void SmartCardMonitor::releaseContext()
{
    if (m_context) {
        SCardReleaseContext(m_context);
        m_context = 0;
    }
}

void SmartCardMonitor::poll()
{
    if (!ensureContext()) {
        updateState(false, false, {});
        return;
    }

    DWORD len = 0;
    LONG rv = SCardListReaders(m_context, nullptr, nullptr, &len);
    if (rv == SCARD_E_NO_READERS_AVAILABLE) {
        updateState(true, false, {});
        return;
    }
    if (rv != SCARD_S_SUCCESS) {
        releaseContext();
        updateState(false, false, {});
        return;
    }

    QByteArray buf(static_cast<int>(len), '\0');
    rv = SCardListReaders(m_context, nullptr, buf.data(), &len);
    if (rv != SCARD_S_SUCCESS) {
        releaseContext();
        updateState(false, false, {});
        return;
    }

    // Parse the multi-string (NUL-separated, double-NUL terminated).
    // Keep QByteArrays alive so szReader pointers remain valid.
    QList<QByteArray> names;
    for (const char *p = buf.constData(); *p; p += std::strlen(p) + 1)
        names.append(QByteArray(p));

    if (names.isEmpty()) {
        updateState(true, false, {});
        return;
    }

    QList<SCARD_READERSTATE> states(names.size());
    std::memset(states.data(), 0, static_cast<size_t>(states.size()) * sizeof(SCARD_READERSTATE));
    for (int i = 0; i < names.size(); ++i) {
        states[i].szReader = names[i].constData();
        states[i].dwCurrentState = SCARD_STATE_UNAWARE;
    }

    rv = SCardGetStatusChange(m_context, 0, states.data(), static_cast<DWORD>(states.size()));
    if (rv == SCARD_E_NO_SERVICE || rv == SCARD_E_SERVICE_STOPPED) {
        releaseContext();
        updateState(false, false, {});
        return;
    }
    if (rv != SCARD_S_SUCCESS && rv != SCARD_E_TIMEOUT) {
        // Transient error; keep previous state.
        return;
    }

    QVariantList readers;
    bool anyPresent = false;
    for (int i = 0; i < names.size(); ++i) {
        const bool present = (states[i].dwEventState & SCARD_STATE_PRESENT) != 0;
        anyPresent |= present;
        readers.append(QVariantMap{
            {QStringLiteral("name"), QString::fromUtf8(names[i])},
            {QStringLiteral("present"), present},
        });
    }

    updateState(true, anyPresent, readers);
}

void SmartCardMonitor::updateState(bool available, bool cardPresent, const QVariantList &readers)
{
    if (m_available == available && m_cardPresent == cardPresent && m_readers == readers)
        return;
    m_available = available;
    m_cardPresent = cardPresent;
    m_readers = readers;
    emit statusChanged();
}
