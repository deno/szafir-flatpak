#include "HostRuntimeController.h"
#include "AppSettings.h"

#include <QFile>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QStandardPaths>
#include <KFormat>

namespace {

qint64 directorySizeBytes(const std::filesystem::path &path)
{
    qint64 total = 0;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return 0;

    for (const auto &it : std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (it.is_regular_file(ec)) {
            total += it.file_size(ec);
        }
    }
    return total;
}

} // namespace

HostRuntimeController::HostRuntimeController(QObject *parent)
    : QObject(parent)
{}

QString HostRuntimeController::loadLicenseText()
{
    const bool isPolish = QLocale::system().language() == QLocale::Polish;
    const QString qrcPath = isPolish
        ? QStringLiteral(":/szafir_license_pl.md")
        : QStringLiteral(":/szafir_license_en.md");

    QFile f(qrcPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

std::filesystem::path HostRuntimeController::cachePath()
{
    return PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
        / ".java" / "tmp";
}

QString HostRuntimeController::cacheSize() const
{
    std::error_code ec;
    if (!std::filesystem::exists(cachePath(), ec))
        return QStringLiteral("0 B");
    return KFormat().formatByteSize(directorySizeBytes(cachePath()));
}

bool HostRuntimeController::clearCache()
{
    const std::filesystem::path path = cachePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return true;

    bool ok = true;
    for (const auto &entry : std::filesystem::directory_iterator(path, ec)) {
        std::error_code rec;
        std::filesystem::remove_all(entry.path(), rec);
        if (rec) ok = false;
    }
    return ok;
}

void HostRuntimeController::startWatching()
{
    const std::filesystem::path path = cachePath();
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    
    const QString pathString = PathUtils::toQString(path);

    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                this, &HostRuntimeController::cacheSizeChanged);
        connect(m_watcher, &QFileSystemWatcher::fileChanged,
                this, &HostRuntimeController::cacheSizeChanged);
    }

    if (!m_watcher->directories().contains(pathString))
        m_watcher->addPath(pathString);
}

void HostRuntimeController::stopWatching()
{
    if (m_watcher) {
        const QStringList watched = m_watcher->directories() + m_watcher->files();
        if (!watched.isEmpty())
            m_watcher->removePaths(watched);
    }
}
