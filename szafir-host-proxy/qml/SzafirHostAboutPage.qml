import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    title: i18n("About SzafirHost runtime")
    padding: 0
    property string javaTmpCacheSize: i18n("Calculating...")
    readonly property var installedComponents: componentDownloader
        ? componentDownloader.components.filter(function(component) { return component.present })
        : []

    function refreshJavaTmpCacheSize() {
        javaTmpCacheSize = hostRuntimeController.cacheSize()
    }

    Component.onCompleted: {
        hostRuntimeController.startWatching()
        hostRuntimeController.cacheSizeChanged.connect(refreshJavaTmpCacheSize)
        refreshJavaTmpCacheSize()
    }

    Component.onDestruction: {
        hostRuntimeController.cacheSizeChanged.disconnect(refreshJavaTmpCacheSize)
        hostRuntimeController.stopWatching()
    }

    // ── Inline shared components ─────────────────────────────────────────────

    /** Level-2 section heading with standard page margins. */
    component SectionHeading: Kirigami.Heading {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.largeSpacing
        Layout.leftMargin: Kirigami.Units.largeSpacing * 2
        Layout.rightMargin: Kirigami.Units.largeSpacing * 2
        level: 2
        wrapMode: Text.WordWrap
    }

    /** Body text label indented one gridUnit from section margin. */
    component BodyLabel: QQC2.Label {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        Layout.leftMargin: Kirigami.Units.largeSpacing * 2 + Kirigami.Units.gridUnit
        Layout.rightMargin: Kirigami.Units.largeSpacing * 2
        wrapMode: Text.WordWrap
    }

    /** Read-only selectable monospace field for hashes/checksums. */
    component HashField: QQC2.TextArea {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        Layout.leftMargin: Kirigami.Units.largeSpacing * 2 + Kirigami.Units.gridUnit
        Layout.rightMargin: Kirigami.Units.largeSpacing * 2
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.WrapAnywhere
        background: null
        font.family: "monospace"
    }

    /** Full-width separator with top margin between sections. */
    component SectionSeparator: Kirigami.Separator {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.largeSpacing
    }

    // ── License text view ────────────────────────────────────────────────────
    // Pushed as a pageStack layer when the user clicks the license link.
    Component {
        id: licenseViewComponent
        Kirigami.Page {
            title: i18n("Szafir Software License Agreement")
            padding: 0

            QQC2.ScrollView {
                id: licenseScrollView
                anchors.fill: parent
                contentWidth: availableWidth

                QQC2.TextArea {
                    width: licenseScrollView.availableWidth
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
                    text: typeof szafirHostLicenseText === "string" && szafirHostLicenseText.length > 0
                        ? szafirHostLicenseText
                        : i18n("License text could not be loaded.")
                }
            }
        }
    }

    // ── Main scroll area ─────────────────────────────────────────────────────
    QQC2.ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: 0

            // ── App header (no icon) ─────────────────────────────────────────
            Kirigami.Heading {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                Layout.rightMargin: Kirigami.Units.largeSpacing * 2
                text: SzafirHostAbout.displayName + " " + SzafirHostAbout.version
                wrapMode: Text.WordWrap
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.largeSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                Layout.rightMargin: Kirigami.Units.largeSpacing * 2
                level: 3
                type: Kirigami.Heading.Type.Secondary
                wrapMode: Text.WordWrap
                visible: SzafirHostAbout.shortDescription.length > 0
                text: SzafirHostAbout.shortDescription
            }

            Kirigami.Separator { Layout.fillWidth: true }

            // ── Copyright ────────────────────────────────────────────────────
            SectionHeading { text: i18n("Copyright") }

            BodyLabel {
                visible: SzafirHostAbout.copyrightStatement.length > 0
                text: SzafirHostAbout.copyrightStatement
            }

            Kirigami.UrlButton {
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2 + Kirigami.Units.gridUnit
                visible: SzafirHostAbout.homepage.toString().length > 0
                url: SzafirHostAbout.homepage.toString()
                text: SzafirHostAbout.homepage.toString()
            }

            // ── License ──────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2 + Kirigami.Units.gridUnit
                Layout.rightMargin: Kirigami.Units.largeSpacing * 2

                QQC2.Label { text: i18n("License:") }

                Kirigami.LinkButton {
                    text: i18n("Proprietary")
                    onClicked: {
                        if (applicationWindow().pageStack.layers.depth === 2)
                            applicationWindow().pageStack.layers.push(licenseViewComponent)
                    }
                }
            }

            SectionSeparator {}

            // ── Cache ────────────────────────────────────────────────────────
            SectionHeading {
                text: i18n("Cache")
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2 + Kirigami.Units.gridUnit
                Layout.rightMargin: Kirigami.Units.largeSpacing * 2

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("Size used: %1", page.javaTmpCacheSize)
                }

                QQC2.Button {
                    text: i18n("Clear cache")
                    icon.name: "edit-clear"
                    flat: true
                    onClicked: {
                        hostRuntimeController.clearCache()
                        page.refreshJavaTmpCacheSize()
                    }
                }
            }

            SectionSeparator {}

            // ── Technical Information ────────────────────────────────────────
            SectionHeading {
                text: i18n("Technical Information")
                Layout.bottomMargin: Kirigami.Units.largeSpacing
            }

            Repeater {
                model: installedComponents

                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                        Layout.rightMargin: Kirigami.Units.largeSpacing * 2
                        wrapMode: Text.WordWrap
                        font.bold: true
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                        text: modelData.id === "szafirhost-installer" ? i18n("SzafirHost installer") : modelData.name
                    }

                    BodyLabel {
                        visible: modelData.version.length > 0
                        text: i18n("Version: %1", modelData.version)
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    }

                    BodyLabel {
                        visible: modelData.sha256.length > 0
                        text: modelData.type === "installer" ? i18n("SHA256 (installer):") :
                              modelData.type === "bundled-source" ? i18n("SHA256 (source):") :
                              i18n("SHA256:")
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    }

                    HashField {
                        visible: modelData.sha256.length > 0
                        text: modelData.sha256
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    }
                }
            }

            Item { Layout.minimumHeight: Kirigami.Units.largeSpacing * 2 }
        }
    }
}
