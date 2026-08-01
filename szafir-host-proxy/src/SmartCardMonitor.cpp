#include "SmartCardMonitor.h"

#include <QByteArray>
#include <QVariantMap>

#include <reader.h>

#include <cstring>

namespace {

QVariantMap readerEntry(const QString &name, bool present)
{
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("present"), present},
        {QStringLiteral("atr"), QString()},
        {QStringLiteral("protocol"), QString()},
        {QStringLiteral("readerVendor"), QString()},
        {QStringLiteral("readerModel"), QString()},
        {QStringLiteral("readerSerial"), QString()},
        {QStringLiteral("readerVersion"), QString()},
        {QStringLiteral("providerName"), QString()},
        {QStringLiteral("cardType"), QString()},
        {QStringLiteral("certificateSubject"), QString()},
        {QStringLiteral("certificateIssuer"), QString()},
        {QStringLiteral("certificateSerial"), QString()},
        {QStringLiteral("certificateValidFrom"), QString()},
        {QStringLiteral("certificateValidUntil"), QString()},
    };
}

QString atrText(const BYTE *atr, DWORD length)
{
    if (!atr || length == 0)
        return {};
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(atr), static_cast<qsizetype>(length)).toHex(' ')).toUpper();
}

QString protocolText(DWORD protocol)
{
    switch (protocol) {
    case SCARD_PROTOCOL_T0:
        return QStringLiteral("T=0");
    case SCARD_PROTOCOL_T1:
        return QStringLiteral("T=1");
    case SCARD_PROTOCOL_RAW:
        return QStringLiteral("RAW");
    default:
        return {};
    }
}

QByteArray readerAttribute(SCARDHANDLE card, DWORD attribute)
{
    DWORD length = 0;
    LONG rv = SCardGetAttrib(card, attribute, nullptr, &length);
    if (rv != SCARD_S_SUCCESS && rv != SCARD_E_INSUFFICIENT_BUFFER)
        return {};
    if (length == 0)
        return {};

    QByteArray value;
    value.resize(static_cast<qsizetype>(length));
    rv = SCardGetAttrib(card, attribute, reinterpret_cast<LPBYTE>(value.data()), &length);
    if (rv != SCARD_S_SUCCESS)
        return {};
    value.resize(static_cast<qsizetype>(length));
    return value;
}

QString readerAttributeText(SCARDHANDLE card, DWORD attribute)
{
    QByteArray value = readerAttribute(card, attribute);
    const qsizetype nul = value.indexOf('\0');
    if (nul >= 0)
        value.truncate(nul);
    return QString::fromUtf8(value).trimmed();
}

QString readerAttributeVersion(SCARDHANDLE card)
{
    const QByteArray value = readerAttribute(card, SCARD_ATTR_VENDOR_IFD_VERSION);
    if (value.size() == static_cast<qsizetype>(sizeof(DWORD))) {
        DWORD version = 0;
        std::memcpy(&version, value.constData(), sizeof(version));
        return QStringLiteral("%1.%2.%3")
            .arg((version >> 24) & 0xff)
            .arg((version >> 16) & 0xff)
            .arg(version & 0xffff);
    }
    return QString::fromUtf8(value).trimmed();
}

QString readerAttributeHex(SCARDHANDLE card, DWORD attribute)
{
    const QByteArray value = readerAttribute(card, attribute);
    if (value.isEmpty())
        return {};
    return QString::fromLatin1(value.toHex(' ')).toUpper();
}

bool readerAttributeDword(SCARDHANDLE card, DWORD attribute, DWORD &value)
{
    const QByteArray bytes = readerAttribute(card, attribute);
    if (bytes.size() != static_cast<qsizetype>(sizeof(DWORD)))
        return false;
    std::memcpy(&value, bytes.constData(), sizeof(value));
    return true;
}

void setIfEmpty(QVariantMap &reader, const QString &key, const QString &value)
{
    if (reader.value(key).toString().isEmpty() && !value.isEmpty())
        reader.insert(key, value);
}

void enrichReaderFromHandle(QVariantMap &reader, SCARDHANDLE card, DWORD activeProtocol, bool cardConnection)
{
    setIfEmpty(reader, QStringLiteral("protocol"), protocolText(activeProtocol));
    setIfEmpty(reader, QStringLiteral("readerVendor"), readerAttributeText(card, SCARD_ATTR_VENDOR_NAME));
    setIfEmpty(reader, QStringLiteral("readerModel"), readerAttributeText(card, SCARD_ATTR_VENDOR_IFD_TYPE));
    setIfEmpty(reader, QStringLiteral("readerSerial"), readerAttributeText(card, SCARD_ATTR_VENDOR_IFD_SERIAL_NO));
    setIfEmpty(reader, QStringLiteral("readerVersion"), readerAttributeVersion(card));
    setIfEmpty(reader, QStringLiteral("atr"), readerAttributeHex(card, SCARD_ATTR_ATR_STRING));

    DWORD attributeProtocol = 0;
    if (readerAttributeDword(card, SCARD_ATTR_CURRENT_PROTOCOL_TYPE, attributeProtocol))
        setIfEmpty(reader, QStringLiteral("protocol"), protocolText(attributeProtocol));

    const QByteArray cardType = readerAttribute(card, SCARD_ATTR_ICC_TYPE_PER_ATR);
    if (cardType.size() == 1) {
        const auto type = static_cast<unsigned char>(cardType.constData()[0]);
        setIfEmpty(reader,
                   QStringLiteral("cardType"),
                   QStringLiteral("PC/SC type 0x%1").arg(type, 2, 16, QLatin1Char('0')).toUpper());
    }

    if (!cardConnection)
        return;

    DWORD state = 0;
    DWORD statusProtocol = 0;
    QByteArray statusAtr(MAX_ATR_SIZE, '\0');
    DWORD statusAtrLength = static_cast<DWORD>(statusAtr.size());
    if (SCardStatus(card,
                    nullptr,
                    nullptr,
                    &state,
                    &statusProtocol,
                    reinterpret_cast<LPBYTE>(statusAtr.data()),
                    &statusAtrLength)
        == SCARD_S_SUCCESS) {
        if (statusAtrLength > 0)
            setIfEmpty(reader,
                       QStringLiteral("atr"),
                       atrText(reinterpret_cast<const BYTE *>(statusAtr.constData()), statusAtrLength));
        setIfEmpty(reader, QStringLiteral("protocol"), protocolText(statusProtocol));
    }
}

QVariantList mockReaders()
{
    QVariantMap cherry = readerEntry(QStringLiteral("Cherry SmartTerminal XX44 [Smart Terminal xx44] 00 00"), true);
    cherry.insert(QStringLiteral("atr"), QStringLiteral("3B 7F 18 00 00 81 31 FE 45 4A 43 4F 50 32 31 56 31 00"));
    cherry.insert(QStringLiteral("protocol"), QStringLiteral("T=1"));
    cherry.insert(QStringLiteral("readerVendor"), QStringLiteral("CHERRY"));
    cherry.insert(QStringLiteral("readerModel"), QStringLiteral("SmartTerminal XX44"));
    cherry.insert(QStringLiteral("readerSerial"), QStringLiteral("MOCK-CHERRY-XX44-0001"));
    cherry.insert(QStringLiteral("readerVersion"), QStringLiteral("2.1.0"));
    cherry.insert(QStringLiteral("providerName"), QStringLiteral("Mock Graphite provider"));
    cherry.insert(QStringLiteral("cardType"), QStringLiteral("Mock signing card"));
    cherry.insert(QStringLiteral("certificateSubject"), QStringLiteral("CN=Mock signer"));
    cherry.insert(QStringLiteral("certificateIssuer"), QStringLiteral("CN=Mock root CA"));
    cherry.insert(QStringLiteral("certificateSerial"), QStringLiteral("MOCK-CERT-0001"));
    cherry.insert(QStringLiteral("certificateValidFrom"), QStringLiteral("2026-01-01"));
    cherry.insert(QStringLiteral("certificateValidUntil"), QStringLiteral("2028-01-01"));

    QVariantMap gemalto = readerEntry(QStringLiteral("Gemalto PC Twin Reader 01 00"), false);
    gemalto.insert(QStringLiteral("readerVendor"), QStringLiteral("Gemalto"));
    gemalto.insert(QStringLiteral("readerModel"), QStringLiteral("PC Twin Reader"));
    gemalto.insert(QStringLiteral("readerSerial"), QStringLiteral("MOCK-GEMALTO-0100"));
    gemalto.insert(QStringLiteral("readerVersion"), QStringLiteral("1.0.0"));

    QVariantMap acs = readerEntry(QStringLiteral("ACS ACR122U PICC Interface 02 00"), true);
    acs.insert(QStringLiteral("atr"), QStringLiteral("3B 8F 80 01 80 4F 0C A0 00 00 03 06 03 00 01 00 00 00 00 6A"));
    acs.insert(QStringLiteral("protocol"), QStringLiteral("T=1"));
    acs.insert(QStringLiteral("readerVendor"), QStringLiteral("Advanced Card Systems"));
    acs.insert(QStringLiteral("readerModel"), QStringLiteral("ACR122U PICC Interface"));
    acs.insert(QStringLiteral("readerSerial"), QStringLiteral("MOCK-ACS-ACR122U-0200"));
    acs.insert(QStringLiteral("readerVersion"), QStringLiteral("2.0.5"));
    acs.insert(QStringLiteral("providerName"), QStringLiteral("Mock contactless provider"));
    acs.insert(QStringLiteral("cardType"), QStringLiteral("Mock contactless signing card"));
    acs.insert(QStringLiteral("certificateSubject"), QStringLiteral("CN=Mock contactless signer"));
    acs.insert(QStringLiteral("certificateIssuer"), QStringLiteral("CN=Mock root CA"));
    acs.insert(QStringLiteral("certificateSerial"), QStringLiteral("MOCK-CERT-0002"));
    acs.insert(QStringLiteral("certificateValidFrom"), QStringLiteral("2026-02-01"));
    acs.insert(QStringLiteral("certificateValidUntil"), QStringLiteral("2028-02-01"));

    return {cherry, gemalto, acs};
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

        QVariantMap reader = readerEntry(QString::fromUtf8(names[i]), present);
        reader.insert(QStringLiteral("atr"), atrText(states[i].rgbAtr, states[i].cbAtr));

        if (present) {
            SCARDHANDLE card = 0;
            DWORD protocol = 0;
            LONG connectRv = SCardConnect(m_context,
                                          names[i].constData(),
                                          SCARD_SHARE_SHARED,
                                          SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1 | SCARD_PROTOCOL_RAW,
                                          &card,
                                          &protocol);
            const bool cardConnection = connectRv == SCARD_S_SUCCESS;

            if (!cardConnection) {
                card = 0;
                protocol = 0;
                connectRv = SCardConnect(m_context,
                                         names[i].constData(),
                                         SCARD_SHARE_DIRECT,
                                         0,
                                         &card,
                                         &protocol);
            }

            if (connectRv == SCARD_S_SUCCESS) {
                enrichReaderFromHandle(reader, card, protocol, cardConnection);
                SCardDisconnect(card, SCARD_LEAVE_CARD);
            }
        }

        readers.append(reader);
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
