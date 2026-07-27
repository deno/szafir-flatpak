#pragma once

#include "Component.h"

#include <QList>
#include <QObject>
#include <QVariantList>

class ComponentDownloader;

// ── AboutPageComponentInfo ────────────────────────────────────────────────────

class AboutPageComponentInfo : public QObject
{
    Q_OBJECT
public:
    explicit AboutPageComponentInfo(ComponentDownloader *downloader, QObject *parent = nullptr);

    /**
     * Returns all display-ready components (system first, then installed) as a
     * list of Component gadgets.
     */
    Q_INVOKABLE QList<Component> buildComponentList() const;

private:
    QList<Component> m_systemComponents;
    ComponentDownloader *m_downloader;
};
