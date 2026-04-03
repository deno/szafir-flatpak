#pragma once

#ifdef BUNDLED_HOST

#include "Component.h"

#include <QAbstractListModel>

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
    bool required = false;
    bool suggested = false;
};
Q_DECLARE_METATYPE(DownloadableComponent)

class ComponentDownloader : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool isDownloading READ isDownloading NOTIFY isDownloadingChanged)
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
        qint64 bytesReceived = 0;
        std::filesystem::path verifiedPath;

        bool downloadable() const
        {
            return !info.url.isEmpty() && !info.hash.isEmpty();
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
    bool allRequiredComplete() const;
    bool hasDownloadableComponents() const;
    bool canStartDownload() const;
    bool hasMissingComponents() const;

    Q_INVOKABLE void setComponentEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void startDownloads();

Q_SIGNALS:
    void isDownloadingChanged();
    void summaryStateChanged();
    void allDownloadsComplete();
    void downloadFailed(const QString &id, const QString &errorString);

private:
    void loadManifest();
    void downloadNext();
    void onReadyRead();
    void onDownloadFinished();
    void writeExternalProvidersXml();

    void emitRowChanged(int row, const QList<int> &roles);
    void emitSummaryStateChanged();

    QList<ComponentEntry> m_components;
    int m_currentDownloadIndex = -1;
    bool m_downloading = false;
    std::filesystem::path m_currentDownloadPath;

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QFile *m_outputFile = nullptr;
    QByteArray m_hashAccumulator;
};

#endif // BUNDLED_HOST
