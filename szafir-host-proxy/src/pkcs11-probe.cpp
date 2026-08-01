#include "Pkcs11Probe.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <cstring>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    const bool debug = argc == 3 && std::strcmp(argv[1], "--debug") == 0;
    const int providerIndex = debug ? 2 : 1;
    if (argc != providerIndex + 1) {
        QTextStream(stderr) << "usage: szafir-pkcs11-probe [--debug] PROVIDER_PATH\n";
        return 2;
    }

    const QJsonObject result = Pkcs11Probe::run(QString::fromLocal8Bit(argv[providerIndex]), debug);
    QTextStream output(stdout);
    output << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    output.flush();
    return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}
