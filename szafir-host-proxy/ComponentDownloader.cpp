#ifdef BUNDLED_HOST

#include "ComponentDownloader.h"
#include "AppSettings.h"

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

QString computeFileSha256(const QString &path)
{
    QFile f(path);
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
                      QString *installedPathOut)
{
    std::error_code ec;
    const std::filesystem::path destination = finalComponentPath(filename);
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec)
        return false;

    if (sourcePath == destination) {
        if (installedPathOut)
            *installedPathOut = PathUtils::toQString(destination);
        return true;
    }

    if (move) {
        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(sourcePath, destination, ec);
        if (!ec) {
            if (installedPathOut)
                *installedPathOut = PathUtils::toQString(destination);
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
        *installedPathOut = PathUtils::toQString(destination);
    return true;
}

} // namespace

ComponentDownloader::ComponentDownloader(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    loadManifest();
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
        entry.info.sha256      = obj[QStringLiteral("sha256")].toString();
        entry.info.libraryPath = obj[QStringLiteral("library_path")].toString();
        entry.info.size        = obj[QStringLiteral("size")].toInteger();
        entry.info.required    = obj[QStringLiteral("required")].toBool();
        entry.info.suggested   = obj[QStringLiteral("suggested")].toBool();
        entry.enabled          = entry.info.required || entry.info.suggested; // required/suggested components start enabled

        const bool downloadable = !entry.info.url.isEmpty() && !entry.info.sha256.isEmpty();

        // Bundled-source components are compiled into the Flatpak; never download them.
        if (entry.info.type == QStringLiteral("bundled-source")) {
            if (!entry.info.libraryPath.isEmpty()) {
                std::error_code ec;
                entry.present = std::filesystem::exists(PathUtils::toFsPath(entry.info.libraryPath), ec);
                if (!entry.present)
                    qWarning() << "ComponentDownloader: bundled-source" << entry.info.id
                               << "- library not found at" << entry.info.libraryPath;
                else
                    qDebug() << "ComponentDownloader: bundled-source" << entry.info.id
                             << "- library found at" << entry.info.libraryPath;
            } else {
                entry.present = true;
            }
            entry.state = entry.present ? Done : Missing;
            m_components.append(entry);
            continue;
        }

        bool verified = false;
        QJsonObject compState = componentsState[entry.info.id].toObject();
        if (compState.contains(QStringLiteral("sha256")) && compState[QStringLiteral("sha256")].toString() == entry.info.sha256) {
            QString pathStr = compState[QStringLiteral("path")].toString();
            // Ensure the file is still present on disk
            std::error_code ec;
            if (std::filesystem::exists(PathUtils::toFsPath(pathStr), ec)
                && PathUtils::toFsPath(pathStr).parent_path() == verifiedComponentsPath()) {
                verified = true;
                entry.verifiedPath = pathStr;
            }
        }

        if (!verified) {
            // Not in state or checksum changed or file missing, let's look for it
            QString foundPath;
            std::error_code ec;

            // Check dedicated verified location first.
            const std::filesystem::path verifiedPath = finalComponentPath(entry.info.filename);
            if (std::filesystem::exists(verifiedPath, ec)) {
                foundPath = PathUtils::toQString(verifiedPath);
            } else {
                // Legacy/bundled fallback: /app/extra and old XDG data extra dir.
                const std::filesystem::path bundled = std::filesystem::path("/app/extra") / entry.info.filename.toStdString();
                if (std::filesystem::exists(bundled, ec)) {
                    foundPath = PathUtils::toQString(bundled);
                } else {
                    const std::filesystem::path downloaded = downloadedExtraPath() / entry.info.filename.toStdString();
                    if (std::filesystem::exists(downloaded, ec)) {
                        foundPath = PathUtils::toQString(downloaded);
                    }
                }
            }

            if (!foundPath.isEmpty()) {
                // File found on disk but not in state (or invalid state). Verify its hash.
                qDebug() << "ComponentDownloader: verifying SHA256 for newly found component" << entry.info.id;
                QString actualHash = computeFileSha256(foundPath);
                if (actualHash == entry.info.sha256) {
                    QString promotedPath;
                    if (installComponent(PathUtils::toFsPath(foundPath), entry.info.filename, false, &promotedPath)) {
                        verified = true;
                        entry.verifiedPath = promotedPath;
                        // Save to state
                        QJsonObject newState;
                        newState[QStringLiteral("sha256")] = entry.info.sha256;
                        newState[QStringLiteral("path")] = promotedPath;
                        componentsState[entry.info.id] = newState;
                        stateChanged = true;
                    } else {
                        qWarning() << "ComponentDownloader: failed to promote verified component" << entry.info.id;
                    }
                } else {
                    qWarning() << "ComponentDownloader: checksum mismatch for" << entry.info.id << "at" << foundPath;
                    // If downloaded, we might delete it, but let's just leave it unverified.
                    // It will be re-downloaded if needed.
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

        m_components.append(entry);
    }

    if (stateChanged) {
        stateObj[QStringLiteral("components")] = componentsState;
        saveComponentsState(stateObj);
    }

    writeExternalProvidersXml();

    qDebug() << "ComponentDownloader: loaded" << m_components.size() << "component(s) from manifest";
}

QVariantList ComponentDownloader::components() const
{
    QVariantList result;
    for (const ComponentEntry &e : m_components) {
        QVariantMap m;
        m[QStringLiteral("id")]            = e.info.id;
        m[QStringLiteral("name")]          = e.info.name;
        m[QStringLiteral("type")]          = e.info.type;
        m[QStringLiteral("version")]       = e.info.version;
        m[QStringLiteral("sha256")]        = e.info.sha256;
        m[QStringLiteral("filename")]      = e.info.filename;
        m[QStringLiteral("size")]          = e.info.size;
        m[QStringLiteral("required")]      = e.info.required;
        m[QStringLiteral("suggested")]     = e.info.suggested;
        m[QStringLiteral("enabled")]       = e.enabled;
        m[QStringLiteral("state")]         = static_cast<int>(e.state);
        m[QStringLiteral("bytesReceived")] = e.bytesReceived;
        m[QStringLiteral("present")]       = e.present;
        m[QStringLiteral("downloadable")]   = !e.info.url.isEmpty() && !e.info.sha256.isEmpty();
        result.append(m);
    }
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
        if (e.info.url.isEmpty() || e.info.sha256.isEmpty())
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
            && !e.info.url.isEmpty() && !e.info.sha256.isEmpty())
            return true;
    }
    return false;
}

bool ComponentDownloader::hasmissingComponents() const
{
    for (const ComponentEntry &e : m_components) {
        if (e.info.required && e.state == Missing)
            return true;
    }
    return false;
}

void ComponentDownloader::setComponentEnabled(const QString &id, bool enabled)
{
    for (int i = 0; i < m_components.size(); ++i) {
        if (m_components[i].info.id == id && !m_components[i].info.required) {
            qDebug() << "ComponentDownloader: setComponentEnabled" << id << "->"
                     << (enabled ? "enabled" : "disabled");
            m_components[i].enabled = enabled;
            Q_EMIT componentsChanged();
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
    for (ComponentEntry &e : m_components) {
        if (e.info.url.isEmpty() || e.info.sha256.isEmpty())
            continue; // Not downloadable — skip
        if (e.enabled && e.state != Done) {
            qDebug() << "ComponentDownloader: queuing" << e.info.id << "for download";
            e.state = Pending;
        } else if (!e.enabled && e.state != Done) {
            qDebug() << "ComponentDownloader: skipping" << e.info.id << "(not enabled)";
            e.state = Skipped;
        }
    }
    Q_EMIT componentsChanged();

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
        Q_EMIT componentsChanged();
        Q_EMIT allDownloadsComplete();
        return;
    }

    ComponentEntry &entry = m_components[m_currentDownloadIndex];
    qDebug() << "ComponentDownloader: starting download for" << entry.info.id
             << "url:" << entry.info.url
             << "expected size:" << entry.info.size << "bytes";
    entry.state = Downloading;
    entry.bytesReceived = 0;
    Q_EMIT componentsChanged();

    const std::filesystem::path downloadSubdir =
        componentDownloadPath() / QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    std::error_code ec;
    std::filesystem::create_directories(downloadSubdir, ec);
    if (ec) {
        qWarning() << "ComponentDownloader: failed to create download subdirectory:" << ec.message().c_str();
        entry.state = Error;
        Q_EMIT componentsChanged();
        Q_EMIT downloadFailed(entry.info.id, QStringLiteral("Failed to create download subdirectory"));
        downloadNext();
        return;
    }

    const std::filesystem::path destPath = downloadSubdir / entry.info.filename.toStdString();
    m_outputFile = new QFile(PathUtils::toQString(destPath), this);
    if (!m_outputFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open file for writing:" << PathUtils::toQString(destPath);
        entry.state = Error;
        Q_EMIT componentsChanged();
        Q_EMIT downloadFailed(entry.info.id, QStringLiteral("Failed to open file for writing"));
        delete m_outputFile;
        m_outputFile = nullptr;
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
    Q_EMIT componentsChanged();
}

void ComponentDownloader::onDownloadFinished()
{
    if (m_currentDownloadIndex < 0 || m_currentDownloadIndex >= m_components.size())
        return;

    ComponentEntry &entry = m_components[m_currentDownloadIndex];

    std::filesystem::path downloadedPath;
    if (m_outputFile) {
        downloadedPath = PathUtils::toFsPath(m_outputFile->fileName());
        m_outputFile->close();
        delete m_outputFile;
        m_outputFile = nullptr;
    }

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Download failed for" << entry.info.id << ":" << reply->errorString();
        entry.state = Error;
        Q_EMIT componentsChanged();
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
    Q_EMIT componentsChanged();

    const QByteArray hash = QCryptographicHash::hash(m_hashAccumulator, QCryptographicHash::Sha256).toHex();
    m_hashAccumulator.clear();

    if (QString::fromLatin1(hash) != entry.info.sha256) {
        qWarning() << "SHA256 mismatch for" << entry.info.id
                   << "expected:" << entry.info.sha256
                   << "got:" << hash;
        entry.state = Error;
        Q_EMIT componentsChanged();
        Q_EMIT downloadFailed(entry.info.id, QStringLiteral("SHA256 checksum mismatch"));

        if (!downloadedPath.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(downloadedPath.parent_path(), ec);
        }

        downloadNext();
        return;
    }

    qDebug() << "ComponentDownloader: SHA256 verified for" << entry.info.id;

    QString promotedPath;
    if (!installComponent(downloadedPath, entry.info.filename, true, &promotedPath)) {
        qWarning() << "ComponentDownloader: failed to promote verified download for" << entry.info.id;
        if (!downloadedPath.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(downloadedPath.parent_path(), ec);
        }
        entry.state = Error;
        Q_EMIT componentsChanged();
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
    newState[QStringLiteral("sha256")] = entry.info.sha256;
    newState[QStringLiteral("path")] = promotedPath;
    componentsState[entry.info.id] = newState;
    
    stateObj[QStringLiteral("components")] = componentsState;
    saveComponentsState(stateObj);

    Q_EMIT componentsChanged();
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
                    && !e.verifiedPath.isEmpty()
                    && !e.info.providerName.isEmpty()) {
                xml.writeStartElement(QStringLiteral("Provider"));
                xml.writeTextElement(QStringLiteral("Name"), e.info.providerName);
                xml.writeTextElement(QStringLiteral("URI"), QStringLiteral("file:") + e.verifiedPath);
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
