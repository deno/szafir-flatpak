import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    title: i18n("Welcome")

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        spacing: Kirigami.Units.largeSpacing * 2

        Kirigami.Icon {
            Layout.alignment: Qt.AlignHCenter
            source: APP_ID
            Layout.preferredWidth: Kirigami.Units.iconSizes.enormous
            Layout.preferredHeight: Kirigami.Units.iconSizes.enormous
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: i18n("Welcome to SzafirHost Proxy")
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: i18n("This wizard will help you set up everything needed for Szafir browser integration with qualified electronic signatures.")
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
            text: i18n("Click \"Get Started\" to continue.")
        }
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
                text: i18n("Get Started")
                highlighted: true
                icon.name: "go-next"
                onClicked: setupController.advance()
            }
        }
    }
}
