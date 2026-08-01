#include "Pkcs11Probe.h"

#include "CertificateParser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QLibrary>
#include <QStringView>
#include <QThread>

#include <p11-kit/pkcs11.h>

#include <winscard.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

QString fixedField(const CK_UTF8CHAR *data, std::size_t size)
{
    QByteArray value(reinterpret_cast<const char *>(data), static_cast<qsizetype>(size));
    const qsizetype nul = value.indexOf('\0');
    if (nul >= 0)
        value.truncate(nul);
    return QString::fromUtf8(value).trimmed();
}

QString versionText(const CK_VERSION &version)
{
    return QStringLiteral("%1.%2").arg(version.major).arg(version.minor);
}

QString rvText(CK_RV rv)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(rv), 8, 16, QLatin1Char('0')).toUpper();
}

QJsonObject failure(const QString &code, const QString &message, CK_RV rv = CKR_OK)
{
    QJsonObject result {
        {QStringLiteral("ok"), false},
        {QStringLiteral("errorCode"), code},
        {QStringLiteral("error"), message},
    };
    if (rv != CKR_OK)
        result.insert(QStringLiteral("pkcs11ReturnValue"), rvText(rv));
    return result;
}

void debugLog(bool enabled, const QString &message)
{
    if (!enabled)
        return;
    const QByteArray text = message.toLocal8Bit();
    std::fprintf(stderr, "pkcs11-probe: %s\n", text.constData());
    std::fflush(stderr);
}

QString pcscRvText(LONG rv)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<qulonglong>(static_cast<unsigned long>(rv)), 8, 16, QLatin1Char('0'))
        .toUpper();
}

QString atrHex(const BYTE *atr, DWORD length)
{
    if (!atr || length == 0)
        return {};
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(atr), static_cast<qsizetype>(length)).toHex(' ')).toUpper();
}

QString protocolName(DWORD protocol)
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

void logPcscSnapshot(bool debug, const char *when, bool connect)
{
    if (!debug)
        return;
    const QString tag = QStringLiteral("pcsc[%1]: ").arg(QString::fromLatin1(when));

    SCARDCONTEXT context = 0;
    LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &context);
    if (rv != SCARD_S_SUCCESS) {
        debugLog(debug, tag + QStringLiteral("SCardEstablishContext failed (%1)").arg(pcscRvText(rv)));
        return;
    }
    debugLog(debug, tag + QStringLiteral("SCardEstablishContext succeeded"));

    DWORD len = 0;
    rv = SCardListReaders(context, nullptr, nullptr, &len);
    if (rv == SCARD_E_NO_READERS_AVAILABLE) {
        debugLog(debug, tag + QStringLiteral("no readers available"));
        SCardReleaseContext(context);
        return;
    }
    if (rv != SCARD_S_SUCCESS) {
        debugLog(debug, tag + QStringLiteral("SCardListReaders(size) failed (%1)").arg(pcscRvText(rv)));
        SCardReleaseContext(context);
        return;
    }

    QByteArray buf(static_cast<int>(len), '\0');
    rv = SCardListReaders(context, nullptr, buf.data(), &len);
    if (rv != SCARD_S_SUCCESS) {
        debugLog(debug, tag + QStringLiteral("SCardListReaders(data) failed (%1)").arg(pcscRvText(rv)));
        SCardReleaseContext(context);
        return;
    }

    // Keep the name buffers alive so the szReader pointers stay valid.
    QVector<QByteArray> names;
    for (const char *p = buf.constData(); *p; p += std::strlen(p) + 1)
        names.append(QByteArray(p));
    debugLog(debug, tag + QStringLiteral("%1 reader(s)").arg(names.size()));
    if (names.isEmpty()) {
        SCardReleaseContext(context);
        return;
    }

    QVector<SCARD_READERSTATE> states(names.size());
    std::memset(states.data(), 0, static_cast<size_t>(states.size()) * sizeof(SCARD_READERSTATE));
    for (int i = 0; i < names.size(); ++i) {
        states[i].szReader = names[i].constData();
        states[i].dwCurrentState = SCARD_STATE_UNAWARE;
    }

    rv = SCardGetStatusChange(context, 0, states.data(), static_cast<DWORD>(states.size()));
    if (rv != SCARD_S_SUCCESS && rv != SCARD_E_TIMEOUT) {
        debugLog(debug, tag + QStringLiteral("SCardGetStatusChange failed (%1)").arg(pcscRvText(rv)));
        SCardReleaseContext(context);
        return;
    }

    for (int i = 0; i < names.size(); ++i) {
        const bool present = (states[i].dwEventState & SCARD_STATE_PRESENT) != 0;
        debugLog(debug, tag + QStringLiteral("reader \"%1\": present=%2 ATR=%3")
            .arg(QString::fromUtf8(names[i]))
            .arg(present ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(atrHex(states[i].rgbAtr, states[i].cbAtr)));

        if (!connect || !present)
            continue;

        SCARDHANDLE card = 0;
        DWORD protocol = 0;
        LONG connectRv = SCardConnect(context,
                                      names[i].constData(),
                                      SCARD_SHARE_SHARED,
                                      SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1 | SCARD_PROTOCOL_RAW,
                                      &card,
                                      &protocol);
        debugLog(debug, tag + QStringLiteral("reader \"%1\": SCardConnect(shared) -> %2 protocol=%3")
            .arg(QString::fromUtf8(names[i]))
            .arg(pcscRvText(connectRv))
            .arg(protocolName(protocol)));
        if (connectRv != SCARD_S_SUCCESS) {
            card = 0;
            protocol = 0;
            connectRv = SCardConnect(context,
                                     names[i].constData(),
                                     SCARD_SHARE_DIRECT,
                                     0,
                                     &card,
                                     &protocol);
            debugLog(debug, tag + QStringLiteral("reader \"%1\": SCardConnect(direct) -> %2")
                .arg(QString::fromUtf8(names[i])).arg(pcscRvText(connectRv)));
        }
        if (connectRv == SCARD_S_SUCCESS)
            SCardDisconnect(card, SCARD_LEAVE_CARD);
    }

    SCardReleaseContext(context);
}

QJsonObject tokenInfo(const CK_TOKEN_INFO &info)
{
    QJsonObject result {
        {QStringLiteral("label"), fixedField(info.label, sizeof(info.label))},
        {QStringLiteral("manufacturer"), fixedField(info.manufacturerID, sizeof(info.manufacturerID))},
        {QStringLiteral("model"), fixedField(info.model, sizeof(info.model))},
        {QStringLiteral("serial"), fixedField(info.serialNumber, sizeof(info.serialNumber))},
        {QStringLiteral("hardwareVersion"), versionText(info.hardwareVersion)},
        {QStringLiteral("firmwareVersion"), versionText(info.firmwareVersion)},
        {QStringLiteral("flags"), static_cast<qint64>(info.flags)},
    };
    return result;
}

QByteArray getAttribute(CK_FUNCTION_LIST_PTR functions,
                        CK_SESSION_HANDLE session,
                        CK_OBJECT_HANDLE object,
                        CK_ATTRIBUTE_TYPE type,
                        CK_RV *resultRv)
{
    CK_ATTRIBUTE attribute {type, nullptr, 0};
    CK_RV rv = functions->C_GetAttributeValue(session, object, &attribute, 1);
    if (rv != CKR_OK && rv != CKR_BUFFER_TOO_SMALL) {
        if (resultRv)
            *resultRv = rv;
        return {};
    }
    if (attribute.ulValueLen == CK_UNAVAILABLE_INFORMATION || attribute.ulValueLen > 1024 * 1024) {
        if (resultRv)
            *resultRv = CKR_ATTRIBUTE_SENSITIVE;
        return {};
    }

    QByteArray value(static_cast<qsizetype>(attribute.ulValueLen), '\0');
    attribute.pValue = value.data();
    rv = functions->C_GetAttributeValue(session, object, &attribute, 1);
    if (resultRv)
        *resultRv = rv;
    if (rv != CKR_OK)
        return {};
    value.resize(static_cast<qsizetype>(attribute.ulValueLen));
    return value;
}

QJsonObject slotDetails(CK_FUNCTION_LIST_PTR functions, CK_SLOT_ID slot, bool debug)
{
    CK_SLOT_INFO slotInfo {};
    CK_RV rv = functions->C_GetSlotInfo(slot, &slotInfo);
    if (rv != CKR_OK) {
        debugLog(debug, QStringLiteral("slot %1: C_GetSlotInfo failed (%2)")
            .arg(static_cast<qulonglong>(slot)).arg(rvText(rv)));
        return failure(QStringLiteral("slot-info-failed"), QStringLiteral("C_GetSlotInfo failed"), rv);
    }

    CK_TOKEN_INFO tokenInfoValue {};
    rv = functions->C_GetTokenInfo(slot, &tokenInfoValue);
    if (rv != CKR_OK) {
        debugLog(debug, QStringLiteral("slot %1 (%2): C_GetTokenInfo failed (%3)")
            .arg(static_cast<qulonglong>(slot))
            .arg(fixedField(slotInfo.slotDescription, sizeof(slotInfo.slotDescription)))
            .arg(rvText(rv)));
        return failure(QStringLiteral("token-info-failed"), QStringLiteral("C_GetTokenInfo failed"), rv);
    }

    QJsonObject token = tokenInfo(tokenInfoValue);
    token.insert(QStringLiteral("slotId"), static_cast<qint64>(slot));
    token.insert(QStringLiteral("slotDescription"), fixedField(slotInfo.slotDescription, sizeof(slotInfo.slotDescription)));
    token.insert(QStringLiteral("slotManufacturer"), fixedField(slotInfo.manufacturerID, sizeof(slotInfo.manufacturerID)));
    token.insert(QStringLiteral("slotHardwareVersion"), versionText(slotInfo.hardwareVersion));
    token.insert(QStringLiteral("slotFirmwareVersion"), versionText(slotInfo.firmwareVersion));
    token.insert(QStringLiteral("slotFlags"), static_cast<qint64>(slotInfo.flags));

    CK_SESSION_HANDLE session = 0;
    rv = functions->C_OpenSession(slot,
                                  CKF_SERIAL_SESSION,
                                  nullptr,
                                  nullptr,
                                  &session);
    if (rv != CKR_OK) {
        debugLog(debug, QStringLiteral("slot %1 (%2): C_OpenSession failed (%3)")
            .arg(static_cast<qulonglong>(slot))
            .arg(token.value(QStringLiteral("label")).toString())
            .arg(rvText(rv)));
        token.insert(QStringLiteral("certificates"), QJsonArray());
        token.insert(QStringLiteral("detailsState"), rv == CKR_USER_NOT_LOGGED_IN
                         ? QStringLiteral("login-required")
                         : QStringLiteral("session-failed"));
        token.insert(QStringLiteral("error"), rvText(rv));
        return token;
    }

    const CK_ULONG certificateClass = CKO_CERTIFICATE;
    CK_ATTRIBUTE templateAttribute {
        CKA_CLASS,
        const_cast<CK_ULONG *>(&certificateClass),
        sizeof(certificateClass)
    };
    rv = functions->C_FindObjectsInit(session, &templateAttribute, 1);
    if (rv != CKR_OK) {
        debugLog(debug, QStringLiteral("slot %1 (%2): C_FindObjectsInit failed (%3)")
            .arg(static_cast<qulonglong>(slot))
            .arg(token.value(QStringLiteral("label")).toString())
            .arg(rvText(rv)));
        functions->C_CloseSession(session);
        if (rv == CKR_USER_NOT_LOGGED_IN) {
            token.insert(QStringLiteral("certificates"), QJsonArray());
            token.insert(QStringLiteral("detailsState"), QStringLiteral("login-required"));
            token.insert(QStringLiteral("error"), rvText(rv));
            return token;
        }
        return failure(QStringLiteral("find-init-failed"), QStringLiteral("C_FindObjectsInit failed"), rv);
    }

    QJsonArray certificates;
    for (int index = 0; index < 32; ++index) {
        CK_OBJECT_HANDLE object = 0;
        CK_ULONG count = 0;
        rv = functions->C_FindObjects(session, &object, 1, &count);
        if (rv != CKR_OK || count == 0)
            break;

        CK_RV attributeRv = CKR_OK;
        const QByteArray der = getAttribute(functions, session, object, CKA_VALUE, &attributeRv);
        if (der.isEmpty())
            continue;

        QString parseError;
        QJsonObject certificate = CertificateParser::parseDer(der, &parseError);
        if (certificate.isEmpty())
            continue;

        certificate.insert(QStringLiteral("objectId"), QString::fromLatin1(getAttribute(functions, session, object, CKA_ID, &attributeRv).toHex()).toUpper());
        const QByteArray label = getAttribute(functions, session, object, CKA_LABEL, &attributeRv);
        if (!label.isEmpty())
            certificate.insert(QStringLiteral("label"), QString::fromUtf8(label).trimmed());
        certificates.append(certificate);
    }
    functions->C_FindObjectsFinal(session);
    functions->C_CloseSession(session);

    token.insert(QStringLiteral("certificates"), certificates);
    token.insert(QStringLiteral("detailsState"), certificates.isEmpty()
                     ? QStringLiteral("no-certificates")
                     : QStringLiteral("ready"));
    debugLog(debug, QStringLiteral("slot %1: label=\"%2\" slotDescription=\"%3\" state=%4 certificates=%5")
        .arg(static_cast<qulonglong>(slot))
        .arg(token.value(QStringLiteral("label")).toString())
        .arg(token.value(QStringLiteral("slotDescription")).toString())
        .arg(token.value(QStringLiteral("detailsState")).toString())
        .arg(certificates.size()));
    return token;
}

} // namespace

namespace Pkcs11Probe {

QJsonObject run(const QString &providerPath, bool debug)
{
    if (providerPath.isEmpty())
        return failure(QStringLiteral("provider-missing"), QStringLiteral("provider path is empty"));

    logPcscSnapshot(debug, "pre-init", false);

    debugLog(debug, QStringLiteral("loading provider %1").arg(providerPath));
    QLibrary library(providerPath);
    if (!library.load()) {
        debugLog(debug, QStringLiteral("provider load failed: %1").arg(library.errorString()));
        return failure(QStringLiteral("module-load-failed"), library.errorString());
    }

    using GetFunctionList = CK_RV (*)(CK_FUNCTION_LIST_PTR_PTR);
    auto getFunctionList = reinterpret_cast<GetFunctionList>(library.resolve("C_GetFunctionList"));
    if (!getFunctionList) {
        debugLog(debug, QStringLiteral("C_GetFunctionList is unavailable"));
        return failure(QStringLiteral("provider-incompatible"), QStringLiteral("C_GetFunctionList is unavailable"));
    }

    CK_FUNCTION_LIST_PTR functions = nullptr;
    CK_RV rv = getFunctionList(&functions);
    if (rv != CKR_OK || !functions) {
        debugLog(debug, QStringLiteral("C_GetFunctionList failed (%1)").arg(rvText(rv)));
        return failure(QStringLiteral("provider-incompatible"), QStringLiteral("C_GetFunctionList failed"), rv);
    }

    rv = functions->C_Initialize(nullptr);
    if (rv != CKR_OK && rv != CKR_CRYPTOKI_ALREADY_INITIALIZED) {
        debugLog(debug, QStringLiteral("C_Initialize failed (%1)").arg(rvText(rv)));
        return failure(QStringLiteral("initialize-failed"), QStringLiteral("C_Initialize failed"), rv);
    }
    debugLog(debug, rv == CKR_CRYPTOKI_ALREADY_INITIALIZED
        ? QStringLiteral("C_Initialize: already initialized")
        : QStringLiteral("C_Initialize: success"));

    CK_INFO info {};
    rv = functions->C_GetInfo(&info);
    if (rv != CKR_OK) {
        debugLog(debug, QStringLiteral("C_GetInfo failed (%1)").arg(rvText(rv)));
        functions->C_Finalize(nullptr);
        return failure(QStringLiteral("provider-info-failed"), QStringLiteral("C_GetInfo failed"), rv);
    }

    debugLog(debug, QStringLiteral("providerName=\"%1\" description=\"%2\" cryptoki=%3 library=%4")
        .arg(fixedField(info.manufacturerID, sizeof(info.manufacturerID)))
        .arg(fixedField(info.libraryDescription, sizeof(info.libraryDescription)))
        .arg(versionText(info.cryptokiVersion))
        .arg(versionText(info.libraryVersion)));

    QJsonObject result {
        {QStringLiteral("ok"), true},
        {QStringLiteral("providerName"), fixedField(info.manufacturerID, sizeof(info.manufacturerID))},
        {QStringLiteral("providerDescription"), fixedField(info.libraryDescription, sizeof(info.libraryDescription))},
        {QStringLiteral("cryptokiVersion"), versionText(info.cryptokiVersion)},
        {QStringLiteral("libraryVersion"), versionText(info.libraryVersion)},
    };

    CK_ULONG slotCount = 0;
    constexpr int slotPollAttempts = 30;
    for (int attempt = 0; attempt < slotPollAttempts; ++attempt) {
        slotCount = 0;
        rv = functions->C_GetSlotList(CK_TRUE, nullptr, &slotCount);
        if (rv != CKR_OK) {
            debugLog(debug, QStringLiteral("C_GetSlotList(CK_TRUE) failed on attempt %1 (%2)")
                .arg(attempt + 1).arg(rvText(rv)));
            functions->C_Finalize(nullptr);
            return failure(QStringLiteral("slot-list-failed"), QStringLiteral("C_GetSlotList failed"), rv);
        }
        debugLog(debug, QStringLiteral("C_GetSlotList(CK_TRUE) attempt %1/%2 -> %3 slot(s)")
            .arg(attempt + 1).arg(slotPollAttempts).arg(static_cast<qulonglong>(slotCount)));
        if (slotCount > 0 || attempt + 1 == slotPollAttempts)
            break;
        QThread::msleep(100);
    }
    if (slotCount == 0) {
        CK_ULONG allSlots = 0;
        rv = functions->C_GetSlotList(CK_FALSE, nullptr, &allSlots);
        debugLog(debug, QStringLiteral("C_GetSlotList(CK_FALSE) -> %1 slot(s) (rv=%2)")
            .arg(static_cast<qulonglong>(allSlots)).arg(rvText(rv)));
        logPcscSnapshot(debug, "post-poll", true);
    }
    slotCount = std::min<CK_ULONG>(slotCount, 32);
    QVector<CK_SLOT_ID> slotIds(static_cast<qsizetype>(slotCount));
    if (slotCount > 0) {
        rv = functions->C_GetSlotList(CK_TRUE, slotIds.data(), &slotCount);
        if (rv != CKR_OK) {
            debugLog(debug, QStringLiteral("C_GetSlotList(CK_TRUE) fill failed (%1)").arg(rvText(rv)));
            functions->C_Finalize(nullptr);
            return failure(QStringLiteral("slot-list-failed"), QStringLiteral("C_GetSlotList fill failed"), rv);
        }
    }

    QJsonArray tokens;
    for (CK_ULONG index = 0; index < slotCount; ++index) {
        QJsonObject token = slotDetails(functions, slotIds[static_cast<qsizetype>(index)], debug);
        if (token.value(QStringLiteral("ok")).isBool() && !token.value(QStringLiteral("ok")).toBool())
            continue;
        tokens.append(token);
    }
    result.insert(QStringLiteral("tokens"), tokens);
    result.insert(QStringLiteral("detailsState"), tokens.isEmpty()
                     ? QStringLiteral("no-token")
                     : QStringLiteral("ready"));

    debugLog(debug, QStringLiteral("probe complete: %1 token(s), detailsState=%2")
        .arg(tokens.size()).arg(result.value(QStringLiteral("detailsState")).toString()));

    functions->C_Finalize(nullptr);
    return result;
}

} // namespace Pkcs11Probe
