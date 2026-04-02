import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    title: i18n("License Agreement")

    property string licenseTextData: typeof szafirHostLicenseText === "string" ? szafirHostLicenseText : ""

    padding: 0

    ColumnLayout {
        anchors.fill: parent

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TextArea {
                readOnly: true
                textFormat: TextEdit.MarkdownText
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                background: null

                horizontalAlignment: Text.AlignJustify

                leftPadding: Kirigami.Units.largeSpacing * 2 + 2
                rightPadding: Kirigami.Units.largeSpacing * 2
                topPadding: Kirigami.Units.largeSpacing
                bottomPadding: Kirigami.Units.largeSpacing

                text: page.licenseTextData.length > 0
                    ? page.licenseTextData
                    : i18n("License text could not be loaded.")
            }
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
                text: i18n("Quit")
                icon.name: "application-exit"
                onClicked: Qt.quit()
            }

            Button {
                text: i18n("Accept")
                highlighted: true
                icon.name: "dialog-ok"
                onClicked: setupController.acceptLicense()
            }
        }
    }
}
