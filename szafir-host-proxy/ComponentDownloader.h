#pragma once

#include "Component.h"
#include "ComponentDiscovery.h"

#include <QAbstractListModel>
#include <QUrl>

#include <filesystem>
#include <span>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

struct DownloadableComponent : Component
{
    Q_GADGET
    Q_PROPERTY(QString id           MEMBER id)
    Q_PROPERTY(QString type         MEMBER type)
    Q_PROPERTY(QString providerName MEMBER providerName)
    Q_PROPERTY(QString filename     MEMBER filename)
    Q_PROPERTY(QString url          MEMBER url)
    Q_PROPERTY(qint64  size         MEMBER size)
    Q_PROPERTY(qint64  estimatedSize MEMBER estimatedSize)
    Q_PROPERTY(bool    required     MEMBER required)
    Q_PROPERTY(bool    suggested    MEMBER suggested)
public:
    // name, subtitle, version, hashLabel, hash come from Component.
    QString id;
    QString type;         // "installer", "library", etc.
    QString providerName; // for type=="library": Name entry in external_providers.xml
    QString filename;
    QString url;
    qint64 size = 0;
    qint64 estimatedSize = 0;
    bool required = false;
    bool suggested = false;
};
Q_DECLARE_METATYPE(DownloadableComponent)

class ComponentDownloader : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool isDownloading READ isDownloading NOTIFY isDownloadingChanged)
    Q_PROPERTY(bool isDiscovering READ isDiscovering NOTIFY isDiscoveringChanged)
    Q_PROPERTY(bool allRequiredComplete READ allRequiredComplete NOTIFY summaryStateChanged)
    Q_PROPERTY(bool canStartDownload READ canStartDownload NOTIFY summaryStateChanged)
    Q_PROPERTY(bool hasMissingComponents READ hasMissingComponents NOTIFY summaryStateChanged)
    Q_PROPERTY(bool hasBrokenComponents READ hasMissingComponents NOTIFY summaryStateChanged)

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

    struct ComponentEntry {
        DownloadableComponent info;
        ComponentState state = Pending;
        bool enabled = true;
        bool present = false;
        bool trustFirstDownload = false;
        QString urlHash;
        qint64 bytesReceived = 0;
        std::filesystem::path verifiedPath;

        bool downloadable() const
        {
            return !info.url.isEmpty() && (!info.hash.isEmpty() || trustFirstDownload);
        }
    };

    enum Role {
        ComponentRole = Qt::UserRole + 1,
        StateRole,
        EnabledRole,
        PresentRole,
        BytesReceivedRole,
        DownloadableRole,
    };
    Q_ENUM(Role)

    explicit ComponentDownloader(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    std::span<const ComponentEntry> components() const;
    QList<Component> presentDisplayEntries() const;
    bool isDownloading() const { return m_downloading; }
    bool isDiscovering() const { return m_discovering; }
    bool allRequiredComplete() const;
    bool hasDownloadableComponents() const;
    bool canStartDownload() const;
    bool hasMissingComponents() const;
    bool needsDiscovery() const;

    void discoverComponents();
    void applyDiscovery(const DiscoveryResult &result);
    void fetchRemoteSizes();

    Q_INVOKABLE void setComponentEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void startDownloads();
    Q_INVOKABLE bool overrideComponentSource(const QString &id, const QUrl &url,
                                             const QString &version, const QString &urlHash,
                                             const QString &filename = {});
    Q_INVOKABLE bool adoptLocalFile(const QString &id, const QString &sourcePath,
                                    const QString &sha256);

Q_SIGNALS:
    void isDownloadingChanged();
    void isDiscoveringChanged();
    void summaryStateChanged();
    void allDownloadsComplete();
    void downloadFailed(const QString &id, const QString &errorString);
    void discoveryFinished(const DiscoveryResult &result);

private:
    void loadInstalledState();
    void downloadNext();
    void startRequest(const QUrl &url);
    void onReadyRead();
    void onDownloadFinished();
    void writeExternalProvidersXml();

    void emitRowChanged(int row, const QList<int> &roles);
    void emitSummaryStateChanged();

    static constexpr int kMaxRedirectHops = 5;

    QList<ComponentEntry> m_components;
    int m_currentDownloadIndex = -1;
    int m_redirectHops = 0;
    bool m_downloading = false;
    bool m_discovering = false;
    std::filesystem::path m_currentDownloadPath;

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QFile *m_outputFile = nullptr;
    QByteArray m_hashAccumulator;
};
