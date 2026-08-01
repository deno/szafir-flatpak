#pragma once

#include <QJsonObject>
#include <QString>

namespace Pkcs11Probe {

QJsonObject run(const QString &providerPath, bool debug = false);

} // namespace Pkcs11Probe
