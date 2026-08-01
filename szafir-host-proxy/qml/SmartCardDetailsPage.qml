import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page

    title: i18n("Smart card details")
    padding: 0

    property string readerName: ""

    readonly property var selectedReader: {
        const readers = smartCardMonitor.readers
        for (let i = 0; i < readers.length; ++i) {
            if (readers[i].name === page.readerName)
                return readers[i]
        }
        return null
    }
    readonly property var providerInfo: page.selectedReader && page.selectedReader.provider
        ? page.selectedReader.provider : ({})
    readonly property var tokenInfo: page.selectedReader && page.selectedReader.token
        ? page.selectedReader.token : ({})
    readonly property var certificates: page.selectedReader && page.selectedReader.certificates
        ? page.selectedReader.certificates : []
    readonly property string detailsState: page.selectedReader && page.selectedReader.detailsState
        ? page.selectedReader.detailsState : "not-requested"

    function detailValue(key) {
        const reader = page.selectedReader
        if (!reader || reader[key] === undefined || reader[key] === null || reader[key] === "")
            return i18n("Not available")
        return reader[key]
    }

    function mapValue(map, key) {
        if (!map || map[key] === undefined || map[key] === null || map[key] === "")
            return i18n("Not available")
        return map[key]
    }

    function stateMessage() {
        switch (page.detailsState) {
        case "provider-missing":
            return i18n("The Graphite cryptographic provider is not installed.")
        case "no-token":
            return i18n("No token was found through the cryptographic provider.")
        case "no-certificates":
            return i18n("No public certificates were found on this card.")
        case "login-required":
            return i18n("The card requires authentication before its certificates can be read.")
        case "ambiguous-token":
            return i18n("The cryptographic provider could not associate a token with this reader.")
        case "timed-out":
            return i18n("The cryptographic provider did not respond in time.")
        case "module-load-failed":
            return i18n("The cryptographic provider could not be loaded.")
        default:
            return i18n("Certificate details are not available.")
        }
    }

    function requestDetails() {
        if (page.visible && page.selectedReader && page.selectedReader.present)
            smartCardMonitor.requestDetails(page.readerName)
    }

    Component.onCompleted: requestDetails()
    onVisibleChanged: requestDetails()

    Connections {
        target: smartCardMonitor
        function onStatusChanged() {
            page.requestDetails()
        }
    }

    component SectionHeading: Kirigami.Heading {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.largeSpacing
        Layout.leftMargin: Kirigami.Units.largeSpacing * 2
        Layout.rightMargin: Kirigami.Units.largeSpacing * 2
        level: 2
        wrapMode: Text.WordWrap
    }

    component SectionSeparator: Kirigami.Separator {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.largeSpacing
    }

    component DetailRow: ColumnLayout {
        required property string label
        required property string value

        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        Layout.leftMargin: Kirigami.Units.largeSpacing * 2 + Kirigami.Units.gridUnit
        Layout.rightMargin: Kirigami.Units.largeSpacing * 2
        spacing: 0

        QQC2.Label {
            Layout.fillWidth: true
            text: parent.label
            font.bold: true
            wrapMode: Text.WordWrap
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: parent.value
            wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
        }
    }

    QQC2.ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: 0

            SectionHeading { text: i18n("Reader") }

            DetailRow {
                label: i18n("Reader name")
                value: page.readerName.length > 0 ? page.readerName : i18n("Not available")
            }

            DetailRow {
                label: i18n("Status")
                value: {
                    if (page.selectedReader === null)
                        return i18n("Reader is no longer available")
                    return page.selectedReader.present ? i18n("Card present") : i18n("No card")
                }
            }

            DetailRow { label: i18n("Vendor"); value: page.detailValue("readerVendor") }
            DetailRow { label: i18n("Model"); value: page.detailValue("readerModel") }
            DetailRow { label: i18n("Serial number"); value: page.detailValue("readerSerial") }
            DetailRow { label: i18n("Firmware version"); value: page.detailValue("readerVersion") }

            SectionSeparator {}
            SectionHeading { text: i18n("Card") }

            DetailRow { label: i18n("ATR"); value: page.detailValue("atr") }
            DetailRow { label: i18n("Protocol"); value: page.detailValue("protocol") }
            DetailRow { label: i18n("Card type"); value: page.detailValue("cardType") }

            Item {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                Layout.rightMargin: Kirigami.Units.largeSpacing * 2
                implicitHeight: providerStatus.implicitHeight
                visible: page.detailsState === "loading"
                         || (page.detailsState !== "ready"
                             && page.detailsState !== "not-requested"
                             && page.detailsState !== "provider-missing")

                RowLayout {
                    id: providerStatus
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Kirigami.Units.largeSpacing

                    QQC2.BusyIndicator {
                        running: page.detailsState === "loading"
                        visible: running
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: page.stateMessage()
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Button {
                        text: i18n("Retry")
                        visible: page.detailsState !== "loading"
                        onClicked: smartCardMonitor.retryDetails(page.readerName)
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                Layout.rightMargin: Kirigami.Units.largeSpacing * 2
                implicitHeight: missingProviderMessage.implicitHeight
                visible: page.detailsState === "provider-missing"

                RowLayout {
                    id: missingProviderMessage
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Kirigami.Units.largeSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: page.stateMessage()
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Button {
                        text: i18n("Retry")
                        onClicked: smartCardMonitor.retryDetails(page.readerName)
                    }
                }
            }

            SectionSeparator {
                visible: Object.keys(page.tokenInfo).length > 0
            }
            SectionHeading {
                text: i18n("Token")
                visible: Object.keys(page.tokenInfo).length > 0
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: Object.keys(page.tokenInfo).length > 0

                DetailRow { label: i18n("Token label"); value: page.mapValue(page.tokenInfo, "label") }
                DetailRow { label: i18n("Manufacturer"); value: page.mapValue(page.tokenInfo, "manufacturer") }
                DetailRow { label: i18n("Model"); value: page.mapValue(page.tokenInfo, "model") }
                DetailRow { label: i18n("Token serial number"); value: page.mapValue(page.tokenInfo, "serial") }
                DetailRow { label: i18n("Hardware version"); value: page.mapValue(page.tokenInfo, "hardwareVersion") }
                DetailRow { label: i18n("Firmware version"); value: page.mapValue(page.tokenInfo, "firmwareVersion") }
                DetailRow { label: i18n("Slot"); value: page.mapValue(page.tokenInfo, "slotDescription") }
            }

            SectionSeparator {
                visible: Object.keys(page.providerInfo).length > 0
            }
            SectionHeading {
                text: i18n("Provider")
                visible: Object.keys(page.providerInfo).length > 0
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: Object.keys(page.providerInfo).length > 0

                DetailRow { label: i18n("Provider name"); value: page.mapValue(page.providerInfo, "name") }
                DetailRow { label: i18n("Provider manufacturer"); value: page.mapValue(page.providerInfo, "manufacturer") }
                DetailRow { label: i18n("Provider description"); value: page.mapValue(page.providerInfo, "description") }
                DetailRow { label: i18n("Cryptoki version"); value: page.mapValue(page.providerInfo, "cryptokiVersion") }
                DetailRow { label: i18n("Library version"); value: page.mapValue(page.providerInfo, "libraryVersion") }
            }

            SectionSeparator {
                visible: page.certificates.length > 0
            }
            SectionHeading {
                text: i18n("Certificates")
                visible: page.certificates.length > 0
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: page.certificates.length > 0

                Repeater {
                    model: page.certificates

                    delegate: QQC2.ItemDelegate {
                        required property var modelData
                        required property int index
                        readonly property string certificateTitle: modelData.subjectCommonName
                            ? modelData.subjectCommonName
                            : (modelData.subject || i18n("Certificate"))

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        text: certificateTitle
                        down: false

                        contentItem: ColumnLayout {
                            spacing: 0

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: certificateTitle
                                font.bold: true
                                wrapMode: Text.WordWrap
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: modelData.issuer || i18n("Issuer not available")
                                color: Kirigami.Theme.disabledTextColor
                                wrapMode: Text.WrapAnywhere
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: modelData.validFrom && modelData.validUntil
                                      ? i18n("Valid %1 to %2", modelData.validFrom, modelData.validUntil)
                                      : i18n("Validity period not available")
                                color: Kirigami.Theme.disabledTextColor
                                wrapMode: Text.WrapAnywhere
                            }
                        }

                        onClicked: applicationWindow().pageStack.layers.push(
                            Qt.resolvedUrl("CertificateDetailsPage.qml"),
                            { readerName: page.readerName, certificateIndex: index })
                    }
                }
            }

            Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
        }
    }
}
