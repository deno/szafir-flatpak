#include "MainWindow.h"
#include "ComponentInfo.h"
#include "HostRuntimeController.h"
#include "ScalingController.h"
#include "SetupController.h"
#include "NativeMessagingService.h"
#include "ComponentDownloader.h"
#include "SmartCardMonitor.h"
#include "UpdateController.h"
#include "ThemeController.h"
#include "config.h"

#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QUrl>
#include <QWindow>
#include <KAboutData>
#include <KLocalizedContext>
#include <KLocalizedString>

namespace {

KAboutData createSzafirHostAboutData(const QString &version)
{
    KAboutData aboutData(
        QStringLiteral("szafirhost"),
        i18n("KIR SzafirHost"),
        version
    );

    aboutData.setShortDescription(
        i18n("A secure Native Messaging bridge between web browsers and qualified electronic signature smart cards."));
    aboutData.setCopyrightStatement(i18n("© 2026 Krajowa Izba Rozliczeniowa S.A."));
    aboutData.setHomepage(QStringLiteral("https://www.elektronicznypodpis.pl/"));
    aboutData.setDesktopFileName(QStringLiteral("pl.kir.szafirhost"));
    aboutData.setBugAddress("");

    aboutData.addAuthor(
        i18n("Krajowa Izba Rozliczeniowa S.A."),
        i18n("Developer and Trust Service Provider"),
        QStringLiteral("kontakt@kir.pl"),
        QStringLiteral("https://www.kir.pl/"));

    aboutData.addAuthor(
        i18n("Szafir Technical Support"),
        i18n("Helpdesk and Troubleshooting"),
        QStringLiteral("serwis@kir.pl"));

    return aboutData;
}

}

MainWindow::MainWindow(NativeMessagingService *service, ScalingController *scalingController,
                       SetupController *setupController,
                       ComponentDownloader *componentDownloader,
                       UpdateController *updateController,
                       SmartCardMonitor *smartCardMonitor,
                       ThemeController *themeController,
                       QObject *parent)
    : QObject(parent)
    , m_hostRuntime(new HostRuntimeController(this))
    , m_service(service)
    , m_scalingController(scalingController)
    , m_setupController(setupController)
    , m_componentDownloader(componentDownloader)
    , m_updateController(updateController)
    , m_smartCardMonitor(smartCardMonitor)
    , m_themeController(themeController)
    , m_activeHostCount(service->activeHostCount())
{
    connect(m_service, &NativeMessagingService::activeHostCountChanged,
            this, &MainWindow::setActiveHostCount);
}

MainWindow::~MainWindow()
{
    delete m_engine;
}

int MainWindow::activeHostCount() const
{
    return m_activeHostCount;
}

NativeMessagingService *MainWindow::clientsModel() const
{
    return m_service;
}

void MainWindow::show()
{
    ensureWindow();
    if (m_window)
        m_window->show();
}

void MainWindow::raise()
{
    if (m_window)
        m_window->raise();
}

void MainWindow::activateWindow()
{
    if (m_window)
        m_window->requestActivate();
}

void MainWindow::hide()
{
    if (m_window)
        m_window->hide();
}

void MainWindow::stopClient(qint64 pid)
{
    m_service->stopClient(pid);
}

void MainWindow::setActiveHostCount(int activeHostCount)
{
    if (m_activeHostCount == activeHostCount)
        return;

    m_activeHostCount = activeHostCount;
    emit activeHostCountChanged(m_activeHostCount);
}

void MainWindow::ensureWindow()
{
    if (m_window)
        return;

    if (!m_engine) {
        m_engine = new QQmlApplicationEngine(this);
        m_engine->rootContext()->setContextObject(new KLocalizedContext(m_engine));

        qmlRegisterUncreatableType<UpdateController>(
            "SzafirHostProxy", 1, 0, "UpdateController",
            QStringLiteral("UpdateController is only available as a context property"));

        qmlRegisterUncreatableType<ThemeController>(
            "SzafirHostProxy", 1, 0, "ThemeController",
            QStringLiteral("ThemeController is only available as a context property"));

        m_engine->rootContext()->setContextProperty(
            QStringLiteral("About"),
            QVariant::fromValue(KAboutData::applicationData()));
        m_engine->rootContext()->setContextProperty(
            QStringLiteral("SzafirHostAbout"),
            QVariant::fromValue(createSzafirHostAboutData(
                [this]() -> QString {
                    for (const auto &e : m_componentDownloader->components()) {
                        if (e.info.id == QLatin1String("szafirhost-installer"))
                            return e.info.version;
                    }
                    return {};
                }()
            )));

        m_engine->rootContext()->setContextProperty(
            QStringLiteral("componentInfo"), new AboutPageComponentInfo(
                m_componentDownloader,
                m_engine));

        m_engine->rootContext()->setContextProperty(
            QStringLiteral("szafirHostLicenseText"), HostRuntimeController::loadLicenseText());
        m_engine->rootContext()->setContextProperty(
            QStringLiteral("chromeExtensionUrl"), QStringLiteral(CHROME_EXTENSION_URL));
        m_engine->rootContext()->setContextProperty(
            QStringLiteral("firefoxExtensionUrl"), QStringLiteral(FIREFOX_EXTENSION_URL));
        m_engine->rootContext()->setContextProperty(
            QStringLiteral("hostRuntimeController"), m_hostRuntime);
        m_engine->rootContext()->setContextProperty(
            QStringLiteral("APP_ID"), QStringLiteral(APP_ID));
    }

    m_engine->rootContext()->setContextProperty(QStringLiteral("mainWindowController"), this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("scalingController"), m_scalingController);
    m_engine->rootContext()->setContextProperty(QStringLiteral("setupController"), m_setupController);
    m_engine->rootContext()->setContextProperty(QStringLiteral("componentDownloader"), m_componentDownloader);
    m_engine->rootContext()->setContextProperty(QStringLiteral("updateController"), m_updateController);
    m_engine->rootContext()->setContextProperty(QStringLiteral("smartCardMonitor"), m_smartCardMonitor);
    m_engine->rootContext()->setContextProperty(QStringLiteral("themeController"), m_themeController);
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/SzafirHostProxy/qml/MainWindow.qml")));

    if (m_engine->rootObjects().isEmpty()) {
        qWarning() << "Failed to load MainWindow QML.";
        return;
    }

    QObject *root = m_engine->rootObjects().constFirst();
    m_window = qobject_cast<QWindow *>(root);
    if (!m_window) {
        qWarning() << "MainWindow QML root is not a window.";
        return;
    }

    connect(m_window, &QObject::destroyed, this, [this]() {
        m_window = nullptr;
    });
}
