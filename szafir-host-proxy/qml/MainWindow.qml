import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    width: 480
    height: 420
    minimumWidth: 400
    minimumHeight: 300
        title: setupController && !setupController.isWizardComplete
            ? i18n("Setup")
            : i18n("SzafirHost Proxy")
    visible: false

    Kirigami.Theme.colorSet: Kirigami.Theme.View

    pageStack.globalToolBar.showNavigationButtons: 0
    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn

    onClosing: function(closeEvent) {
        if (setupController && !setupController.isWizardComplete) {
            Qt.quit()
        }
    }

    // ── Page resolver: maps SetupController page enum to QML URLs ────────

    function pageUrlForEnum(pageEnum) {
        switch (pageEnum) {
        case 0: return Qt.resolvedUrl("WelcomePage.qml")
        case 1: return Qt.resolvedUrl("DownloadPage.qml")
        case 2: return Qt.resolvedUrl("MissingHostPage.qml")
        case 3: return Qt.resolvedUrl("LicensePage.qml")
        case 4: return Qt.resolvedUrl("StatusPage.qml")
        default: return Qt.resolvedUrl("StatusPage.qml")
        }
    }

    Component.onCompleted: {
        if (!setupController) return
        if (setupController.isWizardComplete) {
            pageStack.push(Qt.resolvedUrl("StatusPage.qml"))
        } else {
            pageStack.push(pageUrlForEnum(setupController.currentPage))
        }
    }

    Connections {
        target: setupController
        function onCurrentPageChanged() {
            pageStack.push(pageUrlForEnum(setupController.currentPage))
        }
    }
}
