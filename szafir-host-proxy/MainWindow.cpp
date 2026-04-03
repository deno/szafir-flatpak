#include "MainWindow.h"
#include "ComponentInfo.h"
#include "HostRuntimeController.h"
#include "ScalingController.h"
#include "SetupController.h"
#include "NativeMessagingService.h"
#include "config.h"
#ifdef BUNDLED_HOST
#include "ComponentDownloader.h"
#endif

#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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
#ifdef BUNDLED_HOST
                       ComponentDownloader *componentDownloader,
#endif
                       QObject *parent)
    : QObject(parent)
    , m_hostRuntime(new HostRuntimeController(this))
    , m_service(service)
    , m_scalingController(scalingController)
    , m_setupController(setupController)
#ifdef BUNDLED_HOST
    , m_componentDownloader(componentDownloader)
#endif
    , m_activeHostCount(service->activeHostCount())
{
    connect(m_service, &NativeMessagingService::activeHostCountChanged,
            this, &MainWindow::setActiveHostCount);
    connect(m_service, &NativeMessagingService::clientsChanged,
            this, &MainWindow::clientsChanged);
}

MainWindow::~MainWindow()
{
    delete m_engine;
}

int MainWindow::activeHostCount() const
{
    return m_activeHostCount;
}

QVariantList MainWindow::clients() const
{
    QVariantList result;
    for (const ClientInfo &ci : m_service->clients()) {
        QVariantMap m;
        m[QStringLiteral("clientName")] = ci.clientName;
        m[QStringLiteral("icon")]       = ci.icon;
        m[QStringLiteral("browserType")] = ci.browserType;
        m[QStringLiteral("flatpakId")]  = ci.flatpakId;
        m[QStringLiteral("executable")] = ci.executable;
        m[QStringLiteral("pid")]        = ci.pid;
        m[QStringLiteral("dbusHandle")] = ci.dbusHandle;
        result.append(m);
    }
    return result;
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

        m_engine->rootContext()->setContextProperty(
            QStringLiteral("About"),
            QVariant::fromValue(KAboutData::applicationData()));
        m_engine->rootContext()->setContextProperty(
            QStringLiteral("SzafirHostAbout"),
            QVariant::fromValue(createSzafirHostAboutData(
#ifdef BUNDLED_HOST
                [this]() -> QString {
                    for (const auto &e : m_componentDownloader->components()) {
                        if (e.info.id == QLatin1String("szafirhost-installer"))
                            return e.info.version;
                    }
                    return {};
                }()
#else
                QString{}
#endif
            )));

        m_engine->rootContext()->setContextProperty(
            QStringLiteral("componentInfo"), new AboutPageComponentInfo(
#ifdef BUNDLED_HOST
                m_componentDownloader,
#endif
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
#ifdef BUNDLED_HOST
    m_engine->rootContext()->setContextProperty(QStringLiteral("componentDownloader"), m_componentDownloader);
#endif
#ifndef BUNDLED_HOST
    m_engine->rootContext()->setContextProperty(QStringLiteral("missingHostOrigin"), m_missingHostOrigin);
#endif
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
