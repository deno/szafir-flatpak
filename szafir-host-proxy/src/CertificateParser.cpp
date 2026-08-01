#include "CertificateParser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimeZone>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include <algorithm>
#include <ctime>

namespace {

QString gnutlsError(int rv)
{
    return QString::fromLatin1(gnutls_strerror(rv));
}

QString distinguishedName(gnutls_x509_crt_t certificate, bool issuer, int *rvOut)
{
    std::size_t size = 0;
    int rv = issuer ? gnutls_x509_crt_get_issuer_dn(certificate, nullptr, &size)
                    : gnutls_x509_crt_get_dn(certificate, nullptr, &size);
    if (rv != GNUTLS_E_SHORT_MEMORY_BUFFER && rv != GNUTLS_E_SUCCESS) {
        if (rvOut)
            *rvOut = rv;
        return {};
    }

    QByteArray value(static_cast<qsizetype>(size + 1), '\0');
    rv = issuer ? gnutls_x509_crt_get_issuer_dn(certificate, value.data(), &size)
                : gnutls_x509_crt_get_dn(certificate, value.data(), &size);
    if (rvOut)
        *rvOut = rv;
    if (rv != GNUTLS_E_SUCCESS)
        return {};
    return QString::fromUtf8(value.constData(), static_cast<qsizetype>(size)).trimmed();
}

QString distinguishedNameField(gnutls_x509_crt_t certificate,
                               const char *oid,
                               bool issuer)
{
    std::size_t size = 0;
    int rv = gnutls_x509_crt_get_dn_by_oid(certificate, oid, 0, 0, nullptr, &size);
    if (rv != GNUTLS_E_SHORT_MEMORY_BUFFER && rv != GNUTLS_E_SUCCESS)
        return {};

    QByteArray value(static_cast<qsizetype>(size + 1), '\0');
    rv = gnutls_x509_crt_get_dn_by_oid(certificate, oid, 0, 0, value.data(), &size);
    if (rv != GNUTLS_E_SUCCESS)
        return {};

    Q_UNUSED(issuer);
    return QString::fromUtf8(value.constData(), static_cast<qsizetype>(size)).trimmed();
}

QString timeText(std::time_t value)
{
    if (value == static_cast<std::time_t>(-1))
        return {};
    const QDateTime dateTime = QDateTime::fromSecsSinceEpoch(value, QTimeZone::UTC);
    return dateTime.toString(Qt::ISODateWithMs);
}

QString serialDecimalSimple(const QByteArray &serial)
{
    QByteArray decimal("0");
    for (unsigned char input : serial) {
        int carry = input;
        for (int i = decimal.size() - 1; i >= 0; --i) {
            const int value = (decimal[i] - '0') * 256 + carry;
            decimal[i] = static_cast<char>('0' + (value % 10));
            carry = value / 10;
        }
        while (carry > 0) {
            decimal.prepend(static_cast<char>('0' + (carry % 10)));
            carry /= 10;
        }
    }
    return QString::fromLatin1(decimal);
}

QString algorithmName(gnutls_pk_algorithm_t algorithm)
{
    const char *name = gnutls_pk_algorithm_get_name(algorithm);
    return name ? QString::fromLatin1(name) : QString();
}

QString signatureName(gnutls_sign_algorithm_t algorithm)
{
    const char *name = gnutls_sign_get_name(algorithm);
    return name ? QString::fromLatin1(name) : QString();
}

QString keyUsageName(unsigned int bit)
{
    switch (bit) {
    case GNUTLS_KEY_DIGITAL_SIGNATURE: return QStringLiteral("Digital signature");
    case GNUTLS_KEY_NON_REPUDIATION: return QStringLiteral("Non-repudiation");
    case GNUTLS_KEY_KEY_ENCIPHERMENT: return QStringLiteral("Key encipherment");
    case GNUTLS_KEY_DATA_ENCIPHERMENT: return QStringLiteral("Data encipherment");
    case GNUTLS_KEY_KEY_AGREEMENT: return QStringLiteral("Key agreement");
    case GNUTLS_KEY_KEY_CERT_SIGN: return QStringLiteral("Certificate signing");
    case GNUTLS_KEY_CRL_SIGN: return QStringLiteral("CRL signing");
    case GNUTLS_KEY_ENCIPHER_ONLY: return QStringLiteral("Encipher only");
    case GNUTLS_KEY_DECIPHER_ONLY: return QStringLiteral("Decipher only");
    default: return {};
    }
}

QString sanTypeName(unsigned int type)
{
    switch (type) {
    case GNUTLS_SAN_DNSNAME: return QStringLiteral("DNS");
    case GNUTLS_SAN_RFC822NAME: return QStringLiteral("Email");
    case GNUTLS_SAN_URI: return QStringLiteral("URI");
    case GNUTLS_SAN_IPADDRESS: return QStringLiteral("IP");
    case GNUTLS_SAN_DN: return QStringLiteral("DN");
    case GNUTLS_SAN_REGISTERED_ID: return QStringLiteral("Registered ID");
    default: return QStringLiteral("Other");
    }
}

QJsonArray keyUsages(gnutls_x509_crt_t certificate)
{
    unsigned int usage = 0;
    unsigned int critical = 0;
    if (gnutls_x509_crt_get_key_usage(certificate, &usage, &critical) != GNUTLS_E_SUCCESS)
        return {};

    QJsonArray result;
    constexpr unsigned int bits[] = {
        GNUTLS_KEY_DIGITAL_SIGNATURE,
        GNUTLS_KEY_NON_REPUDIATION,
        GNUTLS_KEY_KEY_ENCIPHERMENT,
        GNUTLS_KEY_DATA_ENCIPHERMENT,
        GNUTLS_KEY_KEY_AGREEMENT,
        GNUTLS_KEY_KEY_CERT_SIGN,
        GNUTLS_KEY_CRL_SIGN,
        GNUTLS_KEY_ENCIPHER_ONLY,
        GNUTLS_KEY_DECIPHER_ONLY,
    };
    for (unsigned int bit : bits) {
        if (usage & bit)
            result.append(keyUsageName(bit));
    }
    return result;
}

QJsonArray extendedKeyUsages(gnutls_x509_crt_t certificate)
{
    QJsonArray result;
    for (unsigned int index = 0; index < 64; ++index) {
        std::size_t size = 0;
        unsigned int critical = 0;
        int rv = gnutls_x509_crt_get_key_purpose_oid(certificate, index, nullptr, &size, &critical);
        if (rv == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE)
            break;
        if (rv != GNUTLS_E_SHORT_MEMORY_BUFFER)
            break;

        QByteArray oid(static_cast<qsizetype>(size + 1), '\0');
        rv = gnutls_x509_crt_get_key_purpose_oid(certificate, index, oid.data(), &size, &critical);
        if (rv != GNUTLS_E_SUCCESS)
            break;
        result.append(QString::fromLatin1(oid.constData(), static_cast<qsizetype>(size)));
    }
    return result;
}

QJsonArray alternativeNames(gnutls_x509_crt_t certificate)
{
    QJsonArray result;
    for (unsigned int index = 0; index < 64; ++index) {
        std::size_t size = 0;
        unsigned int type = 0;
        unsigned int critical = 0;
        int rv = gnutls_x509_crt_get_subject_alt_name2(certificate, index, nullptr, &size, &type, &critical);
        if (rv == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE)
            break;
        if (rv != GNUTLS_E_SHORT_MEMORY_BUFFER)
            break;

        QByteArray value(static_cast<qsizetype>(size + 1), '\0');
        rv = gnutls_x509_crt_get_subject_alt_name2(certificate, index, value.data(), &size, &type, &critical);
        if (rv != GNUTLS_E_SUCCESS)
            break;

        result.append(QStringLiteral("%1: %2").arg(sanTypeName(type),
                                                   QString::fromUtf8(value.constData(), static_cast<qsizetype>(size))));
    }
    return result;
}

QJsonArray policyOids(gnutls_x509_crt_t certificate)
{
    QJsonArray result;
    for (unsigned int index = 0; index < 64; ++index) {
        gnutls_x509_policy_st policy {};
        unsigned int critical = 0;
        const int rv = gnutls_x509_crt_get_policy(certificate, index, &policy, &critical);
        if (rv == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE)
            break;
        if (rv != GNUTLS_E_SUCCESS)
            break;
        if (policy.oid)
            result.append(QString::fromLatin1(policy.oid));
        gnutls_x509_policy_release(&policy);
    }
    return result;
}

QJsonArray qualifiedStatements(gnutls_x509_crt_t certificate)
{
    constexpr const char *qcStatementsOid = "1.3.6.1.5.5.7.1.3";
    QJsonArray result;
    for (unsigned int index = 0; index < 8; ++index) {
        std::size_t size = 0;
        unsigned int critical = 0;
        const int rv = gnutls_x509_crt_get_extension_by_oid(certificate,
                                                              qcStatementsOid,
                                                              index,
                                                              nullptr,
                                                              &size,
                                                              &critical);
        if (rv == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE)
            break;
        if (rv != GNUTLS_E_SHORT_MEMORY_BUFFER && rv != GNUTLS_E_SUCCESS)
            break;
        result.append(QString::fromLatin1(qcStatementsOid));
    }
    return result;
}

QJsonArray extensionOids(gnutls_x509_crt_t certificate)
{
    QJsonArray result;
    for (unsigned int index = 0; index < 128; ++index) {
        std::size_t size = 0;
        unsigned int critical = 0;
        int rv = gnutls_x509_crt_get_extension_info(certificate, index, nullptr, &size, &critical);
        if (rv == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE)
            break;
        if (rv != GNUTLS_E_SHORT_MEMORY_BUFFER)
            break;

        QByteArray oid(static_cast<qsizetype>(size + 1), '\0');
        rv = gnutls_x509_crt_get_extension_info(certificate, index, oid.data(), &size, &critical);
        if (rv != GNUTLS_E_SUCCESS)
            break;
        result.append(QString::fromLatin1(oid.constData(), static_cast<qsizetype>(size)));
    }
    return result;
}

} // namespace

namespace CertificateParser {

QJsonObject parseDer(const QByteArray &der, QString *error)
{
    if (der.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty certificate");
        return {};
    }

    gnutls_x509_crt_t certificate = nullptr;
    int rv = gnutls_x509_crt_init(&certificate);
    if (rv != GNUTLS_E_SUCCESS) {
        if (error)
            *error = gnutlsError(rv);
        return {};
    }

    gnutls_datum_t datum {
        reinterpret_cast<unsigned char *>(const_cast<char *>(der.constData())),
        static_cast<unsigned int>(der.size())
    };
    rv = gnutls_x509_crt_import(certificate, &datum, GNUTLS_X509_FMT_DER);
    if (rv != GNUTLS_E_SUCCESS) {
        if (error)
            *error = gnutlsError(rv);
        gnutls_x509_crt_deinit(certificate);
        return {};
    }

    QJsonObject result;
    int dnRv = GNUTLS_E_SUCCESS;
    result.insert(QStringLiteral("subject"), distinguishedName(certificate, false, &dnRv));
    result.insert(QStringLiteral("issuer"), distinguishedName(certificate, true, &dnRv));
    result.insert(QStringLiteral("subjectCommonName"), distinguishedNameField(certificate, "2.5.4.3", false));
    result.insert(QStringLiteral("subjectGivenName"), distinguishedNameField(certificate, "2.5.4.42", false));
    result.insert(QStringLiteral("subjectSurname"), distinguishedNameField(certificate, "2.5.4.4", false));
    result.insert(QStringLiteral("organizationName"), distinguishedNameField(certificate, "2.5.4.10", false));
    result.insert(QStringLiteral("countryName"), distinguishedNameField(certificate, "2.5.4.6", false));

    QByteArray serial(256, '\0');
    std::size_t serialSize = static_cast<std::size_t>(serial.size());
    rv = gnutls_x509_crt_get_serial(certificate, serial.data(), &serialSize);
    if (rv == GNUTLS_E_SHORT_MEMORY_BUFFER) {
        serial.resize(static_cast<qsizetype>(serialSize));
        rv = gnutls_x509_crt_get_serial(certificate, serial.data(), &serialSize);
    }
    if (rv == GNUTLS_E_SUCCESS) {
        serial.resize(static_cast<qsizetype>(serialSize));
        result.insert(QStringLiteral("serialHex"), QString::fromLatin1(serial.toHex()).toUpper());
        result.insert(QStringLiteral("serialDecimal"), serialDecimalSimple(serial));
    }

    result.insert(QStringLiteral("version"), static_cast<int>(gnutls_x509_crt_get_version(certificate)));
    result.insert(QStringLiteral("validFrom"), timeText(gnutls_x509_crt_get_activation_time(certificate)));
    result.insert(QStringLiteral("validUntil"), timeText(gnutls_x509_crt_get_expiration_time(certificate)));

    unsigned int bits = 0;
    const auto pk = static_cast<gnutls_pk_algorithm_t>(gnutls_x509_crt_get_pk_algorithm(certificate, &bits));
    result.insert(QStringLiteral("publicKeyAlgorithm"), algorithmName(pk));
    result.insert(QStringLiteral("publicKeyBits"), static_cast<int>(bits));
    result.insert(QStringLiteral("signatureAlgorithm"), signatureName(static_cast<gnutls_sign_algorithm_t>(
        gnutls_x509_crt_get_signature_algorithm(certificate))));
    result.insert(QStringLiteral("keyUsage"), keyUsages(certificate));
    result.insert(QStringLiteral("extendedKeyUsage"), extendedKeyUsages(certificate));
    result.insert(QStringLiteral("alternativeNames"), alternativeNames(certificate));
    result.insert(QStringLiteral("policyOids"), policyOids(certificate));
    result.insert(QStringLiteral("qcStatements"), qualifiedStatements(certificate));
    result.insert(QStringLiteral("extensionOids"), extensionOids(certificate));

    for (const auto &[name, digest] : {
             std::pair{QStringLiteral("fingerprintSha256"), GNUTLS_DIG_SHA256},
             std::pair{QStringLiteral("fingerprintSha1"), GNUTLS_DIG_SHA1}}) {
        QByteArray fingerprint(64, '\0');
        std::size_t fingerprintSize = static_cast<std::size_t>(fingerprint.size());
        rv = gnutls_x509_crt_get_fingerprint(certificate,
                                              digest,
                                              fingerprint.data(),
                                              &fingerprintSize);
        if (rv == GNUTLS_E_SUCCESS) {
            fingerprint.resize(static_cast<qsizetype>(fingerprintSize));
            result.insert(name, QString::fromLatin1(fingerprint.toHex()).toUpper());
        }
    }

    gnutls_x509_crt_deinit(certificate);
    return result;
}

} // namespace CertificateParser
