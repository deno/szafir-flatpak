#include <memory>
#include "config.h"

#include <QActionGroup>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDebug>
#include <QIcon>
#include <QLoggingCategory>
#include <QMenu>
#include <QStandardPaths>

#ifndef BUNDLED_HOST
#include <QProcess>
#endif

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedString>
#include <KStatusNotifierItem>

#include "ScalingController.h"
#include "AppSettings.h"
#include "NativeHostIntegrator.h"
#include "NativeMessagingService.h"
#include "MainWindow.h"
#include "SetupController.h"
#include "LandlockSandbox.h"

#ifdef BUNDLED_HOST
#include "ComponentDownloader.h"
#endif

#include <KSignalHandler>

#include <csignal>
#include <filesystem>

namespace {

#ifndef BUNDLED_HOST

QString getProxyOrigin()
{
    QProcess process;
    process.start(
        QStringLiteral("flatpak-spawn"),
        {QStringLiteral("--host"), QStringLiteral("flatpak"), QStringLiteral("info"),
         QStringLiteral("--show-origin"), QStringLiteral(APP_ID)});
    if (!process.waitForFinished(5000)) {
        qWarning() << "Timed out while querying proxy origin";
        process.kill();
        process.waitForFinished();
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

#endif // BUNDLED_HOST

void updateTray(KStatusNotifierItem *tray, int activeHostCount)
{
    tray->setIconByName(QStringLiteral(APP_ID));
    if (activeHostCount > 0) {
        tray->setStatus(KStatusNotifierItem::Active);
        tray->setToolTip(
            QStringLiteral(APP_ID),
            QStringLiteral("SzafirHost Proxy"),
            i18n("Active hosts: %1", activeHostCount));
    } else {
        tray->setStatus(KStatusNotifierItem::Passive);
        tray->setToolTip(
            QStringLiteral(APP_ID),
            QStringLiteral("SzafirHost Proxy"),
            i18n("No active hosts"));
    }
}

// Appends a submenu (Auto / 2x / 1x) to parentMenu.
// Returns an updater callable that re-checks the correct action given a new scale string.
std::function<void(const QString &)> addHiDpiMenu(
    QMenu *parentMenu,
    const QString &label,
    const QString &initialScale,
    std::function<void(const QString &)> setter)
{
    auto *submenu = parentMenu->addMenu(label);
    auto *group = new QActionGroup(submenu);
    group->setExclusive(true);

    // Pairs of (action, scale value) captured by value in the returned updater.
    // Actions are parented to submenu which lives for the full app lifetime.
    struct Entry { QAction *action; QString value; };
    QList<Entry> entries;

    auto addScaleAction = [&](const QString &actionLabel, const QString &value) {
        auto *action = submenu->addAction(actionLabel);
        action->setCheckable(true);
        action->setChecked(false);
        group->addAction(action);
        entries.append({action, value});
        QObject::connect(action, &QAction::triggered, submenu, [setter, value]() {
            setter(value);
        });
    };

    addScaleAction(i18n("Auto"), {});
    addScaleAction(i18n("2x scaling"), QStringLiteral("2"));
    addScaleAction(i18n("1x scaling"), QStringLiteral("1"));

    auto updater = [entries](const QString &scale) {
        for (const Entry &e : entries)
            e.action->setChecked(e.value == scale);
    };

    updater(initialScale);

    return updater;
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef ENABLE_FLATPAK_HOST_ICONS_LOOKUP
    const std::filesystem::path userFlatpakExportDir =
        PathUtils::toFsPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        / "flatpak" / "exports" / "share";

    // Prepend host directories to XDG_DATA_DIRS before creating QApplication
    // so that KIconLoader and Qt's platform theme (like KDEPlasmaPlatformTheme)
    // can properly find host icons for QML and Kirigami components.
    QByteArrayList mergedDirs {
        "/run/host/usr/share",
        "/run/host/usr/local/share",
        "/run/host/var/lib/flatpak/exports/share",
        QByteArray::fromStdString(userFlatpakExportDir.string()),
        "/var/lib/flatpak/exports/share"
    };

    for (const QByteArray &dir : qgetenv("XDG_DATA_DIRS").split(':')) {
        if (!dir.isEmpty() && !mergedDirs.contains(dir))
            mergedDirs.append(dir);
    }

    qputenv("XDG_DATA_DIRS", mergedDirs.join(':'));
#endif

    QApplication app(argc, argv);

    // Drop any host flatpak override access we don't need at all
    if (!LandlockSandbox::limitOverrides()) {
        qCritical() << "Landlock Phase 1 (limitOverrides) failed; aborting.";
        return 1;
    }

    KLocalizedString::setApplicationDomain("szafir-host-proxy");

    KAboutData aboutData(
        QStringLiteral("szafirhostproxy"),
        QStringLiteral("SzafirHost Proxy"),
        QStringLiteral(APP_VERSION),
        i18n("Szafir native messaging proxy and host integrator."),
        KAboutLicense::GPL_V2,
        QStringLiteral("(C) 2026")
    );
    aboutData.setOrganizationDomain(QByteArrayLiteral("kir.deno.pl"));
    aboutData.addAuthor(QStringLiteral("deno"));
    aboutData.setHomepage(QStringLiteral("https://github.com/deno/szafirhostproxy/"));
    aboutData.setBugAddress("https://github.com/deno/szafirhostproxy/issues");
    KAboutData::setApplicationData(aboutData);

    app.setApplicationName(QStringLiteral("szafirhostproxy"));
    app.setOrganizationDomain(QStringLiteral("kir.deno.pl"));
    app.setApplicationDisplayName(aboutData.displayName());
    app.setApplicationVersion(aboutData.version());
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral(APP_ID)));

    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false"));

    QCommandLineParser parser;
    parser.setApplicationDescription(aboutData.shortDescription());

    aboutData.setupCommandLine(&parser);

    const QCommandLineOption debugOpt(
        QStringLiteral("debug"),
        i18n("Enable debug logging."));
    const QCommandLineOption installOpt(
        QStringLiteral("install"),
        i18n("Install native host definitions and wrappers, then exit."));
    const QCommandLineOption uninstallOpt(
        QStringLiteral("uninstall"),
        i18n("Remove native host definitions/wrappers and permissions, then exit."));
    const QCommandLineOption dryRunOpt(
        QStringLiteral("dry-run"),
        i18n("Print planned file/permission operations without performing them. Requires --install or --uninstall."));
    const QCommandLineOption showStatusWindowOpt(
        QStringLiteral("show-status-window"),
        i18n("Show modeless status window when launched."));
    const QCommandLineOption wizardOpt(
        QStringLiteral("wizard"),
        i18n("Show the first-run setup wizard even if all checks already pass."));

    parser.addOption(debugOpt);
    parser.addOption(installOpt);
    parser.addOption(uninstallOpt);
    parser.addOption(dryRunOpt);
    parser.addOption(showStatusWindowOpt);
    parser.addOption(wizardOpt);

    parser.process(app);
    aboutData.processCommandLine(&parser);

    if (parser.isSet(debugOpt) || qgetenv("SZAFIR_DEBUG") == "1")
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=true"));

    const bool doInstall = parser.isSet(installOpt);
    const bool doUninstall = parser.isSet(uninstallOpt);
    const bool dryRun = parser.isSet(dryRunOpt);
    const bool showStatusWindow = parser.isSet(showStatusWindowOpt);
    const bool forceWizard = parser.isSet(wizardOpt);

    if (doInstall && doUninstall) {
        qCritical().noquote() << i18n("Use either --install or --uninstall, not both.");
        return 2;
    }

    if (dryRun && !doInstall && !doUninstall) {
        qCritical().noquote() << i18n("--dry-run requires --install or --uninstall.");
        return 2;
    }

    NativeHostIntegrator integrator(dryRun);

    if (doInstall) {
        if (!integrator.installNow()) {
            qCritical().noquote() << i18n("Failed to install native host integration.");
            return 1;
        }
        qInfo().noquote() << (dryRun ? i18n("Dry-run install complete.") : i18n("Native host integration installed."));
        return 0;
    }

    if (doUninstall) {
        if (!integrator.uninstall()) {
            qCritical().noquote() << i18n("Failed to uninstall native host integration.");
            return 1;
        }
        qInfo().noquote() << (dryRun ? i18n("Dry-run uninstall complete.") : i18n("Native host integration removed."));
        return 0;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCritical().noquote() << i18n("Cannot connect to the D-Bus session bus.");
        return 1;
    }

    auto *dbusService = new KDBusService(KDBusService::Unique | KDBusService::NoExitOnFailure, &app);
    if (!dbusService->isRegistered()) {
        qInfo().noquote() << i18n("szafir-host-proxy is already running.");
        return 0;
    }

    auto *service = new NativeMessagingService(&app);
    if (!bus.registerObject(QStringLiteral("/pl/deno/kir/szafirhostproxy/NativeMessaging"), service)) {
        qCritical().noquote() << i18n("Failed to register D-Bus object.");
        return 1;
    }

    if (!integrator.installIfNeeded()) {
        qWarning().noquote() << i18n("Failed to fully install native host integration; continuing startup.");
    }

    // Manifests and wrapper setup finished
    if (!LandlockSandbox::dropBrowserAccess()) {
        qCritical() << "Landlock Phase 2 (dropBrowserAccess) failed; aborting.";
        return 1;
    }

    // Scaling override paths
    const std::filesystem::path szafirOverride = hostOverridePath(QString::fromLatin1(kSzafirAppId));
    const std::filesystem::path hostOverride =
#ifndef BUNDLED_HOST
        hostOverridePath(QString::fromLatin1(kSzafirHostAppId));
#else
        bundledHostOverridePath();
#endif
    auto *scalingController = new ScalingController(szafirOverride, hostOverride, &app);

    // Setup controller determines wizard flow
    auto *setupController = new SetupController(&app);
    if (forceWizard)
        setupController->setForceWizard(true);

#ifdef BUNDLED_HOST
    auto *componentDownloader = new ComponentDownloader(&app);
    setupController->setComponentDownloader(componentDownloader);
#endif

    setupController->computePages();

#ifndef BUNDLED_HOST
    const QString proxyOrigin = getProxyOrigin();
#endif

    // MainWindow is always created (shows wizard or status page based on SetupController state)
    std::unique_ptr<MainWindow> mainWindow(new MainWindow(service, scalingController, setupController,
#ifdef BUNDLED_HOST
                                       componentDownloader,
#endif
                                       nullptr));
#ifndef BUNDLED_HOST
    mainWindow->setMissingHostOrigin(proxyOrigin);
#endif

    auto showMainWindow = [&mainWindow]() {
        mainWindow->show();
        mainWindow->raise();
        mainWindow->activateWindow();
    };

    // ----- Tray icon setup (created only after wizard completes) -----
    KStatusNotifierItem *tray = nullptr;

    auto createTray = [&app, &tray, service, scalingController, &mainWindow, &showMainWindow]() {
        if (tray)
            return;  // Already created

        tray = new KStatusNotifierItem(QStringLiteral("szafir-host-proxy"), &app);
        tray->setTitle(QStringLiteral("SzafirHost Proxy"));
        tray->setIconByName(QStringLiteral(APP_ID));
        tray->setStatus(KStatusNotifierItem::Passive);
        tray->setToolTip(
            QStringLiteral(APP_ID),
            QStringLiteral("SzafirHost Proxy"),
            i18n("No active hosts"));

        QObject::connect(tray, &KStatusNotifierItem::activateRequested,
            &app, [&showMainWindow](bool, const QPoint &) {
                showMainWindow();
            });

        // HiDPI scaling submenus
        QMenu *trayMenu = tray->contextMenu();
        trayMenu->addSeparator();
        auto szafirUpdater = addHiDpiMenu(trayMenu, i18n("Szafir scaling"),
            scalingController->szafirScale(),
            [scalingController](const QString &s) { scalingController->setSzafirScale(s); });
        auto hostUpdater = addHiDpiMenu(trayMenu, i18n("SzafirHost scaling"),
            scalingController->hostScale(),
            [scalingController](const QString &s) { scalingController->setHostScale(s); });

        QObject::connect(scalingController, &ScalingController::szafirScaleChanged,
            &app, [szafirUpdater](const QString &s) { szafirUpdater(s); });
        QObject::connect(scalingController, &ScalingController::hostScaleChanged,
            &app, [hostUpdater](const QString &s) { hostUpdater(s); });

        // Update tray on host count changes
        QObject::connect(service, &NativeMessagingService::activeHostCountChanged,
            tray, [tray](int count) {
                updateTray(tray, count);
            });

        updateTray(tray, service->activeHostCount());
    };

    // ----- Wizard vs normal startup flow -----
    if (setupController->isWizardNeeded()) {
        // Wizard mode: show window, block connections, no tray yet
        app.setQuitOnLastWindowClosed(true);
        service->setAcceptingConnections(false);

        showMainWindow();

        // When wizard completes, transition to normal mode
        QObject::connect(setupController, &SetupController::wizardCompleted, &app,
            [&app, service, &createTray]() {
                app.setQuitOnLastWindowClosed(false);
                service->setAcceptingConnections(true);
                createTray();
            });

        // DBus activation during wizard: just raise the wizard window
        QObject::connect(dbusService, &KDBusService::activateRequested,
            &app, [&showMainWindow](const QStringList &, const QString &) {
                showMainWindow();
            });
    } else {
        // No wizard needed: normal mode with tray
        app.setQuitOnLastWindowClosed(false);
        service->setAcceptingConnections(true);
        createTray();

        QObject::connect(dbusService, &KDBusService::activateRequested,
            &app, [&showMainWindow, &parser, &showStatusWindowOpt](const QStringList &args, const QString &) {
                parser.parse(args);
                if (parser.isSet(showStatusWindowOpt)) {
                    showMainWindow();
                }
            });

        if (showStatusWindow) {
            showMainWindow();
        }
    }

    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGTERM);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, &app, [](int signal) {
        qDebug() << "Received signal" << signal << "quitting gracefully...";
        QCoreApplication::quit();
    });

    return app.exec();
}
