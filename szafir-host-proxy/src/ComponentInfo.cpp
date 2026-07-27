#include "ComponentInfo.h"

#include <KLocalizedString>

#include "ComponentDownloader.h"
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>


// ── AboutPageComponentInfo ────────────────────────────────────────────────────

AboutPageComponentInfo::AboutPageComponentInfo(ComponentDownloader *downloader, QObject *parent)
    : QObject(parent)
    , m_downloader(downloader)
{
    QFile f(QStringLiteral(":/szafir-host-proxy/system_components.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "ComponentInfo: failed to open system_components.json resource";
        return;
    }
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll())
        .object().value(QStringLiteral("system_components")).toArray();
    const QString lang = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    for (const QJsonValue &v : arr) {
        const QJsonObject obj    = v.toObject();
        const QString     scope  = obj[QStringLiteral("scope")].toString();
        const QString     sha256 = obj[QStringLiteral("source_sha256")].toString();
        const QJsonValue  nameVal = obj[QStringLiteral("display_name")];
        QString name;
        if (nameVal.isObject()) {
            const QJsonObject namesObj = nameVal.toObject();
            name = namesObj.value(lang).toString();
            if (name.isEmpty())
                name = namesObj.value(QStringLiteral("en")).toString();
        } else {
            name = nameVal.toString();
        }
        m_systemComponents.append(Component{
            .name      = name,
            .subtitle  = scope == QLatin1String("app") ? QString{i18n("App system component")}
                                                       : QString{i18n("Runtime system component")},
            .version   = obj[QStringLiteral("version")].toString(),
            .hashLabel = sha256.isEmpty() ? QString{} : QString{i18n("SHA256 (source):")},
            .hash      = sha256,
        });
    }
}

QList<Component> AboutPageComponentInfo::buildComponentList() const
{
    QList<Component> result = m_systemComponents;
    result.append(m_downloader->presentDisplayEntries());
    return result;
}
