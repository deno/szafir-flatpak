#pragma once

#include <QObject>

#include <filesystem>

class QFileSystemWatcher;

class HostRuntimeController : public QObject
{
    Q_OBJECT

public:
    explicit HostRuntimeController(QObject *parent = nullptr);

    static QString loadLicenseText();

    Q_INVOKABLE QString cacheSize() const;
    Q_INVOKABLE bool clearCache();
    Q_INVOKABLE void startWatching();
    Q_INVOKABLE void stopWatching();

signals:
    void cacheSizeChanged();

private:
    static std::filesystem::path cachePath();

    QFileSystemWatcher *m_watcher = nullptr;
};
