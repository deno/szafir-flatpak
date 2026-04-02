import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    title: "AnimatedStateButton Preview"
    width: 400
    height: 300

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Kirigami.Units.largeSpacing

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "AnimatedStateButton Preview"
            font.pointSize: 14
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Auto-toggling between Download and Next states"
            font.pointSize: 10
            color: Kirigami.Theme.disabledTextColor
        }

        Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }

        AnimatedStateButton {
            Layout.alignment: Qt.AlignHCenter
            previewMode: true
            previewDuration: 2000
        }

        Item { Layout.fillHeight: true }
    }
}
