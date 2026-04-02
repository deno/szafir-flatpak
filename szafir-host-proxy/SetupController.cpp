#include "SetupController.h"
#include "AppSettings.h"

#ifdef BUNDLED_HOST
#include "ComponentDownloader.h"
#endif

#include <QDebug>
#include <QFile>

#ifndef BUNDLED_HOST
#include <QProcess>

static bool checkHostInstalled();
#endif

SetupController::SetupController(QObject *parent)
    : QObject(parent)
{
    // computePages() is called explicitly from main() after optional setForceWizard()
}

void SetupController::computePages()
{
    m_pages.clear();

    bool needsWizard = false;
    bool hasDownloadableComponents = false;

#ifdef BUNDLED_HOST
    if (!isRuntimePresent())
        needsWizard = true;
    if (!isLicenseAccepted())
        needsWizard = true;
    if (m_downloader && m_downloader->hasDownloadableComponents()) {
        needsWizard = true;
        hasDownloadableComponents = true;
    }
#else
    if (!checkHostInstalled())
        needsWizard = true;
#endif

    if (needsWizard || m_forceWizard) {
        m_pages.append(Welcome);

#ifdef BUNDLED_HOST
        if (!isRuntimePresent() || hasDownloadableComponents)
            m_pages.append(Download);
#else
        if (!checkHostInstalled())
            m_pages.append(MissingHost);
#endif

#ifdef BUNDLED_HOST
        if (!isLicenseAccepted() || m_forceWizard)
            m_pages.append(License);
#endif
    }

    m_pages.append(Status);

    m_pageIndex = 0;
    m_currentPage = m_pages.first();

    qDebug() << "SetupController: computed pages:" << m_pages
             << "wizard needed:" << isWizardNeeded();
}

QVariantList SetupController::pages() const
{
    QVariantList result;
    for (Page p : m_pages)
        result.append(static_cast<int>(p));
    return result;
}

void SetupController::advance()
{
    goToNextPage();
}

void SetupController::acceptLicense()
{
#ifdef BUNDLED_HOST
    const std::filesystem::path markerPath = licenseAcceptedMarkerPath();
    const std::filesystem::path markerDir = markerPath.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(markerDir, ec);

    QFile marker(markerPath);
    if (marker.open(QIODevice::WriteOnly)) {
        qDebug() << "License accepted, marker written";
    } else {
        qWarning() << "Failed to write license marker";
    }
#endif

    goToNextPage();
}

#ifndef BUNDLED_HOST
static bool checkHostInstalled()
{
    QProcess process;
    process.start(
        QStringLiteral("flatpak-spawn"),
        {QStringLiteral("--host"), QStringLiteral("flatpak"), QStringLiteral("info"),
         QString::fromLatin1(kSzafirHostAppId)});
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool SetupController::checkHostAndAdvance()
{
    if (checkHostInstalled()) {
        goToNextPage();
        return true;
    }
    return false;
}
#endif

void SetupController::goToNextPage()
{
    if (m_pageIndex + 1 >= m_pages.size())
        return;

    m_pageIndex++;
    m_currentPage = m_pages[m_pageIndex];
    Q_EMIT currentPageChanged();

    if (m_currentPage == Status)
        Q_EMIT wizardCompleted();
}

bool SetupController::isRuntimePresent() const
{
#ifdef BUNDLED_HOST
    if (m_downloader)
        return m_downloader->allRequiredComplete();
#endif
    return false;
}

bool SetupController::isLicenseAccepted() const
{
#ifdef BUNDLED_HOST
    std::error_code ec;
    return std::filesystem::exists(licenseAcceptedMarkerPath(), ec);
#else
    return true;
#endif
}
