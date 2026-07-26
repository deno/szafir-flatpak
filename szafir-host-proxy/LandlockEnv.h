#pragma once

#include <QByteArray>
#include <QDebug>

namespace LandlockEnv {

inline bool readOnOffEnv(const char *name, bool defaultValue = true)
{
    const QByteArray raw = qgetenv(name).trimmed().toLower();
    if (raw.isEmpty())
        return defaultValue;
    if (raw == "on")
        return true;
    if (raw == "off")
        return false;

    qWarning() << name << "must be 'on' or 'off'; got" << raw << "- using"
               << (defaultValue ? "on" : "off");
    return defaultValue;
}

inline bool isModuleEnabled(const char *moduleEnvName)
{
    // Inside the bwrap namespace, Landlock is mandatory — ignore kill switches.
    if (qgetenv("SZAFIR_BWRAPPED") == "1")
        return true;
    if (!readOnOffEnv("LANDLOCK", true))
        return false;
    return readOnOffEnv(moduleEnvName, true);
}

} // namespace LandlockEnv