#ifdef BUNDLED_HOST

#include "ComponentDownloader.h"
#include "AppSettings.h"

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
    loadManifest();
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

void ComponentDownloader::loadManifest()
{
    cleanupDownloadDir();

    QFile f(QStringLiteral(":/szafir-host-proxy/components.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open components.json resource";
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error in components.json:" << err.errorString();
        return;
    }

    QJsonObject stateObj = loadComponentsState();
    QJsonObject componentsState = stateObj[QStringLiteral("components")].toObject();
    bool stateChanged = false;

    QList<ComponentEntry> loadedComponents;

    const QJsonArray arr = doc.object()[QStringLiteral("components")].toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        ComponentEntry entry;
        entry.info.id       = obj[QStringLiteral("id")].toString();
        entry.info.type     = obj[QStringLiteral("type")].toString();
        entry.info.providerName = obj[QStringLiteral("provider_name")].toString();
        entry.info.version  = obj[QStringLiteral("version")].toString();
        // "name" is either a plain string (English) or an object keyed by language
        // code. If an object, "en" is required (validated at load-time). The best
        // available locale match is resolved here and stored as the display name.
        const QJsonValue nameVal = obj[QStringLiteral("name")];
        if (nameVal.isObject()) {
            const QJsonObject namesObj = nameVal.toObject();
            const QString englishName = namesObj.value(QStringLiteral("en")).toString();
            if (englishName.isEmpty()) {
                qWarning() << "components.json: component"
                           << obj[QStringLiteral("id")].toString()
                           << "has name object without required \"en\" key — skipping";
                continue;
            }
            const QString lang = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
            const QString localizedName = namesObj.value(lang).toString();
            entry.info.name = localizedName.isEmpty() ? englishName : localizedName;
        } else {
            entry.info.name = nameVal.toString();
        }
        entry.info.filename    = obj[QStringLiteral("filename")].toString();
        entry.info.url         = obj[QStringLiteral("url")].toString();
        entry.info.hash        = obj[QStringLiteral("sha256")].toString();
        entry.info.hashLabel   = entry.info.hash.isEmpty() ? QString{}
            : (entry.info.type == QLatin1String("installer") ? QString{i18n("SHA256 (installer):")} : QString{i18n("SHA256:")});
        entry.info.size        = obj[QStringLiteral("size")].toInteger();
        entry.info.required    = obj[QStringLiteral("required")].toBool();
        entry.info.suggested   = obj[QStringLiteral("suggested")].toBool();
        entry.enabled          = entry.info.required || entry.info.suggested; // required/suggested components start enabled

        const bool downloadable = entry.downloadable();

        bool verified = false;
        QJsonObject compState = componentsState[entry.info.id].toObject();
        if (compState.contains(QStringLiteral("sha256")) && compState[QStringLiteral("sha256")].toString() == entry.info.hash) {
            QString pathStr = compState[QStringLiteral("path")].toString();
            const std::filesystem::path verifiedPath = PathUtils::toFsPath(pathStr);
            // Ensure the file is still present on disk
            std::error_code ec;
            if (std::filesystem::exists(verifiedPath, ec)
                && verifiedPath.parent_path() == verifiedComponentsPath()) {
                verified = true;
                entry.verifiedPath = verifiedPath;
            }
        }

        if (!verified) {
            // Not in state or checksum changed or file missing, let's look for it
            std::filesystem::path foundPath;
            std::error_code ec;

            // Check dedicated verified location first.
            const std::filesystem::path verifiedPath = finalComponentPath(entry.info.filename);
            if (std::filesystem::exists(verifiedPath, ec)) {
                foundPath = verifiedPath;
            } else {
                // Legacy/bundled fallback: /app/extra and old XDG data extra dir.
                const std::filesystem::path bundled = std::filesystem::path("/app/extra") / entry.info.filename.toStdString();
                if (std::filesystem::exists(bundled, ec)) {
                    foundPath = bundled;
                } else {
                    const std::filesystem::path downloaded = downloadedExtraPath() / entry.info.filename.toStdString();
                    if (std::filesystem::exists(downloaded, ec)) {
                        foundPath = downloaded;
                    }
                }
            }

            if (!foundPath.empty()) {
                // File found on disk but not in state (or invalid state). Verify its hash.
                qDebug() << "ComponentDownloader: verifying SHA256 for newly found component" << entry.info.id;
                QString actualHash = computeFileSha256(foundPath);
                if (actualHash == entry.info.hash) {
                    std::filesystem::path promotedPath;
                    if (installComponent(foundPath, entry.info.filename, false, &promotedPath)) {
                        verified = true;
                        entry.verifiedPath = promotedPath;
                        // Save to state
                        QJsonObject newState;
                        newState[QStringLiteral("sha256")] = entry.info.hash;
                        newState[QStringLiteral("path")] = PathUtils::toQString(promotedPath);
                        componentsState[entry.info.id] = newState;
                        stateChanged = true;
                    } else {
                        qWarning() << "ComponentDownloader: failed to promote verified component" << entry.info.id;
                    }
                } else {
                    qWarning() << "ComponentDownloader: checksum mismatch for" << entry.info.id << "at" << PathUtils::toQString(foundPath);
                }
            }
        }

        entry.present = verified;
        if (entry.present) {
            entry.state = Done;
            qDebug() << "ComponentDownloader: component already present & verified:" << entry.info.id;
        } else if (!downloadable) {
            entry.state = Missing;
            qWarning() << "ComponentDownloader: component" << entry.info.id
                       << "is not present and has no download URL — marking as Missing";
        }

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

    qDebug() << "ComponentDownloader: loaded" << m_components.size() << "component(s) from manifest";
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
        if (e.info.url.isEmpty() || e.info.hash.isEmpty())
            continue;
        if (!e.present && (e.info.required || e.info.suggested))
            return true;
    }
    return false;
}

bool ComponentDownloader::canStartDownload() const
{
    for (const ComponentEntry &e : m_components) {
        if (e.enabled && !e.present
            && !e.info.url.isEmpty() && !e.info.hash.isEmpty())
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
        if (e.info.url.isEmpty() || e.info.hash.isEmpty())
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

    const std::filesystem::path destPath = downloadSubdir / entry.info.filename.toStdString();
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

    QNetworkRequest request(QUrl(entry.info.url));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_currentReply = m_networkManager->get(request);
    connect(m_currentReply, &QNetworkReply::readyRead, this, &ComponentDownloader::onReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &ComponentDownloader::onDownloadFinished);
}

void ComponentDownloader::onReadyRead()
{
    if (!m_outputFile || !m_currentReply
        || m_currentDownloadIndex < 0 || m_currentDownloadIndex >= m_components.size())
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

    if (QString::fromLatin1(hash) != entry.info.hash) {
        qWarning() << "SHA256 mismatch for" << entry.info.id
                   << "expected:" << entry.info.hash
                   << "got:" << hash;
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
    entry.verifiedPath = promotedPath;

    // Save to local state
    QJsonObject stateObj = loadComponentsState();
    QJsonObject componentsState = stateObj[QStringLiteral("components")].toObject();
    
    QJsonObject newState;
    newState[QStringLiteral("sha256")] = entry.info.hash;
    newState[QStringLiteral("path")] = PathUtils::toQString(promotedPath);
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
    QSaveFile file(PathUtils::toQString(xmlPath));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "ComponentDownloader: failed to open external_providers.xml for writing:" << file.errorString();
        return;
    }
    file.write(buffer);
    if (!file.commit()) {
        qWarning() << "ComponentDownloader: failed to commit external_providers.xml:" << file.errorString();
        return;
    }
    qDebug() << "ComponentDownloader: wrote external_providers.xml";
}

#endif // BUNDLED_HOST
