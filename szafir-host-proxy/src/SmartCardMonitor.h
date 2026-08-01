#pragma once

#include <QObject>
#include <QHash>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>

#include <filesystem>

#include <winscard.h>

class ComponentDownloader;

class SmartCardMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(bool cardPresent READ cardPresent NOTIFY statusChanged)
    Q_PROPERTY(QVariantList readers READ readers NOTIFY statusChanged)

public:
    enum class Mode { Live, Empty, Mock };

    explicit SmartCardMonitor(Mode mode = Mode::Live,
                              ComponentDownloader *componentDownloader = nullptr,
                              QObject *parent = nullptr,
                              bool debugLogging = false);
    ~SmartCardMonitor() override;

    bool available() const { return m_available; }
    bool cardPresent() const { return m_cardPresent; }
    // Each reader map always contains name/present and optional diagnostic or
    // provider metadata fields. Unsupported values are represented by empty
    // strings so QML can render them as unavailable.
    QVariantList readers() const { return m_readers; }

    Q_INVOKABLE void requestDetails(const QString &readerName);
    Q_INVOKABLE void retryDetails(const QString &readerName);

Q_SIGNALS:
    void statusChanged();

private:
    bool ensureContext();
    void releaseContext();
    void poll();
    void updateState(bool available, bool cardPresent, const QVariantList &readers);
    void onProviderComponentChanged(const QString &id);
    void startProbe(const QString &readerName,
                    const QString &cardKey,
                    int generation,
                    const std::filesystem::path &providerPath);
    void finishProbe(int exitCode, QProcess::ExitStatus exitStatus);
    void failProbe(const QString &state, const QString &error);
    void probeTimedOut();
    void retryProbe();
    void debugLog(const QString &message) const;

    void updateReaderDetails(const QString &readerName, const QVariantMap &details);
    QVariantMap currentReader(const QString &readerName) const;
    QString cardKey(const QVariantMap &reader) const;
    int readerGeneration(const QString &readerName, const QString &key) const;
    void invalidateProviderDetails();

    SCARDCONTEXT m_context = 0;
    QTimer m_timer;
    bool m_available = false;
    bool m_cardPresent = false;
    QVariantList m_readers;
    Mode m_mode = Mode::Live;
    ComponentDownloader *m_componentDownloader = nullptr;
    bool m_debugLogging = false;
    QProcess *m_probeProcess = nullptr;
    QTimer m_probeTimeout;
    QTimer m_probeRetryTimer;
    QString m_probeReaderName;
    QString m_probeCardKey;
    std::filesystem::path m_probeProviderPath;
    int m_probeGeneration = 0;
    int m_probeRetryCount = 0;
    bool m_probeTimedOut = false;
    QHash<QString, int> m_readerGenerations;
    QHash<QString, QString> m_activeCardKeys;
    QHash<QString, QVariantMap> m_providerDetails;
};
