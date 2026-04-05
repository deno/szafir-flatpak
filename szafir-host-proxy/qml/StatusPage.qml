import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    title: i18n("Status")
    state: mainWindowController.activeHostCount > 0 ? "browsers" : "placeholder"

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
    // ─────────────────────────────────────────────────────────────────────

    states: [
        State {
            name: "placeholder"
            PropertyChanges {
                target: placeholderView
                opacity: 1
                scale: 1
            }
            PropertyChanges {
                target: browsersList
                opacity: 0
                scale: 1
            }
        },
        State {
            name: "browsers"
            PropertyChanges {
                target: placeholderView
                opacity: 0
                scale: 0.85
            }
            PropertyChanges {
                target: browsersList
                opacity: 1
                scale: 1
            }
        }
    ]

    transitions: [
        Transition {
            from: "placeholder"
            to: "browsers"

            ParallelAnimation {
                NumberAnimation {
                    target: placeholderView
                    properties: "opacity,scale"
                    duration: 250
                    easing.type: Easing.OutCubic
                }

                NumberAnimation {
                    target: browsersList
                    property: "opacity"
                    duration: 250
                    easing.type: Easing.OutCubic
                }

                NumberAnimation {
                    target: browsersList
                    property: "scale"
                    from: 1.1
                    to: 1.0
                    duration: 250
                    easing.type: Easing.OutCubic
                }
            }
        },
        Transition {
            from: "browsers"
            to: "placeholder"

            ParallelAnimation {
                NumberAnimation {
                    target: placeholderView
                    property: "opacity"
                    duration: 250
                    easing.type: Easing.OutCubic
                }

                NumberAnimation {
                    target: browsersList
                    property: "opacity"
                    duration: 250
                    easing.type: Easing.OutCubic
                }
            }
        }
    ]

    ColumnLayout {
        id: placeholderView
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: opacity > 0
        opacity: 1
        scale: 1
        spacing: Kirigami.Units.largeSpacing

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

    ListView {
        id: browsersList
        anchors.fill: parent
        visible: opacity > 0
        opacity: 0
        scale: 1
        model: mainWindowController.clientsModel
        spacing: Kirigami.Units.smallSpacing

        add: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 200
                easing.type: Easing.OutCubic
            }
        }

        remove: Transition {
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: 200
                easing.type: Easing.OutCubic
            }
        }

        displaced: Transition {
            NumberAnimation {
                properties: "x,y"
                duration: 220
                easing.type: Easing.OutCubic
            }
        }

        delegate: ItemDelegate {
            width: ListView.view.width
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
