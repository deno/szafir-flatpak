#pragma once

#include <QByteArray>
#include <QJsonObject>

namespace CertificateParser {

// Parse a DER encoded X.509 certificate into the JSON fields consumed by the
// PKCS#11 probe.  The returned object is empty when parsing fails and error is
// populated with a diagnostic suitable for logs (never certificate contents).
QJsonObject parseDer(const QByteArray &der, QString *error = nullptr);

} // namespace CertificateParser
