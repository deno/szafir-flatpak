#pragma once

#ifdef BUNDLED_HOST

#include <QObject>
#include <QVariantList>

#include <filesystem>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

struct ComponentInfo
{
    QString id;
    QString type;         // "installer", "library", etc.
    QString providerName; // for type=="library": Name entry in external_providers.xml
    QString version;      // explicit version string (from components.json)
    QString name;
    QString filename;
    QString url;
    QString sha256;
    QString libraryPath; // for type=="bundled-source": path to probe for library presence
    qint64 size = 0;
    bool required = false;
    bool suggested = false;
};

class ComponentDownloader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList components READ components NOTIFY componentsChanged)
    Q_PROPERTY(bool isDownloading READ isDownloading NOTIFY isDownloadingChanged)
    Q_PROPERTY(bool allRequiredComplete READ allRequiredComplete NOTIFY componentsChanged)
    Q_PROPERTY(bool canStartDownload READ canStartDownload NOTIFY componentsChanged)
    Q_PROPERTY(bool hasmissingComponents READ hasmissingComponents NOTIFY componentsChanged)

public:
    enum ComponentState {
        Pending,
        Downloading,
        Verifying,
        Done,
        Skipped,
        Error,
        Missing
    };
    Q_ENUM(ComponentState)

    explicit ComponentDownloader(QObject *parent = nullptr);

    QVariantList components() const;
    bool isDownloading() const { return m_downloading; }
    bool allRequiredComplete() const;
    bool hasDownloadableComponents() const;
    bool canStartDownload() const;
    bool hasmissingComponents() const;

    Q_INVOKABLE void setComponentEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void startDownloads();

Q_SIGNALS:
    void componentsChanged();
    void isDownloadingChanged();
    void allDownloadsComplete();
    void downloadFailed(const QString &id, const QString &errorString);

private:
    void loadManifest();
    void downloadNext();
    void onReadyRead();
    void onDownloadFinished();
    void writeExternalProvidersXml();

    struct ComponentEntry {
        ComponentInfo info;
        ComponentState state = Pending;
        bool enabled = true;
        bool present = false;
        qint64 bytesReceived = 0;
        QString verifiedPath; // filesystem path to the verified file on disk
    };

    QList<ComponentEntry> m_components;
    int m_currentDownloadIndex = -1;
    bool m_downloading = false;

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QFile *m_outputFile = nullptr;
    QByteArray m_hashAccumulator;
};

#endif // BUNDLED_HOST
