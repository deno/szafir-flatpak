import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    title: i18n("Missing Runtime")

    property string installOrigin: typeof missingHostOrigin === "string" ? missingHostOrigin : ""
    property bool checking: false
    property string errorMessage: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing * 2
        spacing: Kirigami.Units.largeSpacing

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            text: i18n("SzafirHost Runtime Not Found")
            explanation: i18n("The SzafirHost runtime (pl.kir.szafirhost) is not installed. It is required for Szafir browser integration to work.")
        }

        Label {
            Layout.fillWidth: true
            visible: page.installOrigin.length > 0
            wrapMode: Text.WordWrap
            text: i18n("Install it by running the following command:")
        }

        TextField {
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            visible: page.installOrigin.length > 0
            text: "flatpak install " + page.installOrigin + " pl.kir.szafirhost"
        }

        Label {
            Layout.fillWidth: true
            visible: page.errorMessage.length > 0
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.negativeTextColor
            text: page.errorMessage
        }

        Item { Layout.fillHeight: true }
    }

    footer: Rectangle {
        color: Kirigami.Theme.alternateBackgroundColor
        implicitHeight: footerLayout.implicitHeight + Kirigami.Units.largeSpacing * 2

        Kirigami.Separator {
            anchors.left: parent.left
            anchors.right: parent.right
        }

        RowLayout {
            id: footerLayout
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Kirigami.Units.largeSpacing * 2
            anchors.rightMargin: Kirigami.Units.largeSpacing * 2
            spacing: Kirigami.Units.smallSpacing

            Item { Layout.fillWidth: true }

            Button {
                text: i18n("Quit")
                icon.name: "application-exit"
                onClicked: Qt.quit()
            }

            Button {
                text: page.checking ? i18n("Checking...") : i18n("Next")
                highlighted: true
                icon.name: "go-next"
                enabled: !page.checking
                onClicked: {
                    page.checking = true
                    page.errorMessage = ""
                    var result = setupController.checkHostAndAdvance()
                    if (!result) {
                        page.errorMessage = i18n("SzafirHost runtime is still not installed. Please install it and try again.")
                    }
                    page.checking = false
                }
            }
        }
    }
}
