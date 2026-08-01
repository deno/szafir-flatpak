#include "SmartCardMonitor.h"

#include "ComponentDownloader.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QVariantMap>

#include <reader.h>

#include <cstring>
#include <cstdio>

namespace {

QVariantMap readerEntry(const QString &name, bool present)
{
    QVariantMap entry {
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
        {QStringLiteral("detailsState"), QStringLiteral("not-requested")},
        {QStringLiteral("detailsError"), QString()},
    };
    entry.insert(QStringLiteral("provider"), QVariantMap());
    entry.insert(QStringLiteral("token"), QVariantMap());
    entry.insert(QStringLiteral("certificates"), QVariantList());
    return entry;
}

QString normalizedReaderName(QString value)
{
    value = value.toLower();
    value.remove(QRegularExpression(QStringLiteral("[^a-z0-9]+")));
    return value;
}

void applyDetails(QVariantMap &reader, const QVariantMap &details)
{
    for (const QString &key : {
             QStringLiteral("detailsState"),
             QStringLiteral("detailsError"),
             QStringLiteral("provider"),
             QStringLiteral("token"),
             QStringLiteral("certificates"),
             QStringLiteral("providerName"),
             QStringLiteral("cardType"),
             QStringLiteral("certificateSubject"),
             QStringLiteral("certificateIssuer"),
             QStringLiteral("certificateSerial"),
             QStringLiteral("certificateValidFrom"),
             QStringLiteral("certificateValidUntil")}) {
        if (details.contains(key))
            reader.insert(key, details.value(key));
    }

    const QVariantMap provider = reader.value(QStringLiteral("provider")).toMap();
    if (reader.value(QStringLiteral("providerName")).toString().isEmpty())
        reader.insert(QStringLiteral("providerName"), provider.value(QStringLiteral("name")));

    const QVariantMap token = reader.value(QStringLiteral("token")).toMap();
    if (reader.value(QStringLiteral("cardType")).toString().isEmpty()) {
        const QString model = token.value(QStringLiteral("model")).toString();
        const QString label = token.value(QStringLiteral("label")).toString();
        if (!model.isEmpty())
            reader.insert(QStringLiteral("cardType"), model);
        else if (!label.isEmpty())
            reader.insert(QStringLiteral("cardType"), label);
    }

    const QVariantList certificates = reader.value(QStringLiteral("certificates")).toList();
    if (!certificates.isEmpty()) {
        const QVariantMap certificate = certificates.constFirst().toMap();
        reader.insert(QStringLiteral("certificateSubject"), certificate.value(QStringLiteral("subject")));
        reader.insert(QStringLiteral("certificateIssuer"), certificate.value(QStringLiteral("issuer")));
        reader.insert(QStringLiteral("certificateSerial"), certificate.value(QStringLiteral("serialDecimal")));
        reader.insert(QStringLiteral("certificateValidFrom"), certificate.value(QStringLiteral("validFrom")));
        reader.insert(QStringLiteral("certificateValidUntil"), certificate.value(QStringLiteral("validUntil")));
    }
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

QString pcscReturnValue(LONG rv)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<qulonglong>(static_cast<unsigned long>(rv)), 8, 16, QLatin1Char('0'))
        .toUpper();
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
    return {};
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

    const QVariantMap provider {
        {QStringLiteral("name"), QStringLiteral("Mock Graphite provider")},
        {QStringLiteral("manufacturer"), QStringLiteral("Mock Certificate Components")},
        {QStringLiteral("description"), QStringLiteral("Synthetic PKCS#11 provider")},
        {QStringLiteral("cryptokiVersion"), QStringLiteral("2.40")},
        {QStringLiteral("libraryVersion"), QStringLiteral("2.0.5.6")},
    };
    const QVariantMap token {
        {QStringLiteral("slotId"), 1},
        {QStringLiteral("slotDescription"), QStringLiteral("Cherry SmartTerminal XX44 [Smart Terminal xx44] 00 00")},
        {QStringLiteral("label"), QStringLiteral("Mock signing card")},
        {QStringLiteral("manufacturer"), QStringLiteral("Mock Card Provider")},
        {QStringLiteral("model"), QStringLiteral("Graphite-compatible mock token")},
        {QStringLiteral("serial"), QStringLiteral("MOCK-TOKEN-0001")},
        {QStringLiteral("hardwareVersion"), QStringLiteral("1.0")},
        {QStringLiteral("firmwareVersion"), QStringLiteral("4.2")},
    };
    const QVariantMap signingCertificate {
        {QStringLiteral("objectId"), QStringLiteral("01")},
        {QStringLiteral("label"), QStringLiteral("Mock signing certificate")},
        {QStringLiteral("subject"), QStringLiteral("CN=Mock Signer, O=Example Signing Services, C=PL")},
        {QStringLiteral("issuer"), QStringLiteral("CN=Mock Root CA, O=Example Signing Services, C=PL")},
        {QStringLiteral("subjectCommonName"), QStringLiteral("Mock Signer")},
        {QStringLiteral("organizationName"), QStringLiteral("Example Signing Services")},
        {QStringLiteral("countryName"), QStringLiteral("PL")},
        {QStringLiteral("serialDecimal"), QStringLiteral("6418898269650339")},
        {QStringLiteral("serialHex"), QStringLiteral("16  DC  7A  51  22  00  13")},
        {QStringLiteral("validFrom"), QStringLiteral("2026-01-01T00:00:00Z")},
        {QStringLiteral("validUntil"), QStringLiteral("2028-01-01T00:00:00Z")},
        {QStringLiteral("publicKeyAlgorithm"), QStringLiteral("RSA")},
        {QStringLiteral("publicKeyBits"), 2048},
        {QStringLiteral("signatureAlgorithm"), QStringLiteral("RSA-SHA256")},
        {QStringLiteral("keyUsage"), QStringList {QStringLiteral("Digital signature"), QStringLiteral("Non-repudiation")}},
        {QStringLiteral("extendedKeyUsage"), QStringList {QStringLiteral("1.3.6.1.5.5.7.3.3")}},
        {QStringLiteral("fingerprintSha256"), QStringLiteral("AA:BB:CC:DD:EE:FF:00:11")},
        {QStringLiteral("fingerprintSha1"), QStringLiteral("11:22:33:44:55:66:77:88")},
        {QStringLiteral("privateKeyMatch"), QStringLiteral("yes")},
    };
    const QVariantMap authenticationCertificate {
        {QStringLiteral("objectId"), QStringLiteral("02")},
        {QStringLiteral("label"), QStringLiteral("Mock authentication certificate")},
        {QStringLiteral("subject"), QStringLiteral("CN=Mock Signer Authentication, O=Example Signing Services, C=PL")},
        {QStringLiteral("issuer"), QStringLiteral("CN=Mock Root CA, O=Example Signing Services, C=PL")},
        {QStringLiteral("serialDecimal"), QStringLiteral("6418898269650340")},
        {QStringLiteral("serialHex"), QStringLiteral("16 DC 7A 51 22 00 14")},
        {QStringLiteral("validFrom"), QStringLiteral("2026-01-01T00:00:00Z")},
        {QStringLiteral("validUntil"), QStringLiteral("2028-01-01T00:00:00Z")},
        {QStringLiteral("publicKeyAlgorithm"), QStringLiteral("RSA")},
        {QStringLiteral("publicKeyBits"), 2048},
        {QStringLiteral("signatureAlgorithm"), QStringLiteral("RSA-SHA256")},
        {QStringLiteral("keyUsage"), QStringList {QStringLiteral("Digital signature")}},
        {QStringLiteral("extendedKeyUsage"), QStringList {QStringLiteral("1.3.6.1.5.5.7.3.2")}},
        {QStringLiteral("privateKeyMatch"), QStringLiteral("yes")},
    };
    QVariantMap richToken = token;
    richToken.insert(QStringLiteral("certificates"), QVariantList {signingCertificate, authenticationCertificate});
    QVariantMap richDetails {
        {QStringLiteral("detailsState"), QStringLiteral("ready")},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("token"), richToken},
        {QStringLiteral("certificates"), QVariantList {signingCertificate, authenticationCertificate}},
        {QStringLiteral("providerName"), QStringLiteral("Mock Graphite provider")},
        {QStringLiteral("cardType"), QStringLiteral("Graphite-compatible mock token")},
    };
    applyDetails(cherry, richDetails);

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
    applyDetails(acs, {
        {QStringLiteral("detailsState"), QStringLiteral("no-certificates")},
        {QStringLiteral("provider"), QVariantMap {
            {QStringLiteral("name"), QStringLiteral("Mock contactless provider")},
            {QStringLiteral("manufacturer"), QStringLiteral("Mock Card Provider")},
        }},
        {QStringLiteral("token"), QVariantMap {
            {QStringLiteral("label"), QStringLiteral("Mock contactless token")},
            {QStringLiteral("model"), QStringLiteral("Contactless mock card")},
            {QStringLiteral("serial"), QStringLiteral("MOCK-TOKEN-0002")},
        }},
        {QStringLiteral("certificates"), QVariantList()},
        {QStringLiteral("providerName"), QStringLiteral("Mock contactless provider")},
        {QStringLiteral("cardType"), QStringLiteral("Contactless mock card")},
    });

    return {cherry, gemalto, acs};
}

} // namespace

SmartCardMonitor::SmartCardMonitor(Mode mode,
                                   ComponentDownloader *componentDownloader,
                                   QObject *parent,
                                   bool debugLogging)
    : QObject(parent)
    , m_mode(mode)
    , m_componentDownloader(componentDownloader)
    , m_debugLogging(debugLogging)
{
    m_probeTimeout.setSingleShot(true);
    connect(&m_probeTimeout, &QTimer::timeout, this, &SmartCardMonitor::probeTimedOut);
    m_probeRetryTimer.setSingleShot(true);
    connect(&m_probeRetryTimer, &QTimer::timeout, this, &SmartCardMonitor::retryProbe);

    if (m_componentDownloader) {
        connect(m_componentDownloader, &ComponentDownloader::componentChanged,
                this, &SmartCardMonitor::onProviderComponentChanged);
    }

    if (mode == Mode::Empty) {
        debugLog(QStringLiteral("started in empty mode"));
        updateState(true, false, {});
        return;
    }
    if (mode == Mode::Mock) {
        debugLog(QStringLiteral("started in mock mode with synthetic readers"));
        const QVariantList readers = mockReaders();
        updateState(true, true, readers);
        return;
    }

    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &SmartCardMonitor::poll);
    m_timer.start();
    debugLog(QStringLiteral("started in live mode"));
    poll();
}

SmartCardMonitor::~SmartCardMonitor()
{
    m_probeTimeout.stop();
    m_probeRetryTimer.stop();
    if (m_probeProcess) {
        m_probeProcess->kill();
        m_probeProcess->waitForFinished(1000);
        m_probeProcess = nullptr;
    }
    releaseContext();
}

bool SmartCardMonitor::ensureContext()
{
    if (m_context)
        return true;
    const LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &m_context);
    if (rv != SCARD_S_SUCCESS) {
        debugLog(QStringLiteral("SCardEstablishContext failed (%1)").arg(pcscReturnValue(rv)));
        m_context = 0;
        return false;
    }
    debugLog(QStringLiteral("SCardEstablishContext succeeded"));
    return true;
}

void SmartCardMonitor::releaseContext()
{
    if (m_context) {
        debugLog(QStringLiteral("releasing PC/SC context"));
        SCardReleaseContext(m_context);
        m_context = 0;
    }
}

void SmartCardMonitor::poll()
{
    if (!ensureContext()) {
        debugLog(QStringLiteral("poll: PC/SC context unavailable"));
        updateState(false, false, {});
        return;
    }

    DWORD len = 0;
    LONG rv = SCardListReaders(m_context, nullptr, nullptr, &len);
    if (rv == SCARD_E_NO_READERS_AVAILABLE) {
        debugLog(QStringLiteral("poll: no PC/SC readers available"));
        m_providerDetails.clear();
        m_activeCardKeys.clear();
        updateState(true, false, {});
        return;
    }
    if (rv != SCARD_S_SUCCESS) {
        debugLog(QStringLiteral("poll: SCardListReaders(size) failed (%1)").arg(pcscReturnValue(rv)));
        releaseContext();
        updateState(false, false, {});
        return;
    }

    QByteArray buf(static_cast<int>(len), '\0');
    rv = SCardListReaders(m_context, nullptr, buf.data(), &len);
    if (rv != SCARD_S_SUCCESS) {
        debugLog(QStringLiteral("poll: SCardListReaders(data) failed (%1)").arg(pcscReturnValue(rv)));
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
        debugLog(QStringLiteral("poll: PC/SC returned an empty reader list"));
        m_providerDetails.clear();
        m_activeCardKeys.clear();
        updateState(true, false, {});
        return;
    }

    debugLog(QStringLiteral("poll: found %1 reader(s)").arg(names.size()));

    QList<SCARD_READERSTATE> states(names.size());
    std::memset(states.data(), 0, static_cast<size_t>(states.size()) * sizeof(SCARD_READERSTATE));
    for (int i = 0; i < names.size(); ++i) {
        states[i].szReader = names[i].constData();
        states[i].dwCurrentState = SCARD_STATE_UNAWARE;
    }

    rv = SCardGetStatusChange(m_context, 0, states.data(), static_cast<DWORD>(states.size()));
    if (rv == SCARD_E_NO_SERVICE || rv == SCARD_E_SERVICE_STOPPED) {
        debugLog(QStringLiteral("poll: PC/SC service stopped (%1)").arg(pcscReturnValue(rv)));
        releaseContext();
        updateState(false, false, {});
        return;
    }
    if (rv != SCARD_S_SUCCESS && rv != SCARD_E_TIMEOUT) {
        debugLog(QStringLiteral("poll: SCardGetStatusChange failed (%1)").arg(pcscReturnValue(rv)));
        // Transient error; keep previous state.
        return;
    }

    QVariantList readers;
    bool anyPresent = false;
    for (int i = 0; i < names.size(); ++i) {
        const bool present = (states[i].dwEventState & SCARD_STATE_PRESENT) != 0;
        anyPresent |= present;

        const QString readerName = QString::fromUtf8(names[i]);
        QVariantMap reader = readerEntry(readerName, present);
        reader.insert(QStringLiteral("atr"), atrText(states[i].rgbAtr, states[i].cbAtr));
        debugLog(QStringLiteral("reader \"%1\": present=%2 ATR=%3")
            .arg(readerName)
            .arg(present ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(reader.value(QStringLiteral("atr")).toString()));

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
            debugLog(QStringLiteral("reader \"%1\": SCardConnect(shared) -> %2 protocol=%3")
                .arg(readerName)
                .arg(pcscReturnValue(connectRv))
                .arg(protocolText(protocol)));

            if (!cardConnection) {
                card = 0;
                protocol = 0;
                connectRv = SCardConnect(m_context,
                                         names[i].constData(),
                                         SCARD_SHARE_DIRECT,
                                         0,
                                         &card,
                                         &protocol);
                debugLog(QStringLiteral("reader \"%1\": SCardConnect(direct) -> %2")
                    .arg(readerName).arg(pcscReturnValue(connectRv)));
            }

            if (connectRv == SCARD_S_SUCCESS) {
                enrichReaderFromHandle(reader, card, protocol, cardConnection);
                SCardDisconnect(card, SCARD_LEAVE_CARD);
            }
        }

        const QString key = present ? cardKey(reader) : QString();
        const QString previousKey = m_activeCardKeys.value(readerName);
        if (previousKey != key) {
            if (!previousKey.isEmpty())
                m_providerDetails.remove(previousKey);
            m_readerGenerations[readerName] = m_readerGenerations.value(readerName) + 1;
        }
        m_activeCardKeys.insert(readerName, present ? key : QString());
        if (!present)
            m_providerDetails.remove(key);
        else if (m_providerDetails.contains(key))
            applyDetails(reader, m_providerDetails.value(key));

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

QVariantMap SmartCardMonitor::currentReader(const QString &readerName) const
{
    for (const QVariant &value : m_readers) {
        const QVariantMap reader = value.toMap();
        if (reader.value(QStringLiteral("name")).toString() == readerName)
            return reader;
    }
    return {};
}

QString SmartCardMonitor::cardKey(const QVariantMap &reader) const
{
    return reader.value(QStringLiteral("name")).toString()
        + QLatin1Char('|')
        + reader.value(QStringLiteral("atr")).toString();
}

int SmartCardMonitor::readerGeneration(const QString &readerName, const QString &key) const
{
    Q_UNUSED(key);
    return m_readerGenerations.value(readerName, 0);
}

void SmartCardMonitor::updateReaderDetails(const QString &readerName, const QVariantMap &details)
{
    const QVariantMap reader = currentReader(readerName);
    if (reader.isEmpty())
        return;

    m_providerDetails.insert(cardKey(reader), details);

    QVariantList updated = m_readers;
    for (int index = 0; index < updated.size(); ++index) {
        QVariantMap candidate = updated[index].toMap();
        if (candidate.value(QStringLiteral("name")).toString() != readerName)
            continue;
        applyDetails(candidate, details);
        updated[index] = candidate;
        updateState(m_available, m_cardPresent, updated);
        return;
    }
}

void SmartCardMonitor::requestDetails(const QString &readerName)
{
    debugLog(QStringLiteral("requestDetails(\"%1\")").arg(readerName));
    const QVariantMap reader = currentReader(readerName);
    if (reader.isEmpty() || !reader.value(QStringLiteral("present")).toBool()) {
        debugLog(QStringLiteral("requestDetails: reader is missing or card is not present"));
        return;
    }

    const QString key = cardKey(reader);
    const QVariantMap cached = m_providerDetails.value(key);
    const QString cachedState = cached.value(QStringLiteral("detailsState")).toString();
    if (!cachedState.isEmpty() && cachedState != QLatin1String("not-requested")) {
        debugLog(QStringLiteral("requestDetails: using cached state \"%1\"").arg(cachedState));
        return;
    }
    if (m_probeProcess && m_probeReaderName == readerName && m_probeCardKey == key) {
        debugLog(QStringLiteral("requestDetails: probe already running"));
        return;
    }

    if (m_mode == Mode::Mock)
        return;

    QVariantMap loading {
        {QStringLiteral("detailsState"), QStringLiteral("loading")},
        {QStringLiteral("detailsError"), QString()},
    };

    if (!m_componentDownloader) {
        debugLog(QStringLiteral("requestDetails: component downloader is unavailable"));
        loading.insert(QStringLiteral("detailsState"), QStringLiteral("provider-missing"));
        loading.insert(QStringLiteral("detailsError"), QStringLiteral("Graphite provider is not installed"));
        updateReaderDetails(readerName, loading);
        return;
    }

    const std::filesystem::path providerPath =
        m_componentDownloader->verifiedComponentPath(QStringLiteral("libccgraphite"));
    if (providerPath.empty()) {
        debugLog(QStringLiteral("requestDetails: verified libCCGraphite component is unavailable"));
        loading.insert(QStringLiteral("detailsState"), QStringLiteral("provider-missing"));
        loading.insert(QStringLiteral("detailsError"), QStringLiteral("Graphite provider is not installed"));
        updateReaderDetails(readerName, loading);
        return;
    }

    updateReaderDetails(readerName, loading);
    m_probeRetryCount = 0;
    debugLog(QStringLiteral("requestDetails: starting probe for card key \"%1\", generation=%2, provider=%3")
        .arg(key).arg(readerGeneration(readerName, key)).arg(QString::fromStdString(providerPath.string())));
    startProbe(readerName, key, readerGeneration(readerName, key), providerPath);
}

void SmartCardMonitor::retryDetails(const QString &readerName)
{
    debugLog(QStringLiteral("retryDetails(\"%1\")").arg(readerName));
    const QVariantMap reader = currentReader(readerName);
    if (reader.isEmpty())
        return;
    m_probeRetryTimer.stop();
    m_probeRetryCount = 0;
    m_providerDetails.remove(cardKey(reader));
    requestDetails(readerName);
}

void SmartCardMonitor::startProbe(const QString &readerName,
                                  const QString &key,
                                  int generation,
                                  const std::filesystem::path &providerPath)
{
    if (m_probeProcess) {
        disconnect(m_probeProcess, nullptr, this, nullptr);
        m_probeProcess->kill();
        m_probeProcess->deleteLater();
        m_probeProcess = nullptr;
    }

    m_probeReaderName = readerName;
    m_probeCardKey = key;
    m_probeProviderPath = providerPath;
    m_probeGeneration = generation;
    m_probeTimedOut = false;

    auto *process = new QProcess(this);
    m_probeProcess = process;
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_probeProcess == process)
                    finishProbe(exitCode, exitStatus);
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
                if (m_probeProcess == process && error == QProcess::FailedToStart)
                    failProbe(QStringLiteral("probe-start-failed"), process->errorString());
            });

    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("szafir-pkcs11-probe"));
    QStringList arguments;
    if (m_debugLogging)
        arguments.append(QStringLiteral("--debug"));
    arguments.append(QString::fromStdString(providerPath.string()));
    process->setProcessChannelMode(QProcess::SeparateChannels);
    debugLog(QStringLiteral("starting helper \"%1\" with arguments: %2")
        .arg(helperPath).arg(arguments.join(QLatin1Char(' '))));
    process->start(helperPath, arguments);
    m_probeTimeout.start(15000);
}

void SmartCardMonitor::probeTimedOut()
{
    if (!m_probeProcess)
        return;
    debugLog(QStringLiteral("provider probe timed out; terminating helper"));
    m_probeTimedOut = true;
    m_probeProcess->kill();
}

void SmartCardMonitor::failProbe(const QString &state, const QString &error)
{
    debugLog(QStringLiteral("provider probe failed: state=%1 error=\"%2\"").arg(state, error));
    const QString readerName = m_probeReaderName;
    const QString key = m_probeCardKey;
    m_probeTimeout.stop();
    m_probeRetryTimer.stop();
    if (m_probeProcess) {
        disconnect(m_probeProcess, nullptr, this, nullptr);
        m_probeProcess->kill();
        m_probeProcess->deleteLater();
        m_probeProcess = nullptr;
    }
    m_probeReaderName.clear();
    m_probeCardKey.clear();
    m_probeProviderPath.clear();
    m_probeGeneration = 0;
    m_probeRetryCount = 0;
    m_probeTimedOut = false;

    QVariantMap details {
        {QStringLiteral("detailsState"), state},
        {QStringLiteral("detailsError"), error},
    };
    const QVariantMap reader = currentReader(readerName);
    if (!reader.isEmpty() && cardKey(reader) == key)
        updateReaderDetails(readerName, details);
}

void SmartCardMonitor::finishProbe(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess *process = m_probeProcess;
    if (!process)
        return;

    const QByteArray output = process->readAllStandardOutput();
    const QByteArray errorOutput = process->readAllStandardError();
    const QString readerName = m_probeReaderName;
    const QString key = m_probeCardKey;
    const std::filesystem::path providerPath = m_probeProviderPath;
    const int generation = m_probeGeneration;
    const bool timedOut = m_probeTimedOut;
    m_probeTimeout.stop();
    m_probeProcess = nullptr;
    m_probeReaderName.clear();
    m_probeCardKey.clear();
    m_probeProviderPath.clear();
    m_probeGeneration = 0;
    m_probeTimedOut = false;
    process->deleteLater();

    debugLog(QStringLiteral("helper finished: exitCode=%1 exitStatus=%2 stdoutBytes=%3 stderrBytes=%4")
        .arg(exitCode)
        .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed"))
        .arg(output.size())
        .arg(errorOutput.size()));
    if (!errorOutput.trimmed().isEmpty())
        debugLog(QStringLiteral("helper stderr:\n%1").arg(QString::fromLocal8Bit(errorOutput).trimmed()));

    const auto applyFailure = [this, &readerName, &key](const QString &state, const QString &error) {
        const QVariantMap current = currentReader(readerName);
        if (!current.isEmpty() && cardKey(current) == key)
            updateReaderDetails(readerName, {
                {QStringLiteral("detailsState"), state},
                {QStringLiteral("detailsError"), error},
            });
    };

    const auto clearRetryState = [this]() {
        m_probeRetryTimer.stop();
        m_probeProviderPath.clear();
        m_probeRetryCount = 0;
    };

    if (timedOut) {
        clearRetryState();
        applyFailure(QStringLiteral("timed-out"), QStringLiteral("Provider probe timed out"));
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        clearRetryState();
        applyFailure(QStringLiteral("probe-failed"), QStringLiteral("Provider probe failed"));
        return;
    }
    if (output.size() > 8 * 1024 * 1024) {
        clearRetryState();
        applyFailure(QStringLiteral("probe-failed"), QStringLiteral("Provider response is too large"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        clearRetryState();
        applyFailure(QStringLiteral("probe-failed"), QStringLiteral("Invalid provider response"));
        return;
    }

    const QVariantMap reader = currentReader(readerName);
    if (reader.isEmpty() || !reader.value(QStringLiteral("present")).toBool()
        || cardKey(reader) != key || readerGeneration(readerName, key) != generation) {
        clearRetryState();
        return;
    }

    const QJsonObject root = document.object();
    debugLog(QStringLiteral("provider result: ok=%1 provider=\"%2\" description=\"%3\" detailsState=%4 tokenCount=%5")
        .arg(root.value(QStringLiteral("ok")).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(root.value(QStringLiteral("providerName")).toString())
        .arg(root.value(QStringLiteral("providerDescription")).toString())
        .arg(root.value(QStringLiteral("detailsState")).toString())
        .arg(root.value(QStringLiteral("tokens")).toArray().size()));
    if (!root.value(QStringLiteral("ok")).toBool()) {
        clearRetryState();
        const QString code = root.value(QStringLiteral("errorCode")).toString();
        const QString state = code.isEmpty() ? QStringLiteral("probe-failed") : code;
        updateReaderDetails(readerName, {
            {QStringLiteral("detailsState"), state},
            {QStringLiteral("detailsError"), root.value(QStringLiteral("error")).toString()},
        });
        return;
    }

    const QJsonArray tokenArray = root.value(QStringLiteral("tokens")).toArray();
    QList<QJsonObject> tokens;
    for (const QJsonValue &value : tokenArray) {
        if (value.isObject())
            tokens.append(value.toObject());
    }
    for (int index = 0; index < tokens.size(); ++index) {
        const QJsonObject token = tokens.at(index);
        debugLog(QStringLiteral("token[%1]: slot=%2 slotDescription=\"%3\" label=\"%4\" state=%5 certificates=%6")
            .arg(index)
            .arg(token.value(QStringLiteral("slotId")).toVariant().toLongLong())
            .arg(token.value(QStringLiteral("slotDescription")).toString())
            .arg(token.value(QStringLiteral("label")).toString())
            .arg(token.value(QStringLiteral("detailsState")).toString())
            .arg(token.value(QStringLiteral("certificates")).toArray().size()));
    }

    // A newly inserted card can be visible through PC/SC before Graphite's
    // own device-monitor thread has published its PKCS#11 slots. Retry the
    // whole helper a couple of times so this transient empty result does not
    // become a sticky "no token" state in the details page.
    if (tokens.isEmpty() && m_probeRetryCount < 2 && !providerPath.empty()) {
        ++m_probeRetryCount;
        debugLog(QStringLiteral("provider returned no tokens; scheduling retry %1/2").arg(m_probeRetryCount));
        m_probeReaderName = readerName;
        m_probeCardKey = key;
        m_probeProviderPath = providerPath;
        m_probeGeneration = generation;
        updateReaderDetails(readerName, {
            {QStringLiteral("detailsState"), QStringLiteral("loading")},
            {QStringLiteral("detailsError"), QString()},
        });
        m_probeRetryTimer.start(750);
        return;
    }

    const QString normalizedReader = normalizedReaderName(readerName);
    QList<QJsonObject> matchingTokens;
    for (const QJsonObject &token : tokens) {
        if (normalizedReader == normalizedReaderName(token.value(QStringLiteral("slotDescription")).toString()))
            matchingTokens.append(token);
    }

    QJsonObject selected;
    debugLog(QStringLiteral("matching %1 token(s) to reader \"%2\"")
        .arg(matchingTokens.size()).arg(readerName));
    if (matchingTokens.size() == 1) {
        selected = matchingTokens.constFirst();
    } else if (matchingTokens.size() > 1) {
        // Some Graphite configurations expose multiple PKCS#11 tokens for
        // one physical reader. Prefer the unique token that contains a
        // certificate; an empty companion token is not useful in the UI.
        QList<QJsonObject> certificateTokens;
        for (const QJsonObject &token : matchingTokens) {
            if (!token.value(QStringLiteral("certificates")).toArray().isEmpty())
                certificateTokens.append(token);
        }
        if (certificateTokens.size() == 1) {
            selected = certificateTokens.constFirst();
        } else {
            QList<QJsonObject> readyTokens;
            for (const QJsonObject &token : matchingTokens) {
                if (token.value(QStringLiteral("detailsState")).toString() == QStringLiteral("ready"))
                    readyTokens.append(token);
            }
            if (certificateTokens.size() > 1 || readyTokens.size() != 1) {
                debugLog(QStringLiteral("token selection is ambiguous: certificateTokens=%1 readyTokens=%2")
                    .arg(certificateTokens.size()).arg(readyTokens.size()));
                updateReaderDetails(readerName, {
                    {QStringLiteral("detailsState"), QStringLiteral("ambiguous-token")},
                    {QStringLiteral("detailsError"), QStringLiteral("Several tokens match this reader")},
                });
                return;
            }
            selected = readyTokens.constFirst();
        }
    }
    if (selected.isEmpty() && tokens.size() == 1) {
        int presentCount = 0;
        for (const QVariant &value : m_readers) {
            if (value.toMap().value(QStringLiteral("present")).toBool())
                ++presentCount;
        }
        if (presentCount == 1)
            selected = tokens.constFirst();
    }

    if (selected.isEmpty()) {
        debugLog(QStringLiteral("no token could be selected; final state=%1")
            .arg(tokens.isEmpty() ? QStringLiteral("no-token") : QStringLiteral("ambiguous-token")));
        clearRetryState();
        updateReaderDetails(readerName, {
            {QStringLiteral("detailsState"), tokens.isEmpty()
                 ? QStringLiteral("no-token") : QStringLiteral("ambiguous-token")},
            {QStringLiteral("detailsError"), tokens.isEmpty()
                 ? QStringLiteral("No PKCS#11 token was found")
                 : QStringLiteral("Could not associate a PKCS#11 token with this reader")},
        });
        return;
    }

    QVariantMap provider {
        {QStringLiteral("name"), QStringLiteral("libCCGraphiteP11")},
        {QStringLiteral("manufacturer"), root.value(QStringLiteral("providerName")).toString()},
        {QStringLiteral("description"), root.value(QStringLiteral("providerDescription")).toString()},
        {QStringLiteral("cryptokiVersion"), root.value(QStringLiteral("cryptokiVersion")).toString()},
        {QStringLiteral("libraryVersion"), root.value(QStringLiteral("libraryVersion")).toString()},
    };
    QVariantMap token = selected.toVariantMap();
    const QVariantList certificates = selected.value(QStringLiteral("certificates")).toArray().toVariantList();
    token.remove(QStringLiteral("certificates"));
    const QString tokenState = selected.value(QStringLiteral("detailsState")).toString();
    const QString state = tokenState.isEmpty()
        ? (certificates.isEmpty() ? QStringLiteral("no-certificates") : QStringLiteral("ready"))
        : tokenState;

    updateReaderDetails(readerName, {
        {QStringLiteral("detailsState"), state},
        {QStringLiteral("detailsError"), selected.value(QStringLiteral("error")).toString()},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("token"), token},
        {QStringLiteral("certificates"), certificates},
        {QStringLiteral("providerName"), QStringLiteral("libCCGraphiteP11")},
    });
    debugLog(QStringLiteral("selected token label=\"%1\" state=%2 certificates=%3")
        .arg(selected.value(QStringLiteral("label")).toString())
        .arg(state)
        .arg(certificates.size()));
    clearRetryState();
}

void SmartCardMonitor::retryProbe()
{
    debugLog(QStringLiteral("retry timer fired (attempt %1)").arg(m_probeRetryCount));
    const QString readerName = m_probeReaderName;
    const QString key = m_probeCardKey;
    const std::filesystem::path providerPath = m_probeProviderPath;
    const int generation = m_probeGeneration;
    const QVariantMap reader = currentReader(readerName);
    if (readerName.isEmpty() || providerPath.empty()
        || reader.isEmpty() || !reader.value(QStringLiteral("present")).toBool()
        || cardKey(reader) != key || readerGeneration(readerName, key) != generation) {
        m_probeReaderName.clear();
        m_probeCardKey.clear();
        m_probeProviderPath.clear();
        m_probeGeneration = 0;
        m_probeRetryCount = 0;
        return;
    }

    startProbe(readerName, key, generation, providerPath);
}

void SmartCardMonitor::debugLog(const QString &message) const
{
    if (!m_debugLogging)
        return;
    const QByteArray text = message.toLocal8Bit();
    std::fprintf(stderr, "SmartCardMonitor: %s\n", text.constData());
    std::fflush(stderr);
}

void SmartCardMonitor::onProviderComponentChanged(const QString &id)
{
    if (id == QLatin1String("libccgraphite"))
        invalidateProviderDetails();
}

void SmartCardMonitor::invalidateProviderDetails()
{
    m_providerDetails.clear();
    if (m_probeProcess) {
        disconnect(m_probeProcess, nullptr, this, nullptr);
        m_probeProcess->kill();
        m_probeProcess->deleteLater();
        m_probeProcess = nullptr;
    }
    m_probeTimeout.stop();
    m_probeRetryTimer.stop();
    m_probeReaderName.clear();
    m_probeCardKey.clear();
    m_probeProviderPath.clear();
    m_probeGeneration = 0;
    m_probeRetryCount = 0;
    m_probeTimedOut = false;

    QVariantList updated = m_readers;
    for (QVariant &value : updated) {
        QVariantMap reader = value.toMap();
        if (reader.value(QStringLiteral("present")).toBool()) {
            reader.insert(QStringLiteral("detailsState"), QStringLiteral("not-requested"));
            reader.insert(QStringLiteral("detailsError"), QString());
            reader.insert(QStringLiteral("provider"), QVariantMap());
            reader.insert(QStringLiteral("token"), QVariantMap());
            reader.insert(QStringLiteral("certificates"), QVariantList());
            reader.insert(QStringLiteral("providerName"), QString());
            reader.insert(QStringLiteral("certificateSubject"), QString());
            reader.insert(QStringLiteral("certificateIssuer"), QString());
            reader.insert(QStringLiteral("certificateSerial"), QString());
            reader.insert(QStringLiteral("certificateValidFrom"), QString());
            reader.insert(QStringLiteral("certificateValidUntil"), QString());
            value = reader;
        }
    }
    updateState(m_available, m_cardPresent, updated);
}
