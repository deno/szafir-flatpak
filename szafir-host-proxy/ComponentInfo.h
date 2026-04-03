#pragma once

#include "Component.h"

#include <QList>
#include <QObject>
#include <QVariantList>

#ifdef BUNDLED_HOST
class ComponentDownloader;
#endif

// ── AboutPageComponentInfo ────────────────────────────────────────────────────

class AboutPageComponentInfo : public QObject
{
    Q_OBJECT
public:
    explicit AboutPageComponentInfo(
#ifdef BUNDLED_HOST
        ComponentDownloader *downloader,
#endif
        QObject *parent = nullptr);

    /**
     * Returns all display-ready components (system first, then installed) as a
     * list of Component gadgets.
     */
    Q_INVOKABLE QList<Component> buildComponentList() const;

private:
    QList<Component> m_systemComponents;
#ifdef BUNDLED_HOST
    ComponentDownloader *m_downloader;
#endif
};
