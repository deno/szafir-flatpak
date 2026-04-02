import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Button {
    id: root

    // Which state is visible: 0 (first) or 1 (second)
    property int state: 0

    // First state (left position)
    property string firstText: "Download"
    property string firstIcon: "download"

    // Second state (right position)
    property string secondText: "Next"
    property string secondIcon: "go-next"

    // Explicit horizontal padding so it's included in implicitWidth calculation
    horizontalPadding: Kirigami.Units.largeSpacing

    // Auto-size to the wider of the two content sets
    implicitWidth: Math.max(firstLayout.implicitWidth, secondLayout.implicitWidth)
                   + leftPadding + rightPadding

    // Animation speed in milliseconds
    property int animDuration: 250

    // Preview mode: auto-toggles state for demonstration
    property bool previewMode: false
    property int previewDuration: 2000  // milliseconds between state changes

    contentItem: Item {
        id: contentArea
        clip: true
        // Drive height from actual content so the button is never too short
        implicitHeight: firstLayout.implicitHeight

        Row {
            id: contentInner
            // Each slot is exactly the visible area width; two slots side by side
            width: contentArea.width * 2
            height: contentArea.height

            // First state content
            Item {
                id: firstSlot
                width: contentArea.width
                height: parent.height

                RowLayout {
                    id: firstLayout
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: root.firstIcon
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        visible: root.firstIcon !== ""
                    }

                    Label {
                        text: root.firstText
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // Second state content
            Item {
                id: secondSlot
                width: contentArea.width
                height: parent.height

                RowLayout {
                    id: secondLayout
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: root.secondIcon
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        visible: root.secondIcon !== ""
                    }

                    Label {
                        text: root.secondText
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }

    states: [
        State {
            name: "firstState"
            when: root.state === 0
            PropertyChanges { target: contentInner; x: 0 }
            PropertyChanges { target: firstSlot;   opacity: 1.0 }
            PropertyChanges { target: secondSlot;  opacity: 0.0 }
        },
        State {
            name: "secondState"
            when: root.state === 1
            PropertyChanges { target: contentInner; x: -contentArea.width }
            PropertyChanges { target: firstSlot;   opacity: 0.0 }
            PropertyChanges { target: secondSlot;  opacity: 1.0 }
        }
    ]

    transitions: [
        Transition {
            from: "firstState"; to: "secondState"
            ParallelAnimation {
                NumberAnimation { target: contentInner; property: "x"; duration: root.animDuration; easing.type: Easing.InOutQuad }
                NumberAnimation { target: firstSlot;   property: "opacity"; duration: root.animDuration }
                NumberAnimation { target: secondSlot;  property: "opacity"; duration: root.animDuration }
            }
        },
        Transition {
            from: "secondState"; to: "firstState"
            ParallelAnimation {
                NumberAnimation { target: contentInner; property: "x"; duration: root.animDuration; easing.type: Easing.InOutQuad }
                NumberAnimation { target: firstSlot;   property: "opacity"; duration: root.animDuration }
                NumberAnimation { target: secondSlot;  property: "opacity"; duration: root.animDuration }
            }
        }
    ]

    // Auto-toggle for preview mode
    Timer {
        running: root.previewMode
        interval: root.previewDuration
        repeat: true
        onTriggered: root.state = 1 - root.state
    }
}
