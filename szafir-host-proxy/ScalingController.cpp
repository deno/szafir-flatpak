#include "ScalingController.h"
#include "AppSettings.h"

#include <QDebug>
#include <QFileSystemWatcher>
#include <KConfig>
#include <KConfigGroup>

ScalingController::ScalingController(
    const std::filesystem::path &szafirOverridePath,
    const std::filesystem::path &hostOverridePath,
    QObject *parent)
    : QObject(parent)
    , m_szafirOverridePath(szafirOverridePath)
    , m_hostOverridePath(hostOverridePath)
    , m_szafirScale(readScale(m_szafirOverridePath))
    , m_hostScale(readScale(m_hostOverridePath))
{
    const std::filesystem::path overridesDir = m_szafirOverridePath.parent_path();
    const QString overridesDirString = PathUtils::toQString(overridesDir);

    auto *watcher = new QFileSystemWatcher(this);
    if (!watcher->addPath(overridesDirString)) {
        // Directory may not exist yet; create it so we can watch it later.
        std::error_code ec;
        std::filesystem::create_directories(overridesDir, ec);
        watcher->addPath(overridesDirString);
    }
    connect(watcher, &QFileSystemWatcher::directoryChanged,
            this, &ScalingController::reloadScales);
}

QString ScalingController::szafirScale() const
{
    return m_szafirScale;
}

QString ScalingController::hostScale() const
{
    return m_hostScale;
}

void ScalingController::setSzafirScale(const QString &scale)
{
    writeScale(m_szafirOverridePath, scale);
    if (m_szafirScale != scale) {
        m_szafirScale = scale;
        emit szafirScaleChanged(m_szafirScale);
    }
}

void ScalingController::setHostScale(const QString &scale)
{
    writeScale(m_hostOverridePath, scale);
    if (m_hostScale != scale) {
        m_hostScale = scale;
        emit hostScaleChanged(m_hostScale);
    }
}

void ScalingController::reloadScales()
{
    const QString newSzafir = readScale(m_szafirOverridePath);
    if (m_szafirScale != newSzafir) {
        m_szafirScale = newSzafir;
        emit szafirScaleChanged(m_szafirScale);
    }
    const QString newHost = readScale(m_hostOverridePath);
    if (m_hostScale != newHost) {
        m_hostScale = newHost;
        emit hostScaleChanged(m_hostScale);
    }
}

QString ScalingController::readScale(const std::filesystem::path &overridePath)
{
    KConfig config(PathUtils::toQString(overridePath), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Environment"));
    return group.readEntry(QStringLiteral("GDK_SCALE"), QString());
}

void ScalingController::writeScale(const std::filesystem::path &overridePath, const QString &scale)
{
    KConfig config(PathUtils::toQString(overridePath), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Environment"));
    if (scale.isEmpty())
        group.deleteEntry(QStringLiteral("GDK_SCALE"));
    else
        group.writeEntry(QStringLiteral("GDK_SCALE"), scale);
    config.sync();
}
