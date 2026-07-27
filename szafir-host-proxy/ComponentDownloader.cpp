#include "ComponentDownloader.h"
#include "AppSettings.h"
#include "SecureNetwork.h"
#include "config.h"

#include <KLocalizedString>
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamWriter>

#include <algorithm>

namespace {

struct ComponentDescriptor {
    const char *id;
    const char *type;
    const char *providerName;
    bool required;
    bool suggested;
};

constexpr ComponentDescriptor kRegistry[] = {
    { "szafirhost-installer", "installer", "",                 true,  false },
    { "libccgraphite",        "library",   "libCCGraphiteP11", false, true  },
};

QString localizedComponentName(const QString &id)
{
    if (id == QLatin1String("szafirhost-installer"))
        return i18n("SzafirHost Runtime");
    if (id == QLatin1String("libccgraphite"))
        return i18n("libCCGraphite Cryptographic Provider");
    return id;
}

QJsonObject loadComponentsState()
{
    QFile f(PathUtils::toQString(componentStatePath()));
    if (f.open(QIODevice::ReadOnly)) {
        return QJsonDocument::fromJson(f.readAll()).object();
    }
    return {};
}

void saveComponentsState(const QJsonObject &state)
{
    std::error_code ec;
    std::filesystem::create_directories(componentStatePath().parent_path(), ec);
    QSaveFile f(PathUtils::toQString(componentStatePath()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(state).toJson());
        f.commit();
    }
}

QString computeFileSha256(const std::filesystem::path &path)
{
    QFile f(PathUtils::toQString(path));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (hash.addData(&f)) {
        return QString::fromLatin1(hash.result().toHex());
    }
    return {};
}

std::filesystem::path finalComponentPath(const QString &filename)
{
    return verifiedComponentsPath() / filename.toStdString();
}

bool cleanupDownloadDir()
{
    std::error_code ec;
    const std::filesystem::path downloadDir = componentDownloadPath();
    if (!std::filesystem::exists(downloadDir, ec))
        return true;

    for (const auto &entry : std::filesystem::directory_iterator(downloadDir, ec)) {
        if (ec)
            break;

        std::error_code removeEc;
        std::filesystem::remove_all(entry.path(), removeEc);
    }

    return !ec;
}

bool installComponent(const std::filesystem::path &sourcePath,
                      const QString &filename,
                      bool move,
                      std::filesystem::path *installedPathOut)
{
    std::error_code ec;
    const std::filesystem::path destination = finalComponentPath(filename);
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec)
        return false;

    if (sourcePath == destination) {
        if (installedPathOut)
            *installedPathOut = destination;
        return true;
    }

    if (move) {
        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(sourcePath, destination, ec);
        if (!ec) {
            if (installedPathOut)
                *installedPathOut = destination;
            return true;
        }
        ec.clear();
    }

    std::filesystem::copy_file(sourcePath, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        return false;

    if (move) {
        std::error_code removeEc;
        std::filesystem::remove(sourcePath, removeEc);
    }

    if (installedPathOut)
        *installedPathOut = destination;
    return true;
}

} // namespace

ComponentDownloader::ComponentDownloader(QObject *parent)
    : QAbstractListModel(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    loadInstalledState();
}

int ComponentDownloader::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_components.size();
}

QVariant ComponentDownloader::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_components.size())
        return {};

    const ComponentEntry &entry = m_components[index.row()];
    switch (role) {
    case ComponentRole:
        return QVariant::fromValue(entry.info);
    case StateRole:
        return static_cast<int>(entry.state);
    case EnabledRole:
        return entry.enabled;
    case PresentRole:
        return entry.present;
    case BytesReceivedRole:
        return entry.bytesReceived;
    case DownloadableRole:
        return entry.downloadable();
    default:
        return {};
    }
}

QHash<int, QByteArray> ComponentDownloader::roleNames() const
{
    return {
        {ComponentRole, "component"},
        {StateRole, "state"},
        {EnabledRole, "enabled"},
        {PresentRole, "present"},
        {BytesReceivedRole, "bytesReceived"},
        {DownloadableRole, "downloadable"},
    };
}

std::span<const ComponentDownloader::ComponentEntry> ComponentDownloader::components() const
{
    return {m_components.data(), static_cast<std::size_t>(m_components.size())};
}

void ComponentDownloader::loadInstalledState()
{
    cleanupDownloadDir();

    QJsonObject stateObj = loadComponentsState();
    QJsonObject componentsState = stateObj[QStringLiteral("components")].toObject();
    bool stateChanged = false;

    QList<ComponentEntry> loadedComponents;

    for (const auto &desc : kRegistry) {
        ComponentEntry entry;
        entry.info.id           = QString::fromLatin1(desc.id);
        entry.info.type         = QString::fromLatin1(desc.type);
        entry.info.providerName = QString::fromLatin1(desc.providerName);
        entry.info.name         = localizedComponentName(entry.info.id);
        entry.info.required     = desc.required;
        entry.info.suggested    = desc.suggested;
        entry.enabled           = desc.required || desc.suggested;

        // Restore location data from persisted state.
        QJsonObject compState = componentsState[entry.info.id].toObject();
        const bool isDynamicInstall = compState.contains(QStringLiteral("urlHash"));
        const QString stateHash = compState[QStringLiteral("sha256")].toString();

        if (compState.contains(QStringLiteral("sha256")) && !stateHash.isEmpty()) {
            entry.info.url      = compState[QStringLiteral("url")].toString();
            entry.info.version  = compState[QStringLiteral("version")].toString();
            entry.info.filename = compState[QStringLiteral("filename")].toString();
            entry.info.hash     = stateHash;
            entry.info.hashLabel = entry.info.type == QLatin1String("installer")
                ? i18n("SHA256 (installer):") : i18n("SHA256:");

            if (isDynamicInstall) {
                entry.urlHash = compState[QStringLiteral("urlHash")].toString();
                entry.trustFirstDownload = true;
            }

            // Verify the file is still on disk.
            const std::filesystem::path verifiedPath =
                PathUtils::toFsPath(compState[QStringLiteral("path")].toString());
            std::error_code ec;
            if (std::filesystem::exists(verifiedPath, ec)
                && verifiedPath.parent_path() == verifiedComponentsPath()) {
                entry.present = true;
                entry.state = Done;
                entry.verifiedPath = verifiedPath;
                qDebug() << "ComponentDownloader: component already present & verified:" << entry.info.id;
            }
        }

        // Fallback: look for the file on disk even without valid state.
        if (!entry.present && !entry.info.filename.isEmpty()) {
            std::filesystem::path foundPath;
            std::error_code ec;

            const std::filesystem::path verifiedPath = finalComponentPath(entry.info.filename);
            if (std::filesystem::exists(verifiedPath, ec)) {
                foundPath = verifiedPath;
            } else {
                const std::filesystem::path downloaded = downloadedExtraPath() / entry.info.filename.toStdString();
                if (std::filesystem::exists(downloaded, ec))
                    foundPath = downloaded;
            }

            if (!foundPath.empty() && !entry.info.hash.isEmpty()) {
                qDebug() << "ComponentDownloader: verifying SHA256 for newly found component" << entry.info.id;
                const QString actualHash = computeFileSha256(foundPath);
                if (actualHash == entry.info.hash) {
                    std::filesystem::path promotedPath;
                    if (installComponent(foundPath, entry.info.filename, false, &promotedPath)) {
                        entry.present = true;
                        entry.state = Done;
                        entry.verifiedPath = promotedPath;
                        QJsonObject newState;
                        newState[QStringLiteral("sha256")] = entry.info.hash;
                        newState[QStringLiteral("path")] = PathUtils::toQString(promotedPath);
                        componentsState[entry.info.id] = newState;
                        stateChanged = true;
                    }
                } else {
                    qWarning() << "ComponentDownloader: checksum mismatch for" << entry.info.id
                               << "at" << PathUtils::toQString(foundPath);
                }
            }
        }

        if (!entry.present && !entry.downloadable())
            entry.state = Missing;

        qDebug() << "ComponentDownloader: loaded component" << entry.info.id
                 << "required:" << entry.info.required
                 << "suggested:" << entry.info.suggested
                 << "enabled:" << entry.enabled
                 << "present:" << entry.present;

        loadedComponents.append(entry);
    }

    if (stateChanged) {
        stateObj[QStringLiteral("components")] = componentsState;
        saveComponentsState(stateObj);
    }

    std::stable_sort(loadedComponents.begin(), loadedComponents.end(),
                     [](const ComponentEntry &a, const ComponentEntry &b) {
        if (a.present != b.present)
            return a.present && !b.present;
        return false;
    });

    beginResetModel();
    m_components = std::move(loadedComponents);
    endResetModel();

    emitSummaryStateChanged();
    writeExternalProvidersXml();

    qDebug() << "ComponentDownloader: loaded" << m_components.size() << "component(s) from state";
}

void ComponentDownloader::discoverComponents()
{
    if (m_discovering)
        return;

    m_discovering = true;
    Q_EMIT isDiscoveringChanged();

    QNetworkRequest request = SecureNetwork::makeSecureRequest(QUrl{QString::fromLatin1(kDiscoveryUrl)});
    QNetworkReply *reply = m_networkManager->get(request);
    SecureNetwork::attachSslAbort(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_discovering = false;
        Q_EMIT isDiscoveringChanged();

        DiscoveryResult result;
        if (reply->error() == QNetworkReply::NoError) {
            constexpr int kMaxPageBytes = 4 * 1024 * 1024;
            result = parseDiscoveryPage(reply->read(kMaxPageBytes));
        } else {
            qWarning() << "ComponentDownloader: discovery failed:" << reply->errorString();
        }

        Q_EMIT discoveryFinished(result);
    });
}

void ComponentDownloader::applyDiscovery(const DiscoveryResult &result)
{
    auto apply = [this](const DiscoveredComponent &disc, const QString &id) {
        if (!disc.valid)
            return;
        for (auto &e : m_components) {
            if (e.info.id != id)
                continue;
            e.info.url = disc.url.toString();
            e.info.version = disc.version;
            e.info.filename = disc.filename;
            e.urlHash = disc.urlHash;
            e.info.hash.clear();
            e.trustFirstDownload = true;
            e.info.size = 0;
            if (!e.present)
                e.state = Pending;
            break;
        }
    };

    apply(result.runtime, QStringLiteral("szafirhost-installer"));
    apply(result.library, QStringLiteral("libccgraphite"));

    emitSummaryStateChanged();
}

bool ComponentDownloader::needsDiscovery() const
{
    for (const auto &e : m_components) {
        if (e.info.required && !e.present && e.info.url.isEmpty())
            return true;
    }
    return false;
}

QList<Component> ComponentDownloader::presentDisplayEntries() const
{
    QList<Component> result;
    for (const ComponentEntry &e : m_components)
        if (e.present)
            result.append(static_cast<const Component &>(e.info));
    return result;
}


bool ComponentDownloader::allRequiredComplete() const
{
    for (const ComponentEntry &e : m_components) {
        if (e.info.required && e.state != Done)
            return false;
    }
    return true;
}

bool ComponentDownloader::hasDownloadableComponents() const
{
    for (const ComponentEntry &e : m_components) {
        if (!e.downloadable())
            continue;
        if (!e.present && (e.info.required || e.info.suggested))
            return true;
    }
    return false;
}

bool ComponentDownloader::canStartDownload() const
{
    for (const ComponentEntry &e : m_components) {
        if (e.enabled && !e.present && e.downloadable())
            return true;
    }
    return false;
}

bool ComponentDownloader::hasMissingComponents() const
{
    for (const ComponentEntry &e : m_components) {
        if (e.info.required && e.state == Missing)
            return true;
    }
    return false;
}

void ComponentDownloader::emitRowChanged(int row, const QList<int> &roles)
{
    if (row < 0 || row >= m_components.size())
        return;
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx, roles);
}

void ComponentDownloader::emitSummaryStateChanged()
{
    Q_EMIT summaryStateChanged();
}

void ComponentDownloader::setComponentEnabled(const QString &id, bool enabled)
{
    for (int i = 0; i < m_components.size(); ++i) {
        if (m_components[i].info.id == id && !m_components[i].info.required) {
            qDebug() << "ComponentDownloader: setComponentEnabled" << id << "->"
                     << (enabled ? "enabled" : "disabled");
            if (m_components[i].enabled == enabled)
                return;
            m_components[i].enabled = enabled;
            emitRowChanged(i, {EnabledRole});
            emitSummaryStateChanged();
            return;
        }
    }
    qWarning() << "ComponentDownloader: setComponentEnabled - component not found or required:" << id;
}

void ComponentDownloader::startDownloads()
{
    if (m_downloading) {
        qDebug() << "ComponentDownloader: startDownloads called while already downloading, ignoring";
        return;
    }

    qDebug() << "ComponentDownloader: startDownloads";
    m_downloading = true;
    Q_EMIT isDownloadingChanged();

    // Mark enabled but not-done components as Pending
    bool anyStateChanged = false;
    for (int i = 0; i < m_components.size(); ++i) {
        ComponentEntry &e = m_components[i];
        const ComponentState previousState = e.state;
        if (!e.downloadable())
            continue; // Not downloadable — skip
        if (e.enabled && e.state != Done) {
            qDebug() << "ComponentDownloader: queuing" << e.info.id << "for download";
            e.state = Pending;
        } else if (!e.enabled && e.state != Done) {
            qDebug() << "ComponentDownloader: skipping" << e.info.id << "(not enabled)";
            e.state = Skipped;
        }

        if (previousState != e.state) {
            anyStateChanged = true;
            emitRowChanged(i, {StateRole});
        }
    }
    if (anyStateChanged)
        emitSummaryStateChanged();

    // Ensure download directory exists
    std::error_code ec;
    std::filesystem::create_directories(componentDownloadPath(), ec);
    if (ec) {
        qWarning() << "Failed to create download directory:" << ec.message().c_str();
        m_downloading = false;
        Q_EMIT isDownloadingChanged();
        return;
    }

    ec.clear();
    std::filesystem::create_directories(verifiedComponentsPath(), ec);
    if (ec) {
        qWarning() << "Failed to create components directory:" << ec.message().c_str();
        m_downloading = false;
        Q_EMIT isDownloadingChanged();
        return;
    }

    m_currentDownloadIndex = -1;
    downloadNext();
}

void ComponentDownloader::downloadNext()
{
    // Find next component that needs downloading
    m_currentDownloadIndex++;
    while (m_currentDownloadIndex < m_components.size()) {
        const ComponentEntry &e = m_components[m_currentDownloadIndex];
        if (e.enabled && e.state == Pending)
            break;
        m_currentDownloadIndex++;
    }

    if (m_currentDownloadIndex >= m_components.size()) {
        qDebug() << "ComponentDownloader: all downloads complete";
        m_downloading = false;
        Q_EMIT isDownloadingChanged();
        emitSummaryStateChanged();
        Q_EMIT allDownloadsComplete();
        return;
    }

    ComponentEntry &entry = m_components[m_currentDownloadIndex];
    qDebug() << "ComponentDownloader: starting download for" << entry.info.id
             << "url:" << entry.info.url
             << "expected size:" << entry.info.size << "bytes";
    entry.state = Downloading;
    entry.bytesReceived = 0;
    emitRowChanged(m_currentDownloadIndex, {StateRole, BytesReceivedRole});
    emitSummaryStateChanged();

    const std::filesystem::path downloadSubdir =
        componentDownloadPath() / QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    std::error_code ec;
    std::filesystem::create_directories(downloadSubdir, ec);
    if (ec) {
        qWarning() << "ComponentDownloader: failed to create download subdirectory:" << ec.message().c_str();
        entry.state = Error;
        emitRowChanged(m_currentDownloadIndex, {StateRole});
        emitSummaryStateChanged();
        Q_EMIT downloadFailed(entry.info.id, QStringLiteral("Failed to create download subdirectory"));
        downloadNext();
        return;
    }

    QString filename = entry.info.filename;
    if (filename.isEmpty())
        filename = QUrl(entry.info.url).fileName();
    const std::filesystem::path destPath = downloadSubdir / filename.toStdString();
    m_currentDownloadPath = destPath;
    m_outputFile = new QFile(PathUtils::toQString(destPath), this);
    if (!m_outputFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open file for writing:" << PathUtils::toQString(destPath);
        entry.state = Error;
        emitRowChanged(m_currentDownloadIndex, {StateRole});
        emitSummaryStateChanged();
        Q_EMIT downloadFailed(entry.info.id, QStringLiteral("Failed to open file for writing"));
        delete m_outputFile;
        m_outputFile = nullptr;
        m_currentDownloadPath.clear();
        std::filesystem::remove_all(downloadSubdir, ec);
        downloadNext();
        return;
    }

    m_hashAccumulator.clear();
    m_hashAccumulator.reserve(entry.info.size);

    m_redirectHops = 0;
    startRequest(QUrl(entry.info.url));
}

void ComponentDownloader::startRequest(const QUrl &url)
{
    QNetworkRequest request = SecureNetwork::makeManualRedirectRequest(url);
    m_currentReply = m_networkManager->get(request);
    SecureNetwork::attachSslAbort(m_currentReply);

    connect(m_currentReply, &QNetworkReply::metaDataChanged, this, [this]() {
        if (m_currentDownloadIndex < 0 || m_currentDownloadIndex >= m_components.size())
            return;
        ComponentEntry &e = m_components[m_currentDownloadIndex];
        if (e.info.size == 0 && m_currentReply) {
            const int status = m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status < 200 || status >= 300)
                return;
            const QVariant len = m_currentReply->header(QNetworkRequest::ContentLengthHeader);
            if (len.isValid()) {
                e.info.size = len.toLongLong();
                emitRowChanged(m_currentDownloadIndex, {ComponentRole});
            }
        }
    });

    connect(m_currentReply, &QNetworkReply::readyRead, this, &ComponentDownloader::onReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &ComponentDownloader::onDownloadFinished);
}

void ComponentDownloader::onReadyRead()
{
    if (!m_outputFile || !m_currentReply
        || m_currentDownloadIndex < 0 || m_currentDownloadIndex >= m_components.size())
        return;

    // Redirect responses are handled in onDownloadFinished; their bodies are
    // not part of the download.
    const int status = m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400)
        return;

    const QByteArray data = m_currentReply->readAll();
    if (data.isEmpty())
        return;

    m_outputFile->write(data);
    m_hashAccumulator.append(data);

    ComponentEntry &entry = m_components[m_currentDownloadIndex];
    entry.bytesReceived += data.size();
    if (entry.info.size > 0 && (entry.bytesReceived % (1024 * 1024)) < data.size())
        qDebug() << "ComponentDownloader:" << entry.info.id
                 << "progress:" << entry.bytesReceived << "/" << entry.info.size << "bytes";
    emitRowChanged(m_currentDownloadIndex, {BytesReceivedRole});
}

void ComponentDownloader::onDownloadFinished()
{
    if (m_currentDownloadIndex < 0 || m_currentDownloadIndex >= m_components.size())
        return;

    ComponentEntry &entry = m_components[m_currentDownloadIndex];

    // Handle redirects manually: re-validate (and possibly rewrite) each hop
    // before following it, keeping the output file open across hops.
    if (m_currentReply && m_currentReply->error() == QNetworkReply::NoError) {
        const QUrl target = m_currentReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (target.isValid()) {
            QNetworkReply *redirectReply = m_currentReply;
            m_currentReply = nullptr;
            redirectReply->deleteLater();

            const QUrl resolved =
                SecureNetwork::rewriteRedirectTarget(redirectReply->url().resolved(target));

            QString redirectError;
            if (++m_redirectHops > kMaxRedirectHops)
                redirectError = QStringLiteral("Too many redirects");
            else if (resolved.scheme() != QLatin1String("https")
                     || !SecureNetwork::isAllowedHost(resolved))
                redirectError = QStringLiteral("Redirect to untrusted URL: ") + resolved.toString();

            if (redirectError.isEmpty()) {
                qDebug() << "ComponentDownloader: following redirect for" << entry.info.id
                         << "to" << resolved.toString();
                startRequest(resolved);
                return;
            }

            qWarning() << "Download failed for" << entry.info.id << ":" << redirectError;
            if (m_outputFile) {
                m_outputFile->close();
                delete m_outputFile;
                m_outputFile = nullptr;
            }
            const std::filesystem::path downloadedPath = m_currentDownloadPath;
            m_currentDownloadPath.clear();
            m_hashAccumulator.clear();
            entry.state = Error;
            emitRowChanged(m_currentDownloadIndex, {StateRole});
            emitSummaryStateChanged();
            Q_EMIT downloadFailed(entry.info.id, redirectError);
            if (!downloadedPath.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(downloadedPath.parent_path(), ec);
            }
            downloadNext();
            return;
        }
    }

    std::filesystem::path downloadedPath;
    if (m_outputFile) {
        downloadedPath = m_currentDownloadPath;
        m_outputFile->close();
        delete m_outputFile;
        m_outputFile = nullptr;
    }
    m_currentDownloadPath.clear();

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Download failed for" << entry.info.id << ":" << reply->errorString();
        entry.state = Error;
        emitRowChanged(m_currentDownloadIndex, {StateRole});
        emitSummaryStateChanged();
        Q_EMIT downloadFailed(entry.info.id, reply->errorString());

        if (!downloadedPath.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(downloadedPath.parent_path(), ec);
        }

        downloadNext();
        return;
    }

    // Verify SHA256
    entry.state = Verifying;
    emitRowChanged(m_currentDownloadIndex, {StateRole});
    emitSummaryStateChanged();

    const QByteArray hash = QCryptographicHash::hash(m_hashAccumulator, QCryptographicHash::Sha256).toHex();
    m_hashAccumulator.clear();

    const QString computedHash = QString::fromLatin1(hash);

    if (entry.trustFirstDownload && entry.info.hash.isEmpty()) {
        entry.info.hash = computedHash;
        qDebug() << "ComponentDownloader: trust-on-first-download, adopted hash" << computedHash
                 << "for" << entry.info.id;
    } else if (computedHash != entry.info.hash) {
        qWarning() << "SHA256 mismatch for" << entry.info.id
                   << "expected:" << entry.info.hash
                   << "got:" << computedHash;
        entry.state = Error;
        emitRowChanged(m_currentDownloadIndex, {StateRole});
        emitSummaryStateChanged();
        Q_EMIT downloadFailed(entry.info.id, QStringLiteral("SHA256 checksum mismatch"));

        if (!downloadedPath.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(downloadedPath.parent_path(), ec);
        }

        downloadNext();
        return;
    }

    qDebug() << "ComponentDownloader: SHA256 verified for" << entry.info.id;

    std::filesystem::path promotedPath;
    if (!installComponent(downloadedPath, entry.info.filename, true, &promotedPath)) {
        qWarning() << "ComponentDownloader: failed to promote verified download for" << entry.info.id;
        if (!downloadedPath.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(downloadedPath.parent_path(), ec);
        }
        entry.state = Error;
        emitRowChanged(m_currentDownloadIndex, {StateRole});
        emitSummaryStateChanged();
        Q_EMIT downloadFailed(entry.info.id, QStringLiteral("Failed to move verified file into component store"));
        downloadNext();
        return;
    }

    if (!downloadedPath.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(downloadedPath.parent_path(), ec);
    }

    entry.state = Done;
    entry.present = true;

    // Clean up the previous version's file if the filename changed (e.g. versioned .so).
    const std::filesystem::path oldVerifiedPath = entry.verifiedPath;
    entry.verifiedPath = promotedPath;
    if (!oldVerifiedPath.empty() && oldVerifiedPath != promotedPath
        && oldVerifiedPath.parent_path() == verifiedComponentsPath()) {
        std::error_code removeEc;
        std::filesystem::remove(oldVerifiedPath, removeEc);
        if (!removeEc)
            qDebug() << "ComponentDownloader: removed old component file" << PathUtils::toQString(oldVerifiedPath);
    }

    // Save to local state
    QJsonObject stateObj = loadComponentsState();
    QJsonObject componentsState = stateObj[QStringLiteral("components")].toObject();
    
    QJsonObject newState;
    newState[QStringLiteral("sha256")] = entry.info.hash;
    newState[QStringLiteral("path")] = PathUtils::toQString(promotedPath);
    if (!entry.urlHash.isEmpty())
        newState[QStringLiteral("urlHash")] = entry.urlHash;
    if (!entry.info.version.isEmpty())
        newState[QStringLiteral("version")] = entry.info.version;
    if (!entry.info.url.isEmpty())
        newState[QStringLiteral("url")] = entry.info.url;
    if (!entry.info.filename.isEmpty())
        newState[QStringLiteral("filename")] = entry.info.filename;
    componentsState[entry.info.id] = newState;
    
    stateObj[QStringLiteral("components")] = componentsState;
    saveComponentsState(stateObj);

    emitRowChanged(m_currentDownloadIndex, {StateRole, PresentRole});
    emitSummaryStateChanged();
    writeExternalProvidersXml();
    downloadNext();
}

void ComponentDownloader::writeExternalProvidersXml()
{
    QByteArray buffer;
    {
        QXmlStreamWriter xml(&buffer);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeStartElement(QStringLiteral("Providers"));

        int count = 0;
        for (const ComponentEntry &e : m_components) {
            if (e.info.type == QStringLiteral("library")
                    && e.present
                    && !e.verifiedPath.empty()
                    && !e.info.providerName.isEmpty()) {
                xml.writeStartElement(QStringLiteral("Provider"));
                xml.writeTextElement(QStringLiteral("Name"), e.info.providerName);
                xml.writeTextElement(QStringLiteral("URI"), QStringLiteral("file:") + PathUtils::toQString(e.verifiedPath));
                xml.writeEndElement(); // Provider
                ++count;
            }
        }

        xml.writeEndElement(); // Providers
        xml.writeEndDocument();
    }

    const std::filesystem::path xmlPath = externalProvidersXmlPath();
    QFile file(PathUtils::toQString(xmlPath));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ComponentDownloader: failed to open external_providers.xml for writing:" << file.errorString();
        return;
    }
    file.write(buffer);
    file.close();
    qDebug() << "ComponentDownloader: wrote external_providers.xml";
}

bool ComponentDownloader::overrideComponentSource(const QString &id, const QUrl &url,
                                                   const QString &version, const QString &urlHash,
                                                   const QString &filename)
{
    if (m_downloading) {
        qWarning() << "ComponentDownloader: overrideComponentSource refused while downloading";
        return false;
    }

    for (int i = 0; i < m_components.size(); ++i) {
        ComponentEntry &e = m_components[i];
        if (e.info.id != id)
            continue;

        qDebug() << "ComponentDownloader: overriding source for" << id
                 << "url:" << url.toString() << "version:" << version << "urlHash:" << urlHash;

        e.info.url = url.toString();
        e.info.version = version;
        e.info.hash.clear();
        e.urlHash = urlHash;
        e.trustFirstDownload = true;
        e.present = false;
        e.state = Pending;
        e.bytesReceived = 0;
        e.info.size = 0;
        if (!filename.isEmpty())
            e.info.filename = filename;

        emitRowChanged(i, {ComponentRole, StateRole, PresentRole, BytesReceivedRole, DownloadableRole});
        emitSummaryStateChanged();
        return true;
    }

    qWarning() << "ComponentDownloader: overrideComponentSource - component not found:" << id;
    return false;
}

bool ComponentDownloader::adoptLocalFile(const QString &id, const QString &sourcePath,
                                          const QString &sha256)
{
    if (m_downloading) {
        qWarning() << "ComponentDownloader: adoptLocalFile refused while downloading";
        return false;
    }

    for (int i = 0; i < m_components.size(); ++i) {
        ComponentEntry &e = m_components[i];
        if (e.info.id != id)
            continue;

        const std::filesystem::path src = PathUtils::toFsPath(sourcePath);
        std::filesystem::path promotedPath;
        if (!installComponent(src, e.info.filename, false, &promotedPath)) {
            qWarning() << "ComponentDownloader: adoptLocalFile - failed to copy" << sourcePath;
            return false;
        }

        e.info.hash = sha256;
        e.info.url.clear();
        e.info.version.clear();
        e.urlHash.clear();
        e.trustFirstDownload = false;
        e.present = true;
        e.state = Done;
        e.verifiedPath = promotedPath;

        QJsonObject stateObj = loadComponentsState();
        QJsonObject componentsState = stateObj[QStringLiteral("components")].toObject();
        QJsonObject newState;
        newState[QStringLiteral("sha256")] = sha256;
        newState[QStringLiteral("path")] = PathUtils::toQString(promotedPath);
        componentsState[id] = newState;
        stateObj[QStringLiteral("components")] = componentsState;
        saveComponentsState(stateObj);

        emitRowChanged(i, {ComponentRole, StateRole, PresentRole, DownloadableRole});
        emitSummaryStateChanged();
        writeExternalProvidersXml();

        qDebug() << "ComponentDownloader: adopted local file for" << id << "at" << PathUtils::toQString(promotedPath);
        return true;
    }

    qWarning() << "ComponentDownloader: adoptLocalFile - component not found:" << id;
    return false;
}
