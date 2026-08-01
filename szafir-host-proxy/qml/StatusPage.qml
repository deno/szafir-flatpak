import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    objectName: "statusPage"
    title: i18n("Status")
    padding: 0

    readonly property bool updateActive: typeof updateController !== "undefined"
        && updateController !== null
        && (updateController.state === UpdateController.Checking
            || updateController.state === UpdateController.Downloading
            || updateController.state === UpdateController.StoppingHosts
            || updateController.state === UpdateController.Installing)

    property string statusMessage: ""

    Timer {
        interval: 5000
        running: page.statusMessage !== ""
        onTriggered: page.statusMessage = ""
    }

    footer: Rectangle {
        id: statusFooter
        implicitHeight: updateFooterLayout.implicitHeight + Kirigami.Units.smallSpacing * 2
        color: Kirigami.Theme.backgroundColor
        clip: true

        readonly property bool contentShown: updateActive || page.statusMessage !== ""

        Kirigami.Separator {
            anchors.top: parent.top
            width: parent.width
            opacity: statusFooter.contentShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
        }

        RowLayout {
            id: updateFooterLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            transform: Translate {
                y: statusFooter.contentShown ? 0 : statusFooter.height
                Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            }

            BusyIndicator {
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                running: updateActive
                visible: updateActive
            }

            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: {
                    if (updateActive && typeof updateController !== "undefined" && updateController !== null) {
                        switch (updateController.state) {
                        case UpdateController.Checking:
                            return i18n("Checking for updates...")
                        case UpdateController.Downloading:
                            if (updateController.progress >= 0)
                                return i18n("Downloading update... %1%", Math.round(updateController.progress * 100))
                            return i18n("Downloading update...")
                        case UpdateController.StoppingHosts:
                            return i18n("Stopping active connections...")
                        case UpdateController.Installing:
                            return i18n("Installing update...")
                        }
                    }
                    return page.statusMessage
                }
            }

            ProgressBar {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                visible: updateController?.state === UpdateController.Downloading
                value: updateController?.progress >= 0 ? updateController.progress : 0
                indeterminate: updateController?.progress < 0
            }
        }
    }

    Component {
        id: aboutPageComponent
        Kirigami.AboutPage {
            aboutData: About
            getInvolvedUrl: ""
            donateUrl: ""
        }
    }

    Connections {
        target: scalingController
        function onHostScaleChanged() {
            if (applicationWindow().visible && mainWindowController.activeHostCount > 0)
                scalingNoticeDialog.open()
        }
    }

    actions: [
        Kirigami.Action {
            icon.name: "application-menu"
            displayHint: Kirigami.DisplayHint.IconOnly
            text: i18n("Menu")
            onTriggered: appMenu.popup()
        }
    ]

    // ── Hamburger drop-down menu ──────────────────────────────────────────
    Menu {
        id: appMenu

        Menu {
            title: i18n("Szafir scaling")

            MenuItem {
                text: i18n("Auto")
                checkable: true
                autoExclusive: true
                checked: scalingController?.szafirScale === ""
                onTriggered: scalingController.setSzafirScale("")
            }
            MenuItem {
                text: i18n("2x scaling")
                checkable: true
                autoExclusive: true
                checked: scalingController?.szafirScale === "2"
                onTriggered: scalingController.setSzafirScale("2")
            }
            MenuItem {
                text: i18n("1x scaling")
                checkable: true
                autoExclusive: true
                checked: scalingController?.szafirScale === "1"
                onTriggered: scalingController.setSzafirScale("1")
            }
        }

        Menu {
            title: i18n("SzafirHost scaling")

            MenuItem {
                text: i18n("Auto")
                checkable: true
                autoExclusive: true
                checked: scalingController?.hostScale === ""
                onTriggered: scalingController.setHostScale("")
            }
            MenuItem {
                text: i18n("2x scaling")
                checkable: true
                autoExclusive: true
                checked: scalingController?.hostScale === "2"
                onTriggered: scalingController.setHostScale("2")
            }
            MenuItem {
                text: i18n("1x scaling")
                checkable: true
                autoExclusive: true
                checked: scalingController?.hostScale === "1"
                onTriggered: scalingController.setHostScale("1")
            }
        }

        MenuSeparator {}

        MenuItem {
            text: i18n("Download components...")
            icon.name: "download"
            visible: typeof componentDownloader !== "undefined" && componentDownloader !== null
            onTriggered: {
                if (applicationWindow().pageStack.layers.depth === 1)
                    applicationWindow().pageStack.layers.push(Qt.resolvedUrl("DownloadPage.qml"), { standalone: true })
            }
        }

        MenuSeparator {}

        MenuItem {
            text: i18n("Check for updates...")
            icon.name: "view-refresh"
            visible: typeof updateController !== "undefined" && updateController !== null
            onTriggered: updateController.checkForUpdates(true)
        }

        MenuItem {
            text: i18n("Reinstall SzafirHost runtime")
            icon.name: "system-reboot"
            visible: typeof updateController !== "undefined" && updateController !== null
            onTriggered: reinstallConfirmDialog.open()
        }

        MenuItem {
            text: i18n("Install runtime from file...")
            icon.name: "document-open"
            visible: typeof updateController !== "undefined" && updateController !== null
            onTriggered: jarFileDialog.open()
        }

        Menu {
            title: i18n("Updates")
            visible: typeof updateController !== "undefined" && updateController !== null

            MenuItem {
                text: i18n("Automatic updates")
                checkable: true
                checked: updateController?.autoUpdate ?? false
                onTriggered: updateController.autoUpdate = checked
            }
            MenuItem {
                text: i18n("Allow downgrades")
                checkable: true
                checked: updateController?.allowDowngrades ?? true
                onTriggered: updateController.allowDowngrades = checked
            }
        }

        MenuSeparator {}

        MenuItem {
            text: i18n("About SzafirHost")
            onTriggered: {
                if (applicationWindow().pageStack.layers.depth === 1)
                    applicationWindow().pageStack.layers.push(Qt.resolvedUrl("SzafirHostAboutPage.qml"))
            }
        }

        MenuItem {
            icon.name: "help-about"
            text: i18n("About")
            onTriggered: {
                if (applicationWindow().pageStack.layers.depth === 1)
                    applicationWindow().pageStack.layers.push(aboutPageComponent)
            }
        }

        MenuSeparator {}

        MenuItem {
            icon.name: "application-exit"
            text: i18n("Quit")
            onTriggered: Qt.quit()
        }
    }

    Dialog {
        id: scalingNoticeDialog
        title: i18n("Restart Needed")
        modal: true
        standardButtons: Dialog.Ok

        contentItem: Label {
            text: i18n("For changes to take effect, reset the connection and reload the website.")
            wrapMode: Text.Wrap
            width: Kirigami.Units.gridUnit * 20
        }
    }

    FileDialog {
        id: jarFileDialog
        title: i18n("Select SzafirHost installer JAR")
        nameFilters: [i18n("JAR files (*.jar)"), i18n("All files (*)")]
        onAccepted: updateController.installFromFile(selectedFile)
    }

    Dialog {
        id: reinstallConfirmDialog
        title: i18n("Reinstall Runtime")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No

        contentItem: Label {
            text: i18n("This will stop all active SzafirHost connections and reinstall the runtime. Continue?")
            wrapMode: Text.Wrap
            width: Kirigami.Units.gridUnit * 20
        }
        onAccepted: updateController.forceReinstall()
    }

    Dialog {
        id: interruptionDialog
        title: i18n("Active Connections")
        modal: true

        contentItem: Label {
            text: i18n("A browser is currently connected. Updating will interrupt any signing operation in progress. Continue now or wait until idle?")
            wrapMode: Text.Wrap
            width: Kirigami.Units.gridUnit * 22
        }

        footer: DialogButtonBox {
            Button {
                text: i18n("Continue now")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: i18n("Wait")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onAccepted: updateController.confirmInterruption(true)
        onRejected: updateController.confirmInterruption(false)
    }

    Connections {
        target: typeof updateController !== "undefined" ? updateController : null

        function onStateChanged() {
            if (updateController.state === UpdateController.Checking) {
                page.statusMessage = ""
            } else if (updateController.state === UpdateController.UpdateAvailable) {
                page.statusMessage = ""
                if (applicationWindow().pageStack.layers.depth === 1)
                    applicationWindow().pageStack.layers.push(Qt.resolvedUrl("DownloadPage.qml"), { standalone: true })
            } else if (updateController.state === UpdateController.UpToDate) {
                page.statusMessage = i18n("You are already running the latest version.")
            } else if (updateController.state === UpdateController.Error) {
                page.statusMessage = updateController.errorString
            }
        }

        function onInterruptionConfirmationNeeded() {
            interruptionDialog.open()
        }

        function onUpdateFinished(success) {
            page.statusMessage = success
                ? i18n("SzafirHost runtime has been updated successfully.")
                : updateController.errorString
        }
    }
    // ─────────────────────────────────────────────────────────────────────

    // ── Main content: fixed headers with state-dependent body areas ──
    Flickable {
        id: statusContentFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: statusContent.height
        implicitHeight: statusContent.implicitHeight
        clip: true

        Item {
            id: statusContent
            width: statusContentFlickable.width
            readonly property real headersHeight:
                smartCardsHeader.height + connectedBrowsersHeader.height
            readonly property bool smartCardsHaveValues: smartCardMonitor.readers.length > 0
            readonly property real bodyMinimumTotalHeight:
                smartCardsHaveValues
                    ? smartCardsBody.contentHeight + connectedBrowsersBody.contentHeight
                    : Math.max(smartCardsBody.contentHeight, connectedBrowsersBody.contentHeight) * 2
            readonly property real availableBodyHeight:
                Math.max(0, height - headersHeight)
            readonly property real smartCardsBodyHeight:
                smartCardsHaveValues
                    ? smartCardsBody.contentHeight
                    : availableBodyHeight / 2
            readonly property real connectedBrowsersBodyHeight:
                smartCardsHaveValues
                    ? Math.max(0, availableBodyHeight - smartCardsBodyHeight)
                    : availableBodyHeight / 2
            implicitHeight: headersHeight + bodyMinimumTotalHeight
            height: Math.max(statusContentFlickable.height, implicitHeight)

            // ── Smart cards header ───────────────────────────────────
            Item {
                id: smartCardsHeader
                width: parent.width
                height: Kirigami.Units.largeSpacing
                    + smartCardsHeading.implicitHeight
                    + Kirigami.Units.smallSpacing
                    + smartCardsSeparator.implicitHeight

                Label {
                    id: smartCardsHeading
                    anchors.top: parent.top
                    anchors.topMargin: Kirigami.Units.largeSpacing
                    anchors.left: parent.left
                    anchors.leftMargin: Kirigami.Units.smallSpacing
                    text: i18n("Smart cards")
                    font.bold: true
                }

                Kirigami.Separator {
                    id: smartCardsSeparator
                    anchors.top: smartCardsHeading.bottom
                    anchors.topMargin: Kirigami.Units.smallSpacing
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: implicitHeight
                }
            }

            // ── Smart cards body ─────────────────────────────────────
            Item {
                id: smartCardsBody
                anchors.top: smartCardsHeader.bottom
                width: parent.width
                height: statusContent.smartCardsBodyHeight
                readonly property real contentHeight: smartCardMonitor.readers.length > 0
                    ? smartCardsList.childrenRect.height
                    : smartCardsPlaceholder.implicitHeight

                Column {
                    id: smartCardsList
                    anchors.top: parent.top
                    width: parent.width
                    spacing: Kirigami.Units.smallSpacing
                    visible: smartCardMonitor.readers.length > 0

                    Repeater {
                        model: smartCardMonitor.readers

                        ItemDelegate {
                            width: smartCardsList.width
                            height: implicitHeight
                            down: false
                            contentItem: RowLayout {
                                spacing: Kirigami.Units.largeSpacing

                                Kirigami.Icon {
                                    source: modelData.present
                                        ? "qrc:/icons/smartcard_present.svg"
                                        : "qrc:/icons/smartcard_fail.svg"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.present ? i18n("Card present") : i18n("No card")
                                        font.family: Kirigami.Theme.smallFont.family
                                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                                        font.weight: Kirigami.Theme.smallFont.weight
                                        color: modelData.present
                                            ? Kirigami.Theme.positiveTextColor
                                            : Kirigami.Theme.disabledTextColor
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }

                Kirigami.PlaceholderMessage {
                    id: smartCardsPlaceholder
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    height: implicitHeight
                    visible: smartCardMonitor.readers.length === 0
                    icon.source: "qrc:/icons/smartcard_fail.svg"
                    text: smartCardMonitor.available
                        ? i18n("No smart card readers detected")
                        : i18n("Smart card service unavailable")
                    explanation: smartCardMonitor.available
                        ? i18n("Connect a reader to get started.")
                        : i18n("Check that the smart card daemon (pcscd) is running.")
                }
            }

            // ── Connected browsers header ─────────────────────────────
            Item {
                id: connectedBrowsersHeader
                anchors.top: smartCardsBody.bottom
                width: parent.width
                height: Kirigami.Units.largeSpacing
                    + connectedBrowsersHeading.implicitHeight
                    + Kirigami.Units.smallSpacing
                    + connectedBrowsersSeparator.implicitHeight

                Label {
                    id: connectedBrowsersHeading
                    anchors.top: parent.top
                    anchors.topMargin: Kirigami.Units.largeSpacing
                    anchors.left: parent.left
                    anchors.leftMargin: Kirigami.Units.smallSpacing
                    text: i18n("Connected browsers")
                    font.bold: true
                }

                Kirigami.Separator {
                    id: connectedBrowsersSeparator
                    anchors.top: connectedBrowsersHeading.bottom
                    anchors.topMargin: Kirigami.Units.smallSpacing
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: implicitHeight
                }
            }

            // ── Connected browsers body ───────────────────────────────
            Item {
                id: connectedBrowsersBody
                anchors.top: connectedBrowsersHeader.bottom
                width: parent.width
                height: statusContent.connectedBrowsersBodyHeight
                readonly property real contentHeight: mainWindowController.activeHostCount > 0
                    ? connectedBrowsersList.childrenRect.height
                    : connectedBrowsersPlaceholderContent.implicitHeight

                Column {
                    id: connectedBrowsersList
                    anchors.top: parent.top
                    width: parent.width
                    spacing: Kirigami.Units.smallSpacing
                    visible: mainWindowController.activeHostCount > 0

                    Repeater {
                        model: mainWindowController.clientsModel

                        ItemDelegate {
                            width: connectedBrowsersList.width
                            height: implicitHeight
                            down: false
                            contentItem: RowLayout {
                                spacing: Kirigami.Units.largeSpacing

                                Kirigami.Icon {
                                    source: browserIcon || "web-browser"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    Label {
                                        Layout.fillWidth: true
                                        text: clientName
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: flatpakId
                                              ? flatpakId
                                              : (executable ? executable : dbusHandle)
                                        font.family: Kirigami.Theme.smallFont.family
                                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                                        font.weight: Kirigami.Theme.smallFont.weight
                                        font.italic: !flatpakId && !executable
                                        color: Kirigami.Theme.disabledTextColor
                                        elide: Text.ElideRight
                                    }
                                }

                                ToolButton {
                                    icon.name: "process-stop"
                                    text: i18n("Stop")
                                    display: AbstractButton.IconOnly
                                    Layout.alignment: Qt.AlignVCenter
                                    ToolTip.text: text
                                    ToolTip.visible: hovered
                                    onClicked: mainWindowController.stopClient(pid)
                                }
                            }
                        }
                    }
                }

                Item {
                    anchors.fill: parent
                    visible: mainWindowController.activeHostCount === 0

                    ColumnLayout {
                        id: connectedBrowsersPlaceholderContent
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 4
                        height: implicitHeight
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.PlaceholderMessage {
                            Layout.fillWidth: true
                            icon.name: "dialog-ok"
                            text: i18n("Waiting for browser activity...")
                            explanation: i18n("Make sure the browser extension is installed.")
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: Kirigami.Units.largeSpacing

                            Kirigami.UrlButton {
                                url: chromeExtensionUrl
                                text: i18n("Chrome extension")
                            }

                            Kirigami.UrlButton {
                                url: firefoxExtensionUrl
                                text: i18n("Firefox extension")
                            }
                        }
                    }
                }
            }
        }
    }
}
