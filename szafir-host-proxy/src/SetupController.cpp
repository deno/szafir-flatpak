#include "SetupController.h"
#include "AppSettings.h"
#include "ComponentDownloader.h"

#include <QDebug>
#include <QFile>

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

    if (!isRuntimePresent())
        needsWizard = true;
    if (!isLicenseAccepted())
        needsWizard = true;
    if (m_downloader && m_downloader->hasDownloadableComponents()) {
        needsWizard = true;
        hasDownloadableComponents = true;
    }

    if (needsWizard || m_forceWizard) {
        m_pages.append(Welcome);

        if (!isRuntimePresent() || hasDownloadableComponents)
            m_pages.append(Download);

        if (!isLicenseAccepted() || m_forceWizard)
            m_pages.append(License);
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

    goToNextPage();
}

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
    if (m_downloader)
        return m_downloader->allRequiredComplete();
    return false;
}

bool SetupController::isLicenseAccepted() const
{
    std::error_code ec;
    return std::filesystem::exists(licenseAcceptedMarkerPath(), ec);
}
