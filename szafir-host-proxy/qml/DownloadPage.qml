import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    title: i18n("Download Components")

    property bool standalone: false  // true when opened from hamburger menu

    ListView {
        id: componentList
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        model: componentDownloader
        spacing: Kirigami.Units.smallSpacing
        focus: true
        interactive: contentHeight > height

        header: ColumnLayout {
            width: componentList.width
            spacing: Kirigami.Units.largeSpacing

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("The following components need to be downloaded to enable Szafir browser integration.")
            }

            Item { Layout.preferredHeight: Kirigami.Units.smallSpacing }
        }

        delegate: ItemDelegate {
            id: delegateItem
            width: ListView.view.width
            hoverEnabled: true

            // Sync mouse hover with keyboard focus to mimic Plasma applet behavior
            onHoveredChanged: {
                if (hovered) {
                    ListView.view.currentIndex = index
                }
            }

            // Custom background mimicking Plasma style
            background: Rectangle {
                readonly property bool showSelection: delegateItem.ListView.isCurrentItem
                                                     && !componentDownloader.isDownloading

                color: showSelection
                       ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2)
                       : "transparent"

                border.color: showSelection && componentList.activeFocus
                              ? Kirigami.Theme.highlightColor
                              : "transparent"
                border.width: showSelection && componentList.activeFocus ? 1 : 0
                radius: Kirigami.Units.smallSpacing
            }

            onClicked: {
                if (!model.component.required && !model.present && model.downloadable && !componentDownloader.isDownloading) {
                    const savedIndex = index
                    componentDownloader.setComponentEnabled(model.component.id, !model.enabled)
                    ListView.view.currentIndex = savedIndex
                }
            }

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                CheckBox {
                    id: compCheckbox
                    focusPolicy: Qt.NoFocus
                    checked: !model.present && model.enabled && model.downloadable
                    enabled: !model.component.required && !model.present && model.downloadable && !componentDownloader.isDownloading
                    onToggled: componentDownloader.setComponentEnabled(model.component.id, checked)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: model.component.name
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: {
                            if (model.present) return i18n("Already installed")
                            const sizeMb = (model.component.size / 1048576).toFixed(1)
                            switch (model.state) {
                            case 0: return i18n("%1 MB", sizeMb)               // Pending
                            case 1: {                                           // Downloading
                                const recvMb = (model.bytesReceived / 1048576).toFixed(1)
                                return i18n("Downloading... %1 / %2 MB", recvMb, sizeMb)
                            }
                            case 2: return i18n("Verifying checksum...")        // Verifying
                            case 3: return i18n("Downloaded")                   // Done
                            case 4: return i18n("%1 MB", sizeMb)               // Skipped → show size again
                            case 5: return i18n("Error")                        // Error
                            case 6: return i18n("Missing")                      // Missing — not available
                            default: return ""
                            }
                        }
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: (model.state === 5 || model.state === 6) ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                    }
                }

                BusyIndicator {
                    visible: model.state === 1 || model.state === 2
                    running: visible
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }

                Kirigami.Icon {
                    visible: model.state === 3
                    source: "emblem-ok-symbolic"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    color: Kirigami.Theme.positiveTextColor
                }

                Kirigami.Icon {
                    visible: model.state === 5 || model.state === 6
                    source: "emblem-error"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    color: Kirigami.Theme.negativeTextColor
                }
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

            AnimatedStateButton {
                // Animate between Download and Next states
                // state 0 = download, state 1 = next
                state: (!page.standalone && (!componentDownloader.canStartDownload || componentDownloader.hasBrokenComponents)) ? 1 : 0

                firstText: i18n("Download")
                firstIcon: "download"

                secondText: i18n("Next")
                secondIcon: "go-next"

                highlighted: activeFocus
                enabled: {
                    if (!componentDownloader || componentDownloader.isDownloading)
                        return false
                    if (componentDownloader.hasBrokenComponents)
                        return false
                    if (page.standalone)
                        return componentDownloader.canStartDownload
                    return true
                }

                onClicked: {
                    if (page.standalone || componentDownloader.canStartDownload) {
                        componentDownloader.startDownloads()
                    } else {
                        setupController.advance()
                    }
                }
            }
        }
    }

    Connections {
        target: componentDownloader
        function onIsDownloadingChanged() {
            if (componentDownloader.isDownloading)
                componentList.focus = false
        }
        function onAllDownloadsComplete() {
            if (!page.standalone && componentDownloader.allRequiredComplete
                    && !componentDownloader.hasBrokenComponents)
                setupController.advance()
        }
    }
}
