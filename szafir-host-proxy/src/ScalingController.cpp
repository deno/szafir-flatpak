#include "ScalingController.h"
#include "AppSettings.h"

#include <QDebug>
#include <QFile>
#include <QFileSystemWatcher>

namespace {

constexpr char kGroup[] = "Environment";
constexpr char kKey[] = "GDK_SCALE";

bool isGroupLine(const QString &line, const QString &group)
{
    const QString t = line.trimmed();
    return t.size() >= 2 && t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))
        && t.mid(1, t.size() - 2).trimmed() == group;
}

// Splits "key=value" and returns true with the trimmed key/value when the line
// carries the requested key.
bool splitKeyValue(const QString &line, const QString &key, QString *value)
{
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq < 0)
        return false;
    if (line.left(eq).trimmed() != key)
        return false;
    *value = line.mid(eq + 1).trimmed();
    return true;
}

// Reads [group] key from a Flatpak-style override keyfile. Returns an empty
// string when the file, group, or key is absent.
QString readKey(const QString &path, const QString &group, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    bool inGroup = false;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine());
        if (isGroupLine(line, group)) {
            inGroup = true;
            continue;
        }
        if (line.trimmed().startsWith(QLatin1Char('['))) {
            inGroup = false;
            continue;
        }
        if (inGroup) {
            QString value;
            if (splitKeyValue(line, key, &value))
                return value;
        }
    }
    return {};
}

// Rewrites [group] key in content, preserving every other line. An empty value
// deletes the key. The group is created when missing.
QString setKeyInContent(const QString &content, const QString &group, const QString &key,
                        const QString &value)
{
    const QString newLine = key + QLatin1Char('=') + value;

    QStringList lines = content.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    int groupIdx = -1;
    int keyIdx = -1;
    int nextGroupIdx = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (isGroupLine(line, group)) {
            groupIdx = i;
            continue;
        }
        if (line.trimmed().startsWith(QLatin1Char('['))) {
            if (groupIdx >= 0 && keyIdx < 0 && nextGroupIdx < 0)
                nextGroupIdx = i;
            continue;
        }
        if (groupIdx >= 0 && keyIdx < 0) {
            QString existing;
            if (splitKeyValue(line, key, &existing))
                keyIdx = i;
        }
    }

    if (keyIdx >= 0) {
        if (value.isEmpty())
            lines.removeAt(keyIdx);
        else
            lines[keyIdx] = newLine;
    } else if (groupIdx >= 0) {
        if (value.isEmpty())
            return content; // nothing to remove
        int insertAt = (nextGroupIdx >= 0) ? nextGroupIdx : lines.size();
        if (nextGroupIdx >= 0 && insertAt > groupIdx + 1 && lines[insertAt - 1].trimmed().isEmpty())
            --insertAt;
        lines.insert(insertAt, newLine);
    } else {
        if (value.isEmpty())
            return content; // nothing to remove
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
            lines.append(QString());
        lines.append(QLatin1Char('[') + group + QLatin1Char(']'));
        lines.append(newLine);
    }

    QString out = lines.join(QLatin1Char('\n'));
    if (!out.endsWith(QLatin1Char('\n')))
        out += QLatin1Char('\n');
    return out;
}

} // namespace

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
        // Directory may not exist yet. Creation needs MAKE_DIR, which Landlock
        // Phase 2 drops; if it fails the watcher simply stays inactive.
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
    return readKey(PathUtils::toQString(overridePath), QLatin1String(kGroup), QLatin1String(kKey));
}

void ScalingController::writeScale(const std::filesystem::path &overridePath, const QString &scale)
{
    const QString path = PathUtils::toQString(overridePath);

    QFile f(path);
    QString content;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        content = QString::fromUtf8(f.readAll());
        f.close();
    }

    const QString updated =
        setKeyInContent(content, QLatin1String(kGroup), QLatin1String(kKey), scale);

    // In-place rewrite via O_TRUNC: needs only WRITE_FILE|TRUNCATE on the file,
    // not MAKE_REG on the directory (which Landlock Phase 2 drops).
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "ScalingController: cannot open for writing" << path << ":" << f.errorString();
        return;
    }
    f.write(updated.toUtf8());
    f.close();
}
