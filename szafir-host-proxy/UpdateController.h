#pragma once

#include "ComponentDiscovery.h"

#include <QObject>
#include <QTimer>
#include <QUrl>

class ComponentDownloader;
class NativeMessagingService;
class QProcess;

class UpdateController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY settingsChanged)
    Q_PROPERTY(bool allowDowngrades READ allowDowngrades WRITE setAllowDowngrades NOTIFY settingsChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY runtimeInfoChanged)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY stateChanged)
    Q_PROPERTY(QString availableLibraryVersion READ availableLibraryVersion NOTIFY stateChanged)
    Q_PROPERTY(QString lastCheckTime READ lastCheckTime NOTIFY settingsChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    enum State {
        Idle,
        Checking,
        UpdateAvailable,
        Downloading,
        StoppingHosts,
        Installing,
        Error,
        UpToDate
    };
    Q_ENUM(State)

    explicit UpdateController(ComponentDownloader *downloader,
                              NativeMessagingService *service,
                              QObject *parent = nullptr);

    bool autoUpdate() const { return m_autoUpdate; }
    void setAutoUpdate(bool enabled);

    bool allowDowngrades() const { return m_allowDowngrades; }
    void setAllowDowngrades(bool allowed);

    State state() const { return m_state; }
    QString installedVersion() const;
    QString availableVersion() const { return m_availableVersion; }
    QString availableLibraryVersion() const { return m_availableLibraryVersion; }
    QString lastCheckTime() const { return m_lastCheckTime; }
    QString errorString() const { return m_errorString; }
    qreal progress() const { return m_progress; }

    Q_INVOKABLE void checkForUpdates(bool manual);
    Q_INVOKABLE void applyUpdate();
    Q_INVOKABLE void forceReinstall();
    Q_INVOKABLE void dismissOffer();
    Q_INVOKABLE void installFromFile(const QUrl &localFile);
    Q_INVOKABLE void confirmInterruption(bool proceed);

    void startScheduling();

Q_SIGNALS:
    void settingsChanged();
    void stateChanged();
    void progressChanged();
    void runtimeInfoChanged();
    void updateAvailable(const QString &newVersion, bool isDowngrade);
    void updateFinished(bool success);
    void interruptionConfirmationNeeded();

private:
    void setState(State s);
    void setProgress(qreal p);
    void setError(const QString &msg);

    void loadSettings();
    void saveSettings();

    void onDiscoveryFinished(const DiscoveryResult &result, bool manual);
    void evaluateDiscovery(const DiscoveryResult &discovered, bool manual);
    void beginInstallSequence();
    void onHostsStopped();
    void runInstaller();
    void onInstallerFinished(int exitCode);
    void finalizeUpdate(bool success);

    ComponentDownloader *m_downloader;
    NativeMessagingService *m_service;
    QTimer *m_checkTimer = nullptr;

    State m_state = Idle;
    qreal m_progress = -1;
    QString m_errorString;
    QString m_availableVersion;
    QString m_availableLibraryVersion;

    bool m_autoUpdate = false;
    bool m_allowDowngrades = true;
    QString m_lastCheckTime;
    QString m_lastDeclinedUrlHash;
    QString m_lastDeclinedLibUrlHash;
    int m_checkIntervalHours = 24;

    DiscoveredComponent m_pendingUpdate;
    DiscoveredComponent m_pendingLibrary;
    bool m_manualCheck = false;
    bool m_forceMode = false;
    bool m_deferUntilIdle = false;
    QProcess *m_installerProcess = nullptr;
};
