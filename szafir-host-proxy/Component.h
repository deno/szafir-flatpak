#pragma once

#include <QObject>
#include <QString>

// ── Component ─────────────────────────────────────────────────────────────────
// Fields shared by both system components (system_components.json) and
// downloaded components (components.json).  Used as the sole type for the
// About-page component list.

struct Component {
    Q_GADGET
    Q_PROPERTY(QString name      MEMBER name)
    Q_PROPERTY(QString subtitle  MEMBER subtitle)
    Q_PROPERTY(QString version   MEMBER version)
    Q_PROPERTY(QString hashLabel MEMBER hashLabel)
    Q_PROPERTY(QString hash      MEMBER hash)
public:
    QString name;
    QString subtitle;  ///< e.g. "App system component" — empty for downloaded components
    QString version;
    QString hashLabel; ///< e.g. "SHA256 (source):", empty when not applicable
    QString hash;
};
Q_DECLARE_METATYPE(Component)
