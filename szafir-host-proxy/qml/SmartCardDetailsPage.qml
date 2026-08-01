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

    function detailValue(key) {
        const reader = page.selectedReader
        if (!reader || reader[key] === undefined || reader[key] === null || reader[key] === "")
            return i18n("Not available")
        return reader[key]
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
            color: Kirigami.Theme.textColor
            wrapMode: Text.WrapAnywhere
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

            DetailRow { label: i18n("Status"); value: {
                if (page.selectedReader === null)
                    return i18n("Reader is no longer available")
                return page.selectedReader.present ? i18n("Card present") : i18n("No card")
            }}

            DetailRow { label: i18n("Vendor"); value: page.detailValue("readerVendor") }
            DetailRow { label: i18n("Model"); value: page.detailValue("readerModel") }
            DetailRow { label: i18n("Serial number"); value: page.detailValue("readerSerial") }
            DetailRow { label: i18n("Firmware version"); value: page.detailValue("readerVersion") }

            SectionSeparator {}
            SectionHeading { text: i18n("Card") }

            DetailRow { label: i18n("ATR"); value: page.detailValue("atr") }
            DetailRow { label: i18n("Protocol"); value: page.detailValue("protocol") }
            DetailRow { label: i18n("Card type"); value: page.detailValue("cardType") }

            SectionSeparator {}
            SectionHeading { text: i18n("Provider") }

            DetailRow { label: i18n("Provider name"); value: page.detailValue("providerName") }

            SectionSeparator {}
            SectionHeading { text: i18n("Certificate") }

            DetailRow { label: i18n("Certificate subject"); value: page.detailValue("certificateSubject") }
            DetailRow { label: i18n("Certificate issuer"); value: page.detailValue("certificateIssuer") }
            DetailRow { label: i18n("Certificate serial number"); value: page.detailValue("certificateSerial") }
            DetailRow { label: i18n("Valid from"); value: page.detailValue("certificateValidFrom") }
            DetailRow { label: i18n("Valid until"); value: page.detailValue("certificateValidUntil") }

            Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
        }
    }
}
