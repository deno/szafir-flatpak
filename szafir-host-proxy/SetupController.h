#pragma once

#include <QObject>
#include <QVariantList>

#ifdef BUNDLED_HOST
class ComponentDownloader;
#endif

class SetupController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QVariantList pages READ pages CONSTANT)
    Q_PROPERTY(bool isWizardNeeded READ isWizardNeeded CONSTANT)
    Q_PROPERTY(bool isWizardComplete READ isWizardComplete NOTIFY wizardCompleted)

public:
    enum Page {
        Welcome,
        Download,
        MissingHost,
        License,
        Status
    };
    Q_ENUM(Page)

    explicit SetupController(QObject *parent = nullptr);

    int currentPage() const { return m_currentPage; }
    QVariantList pages() const;
    bool isWizardNeeded() const { return m_pages.size() > 1; }
    bool isWizardComplete() const { return m_currentPage == Status; }

    // Call before computePages() to force wizard even when all checks pass.
    void setForceWizard(bool force) { m_forceWizard = force; }

#ifdef BUNDLED_HOST
    // Must be called before computePages() so isRuntimePresent() uses verified state.
    void setComponentDownloader(ComponentDownloader *downloader) { m_downloader = downloader; }
#endif

    void computePages();

    Q_INVOKABLE void advance();
    Q_INVOKABLE void acceptLicense();

#ifndef BUNDLED_HOST
    Q_INVOKABLE bool checkHostAndAdvance();
#endif

Q_SIGNALS:
    void currentPageChanged();
    void wizardCompleted();

private:
    void goToNextPage();
    bool isRuntimePresent() const;
    bool isLicenseAccepted() const;

    QList<Page> m_pages;
    int m_pageIndex = 0;
    Page m_currentPage = Welcome;
    bool m_forceWizard = false;
#ifdef BUNDLED_HOST
    ComponentDownloader *m_downloader = nullptr;
#endif
};
