import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page

    title: i18n("Certificate details")
    padding: 0

    property string readerName: ""
    property int certificateIndex: -1

    readonly property var selectedReader: {
        const readers = smartCardMonitor.readers
        for (let i = 0; i < readers.length; ++i) {
            if (readers[i].name === page.readerName)
                return readers[i]
        }
        return null
    }
    readonly property var certificate: page.selectedReader && page.selectedReader.certificates
        && page.certificateIndex >= 0
        && page.certificateIndex < page.selectedReader.certificates.length
        ? page.selectedReader.certificates[page.certificateIndex] : ({})

    function value(key) {
        const value = page.certificate[key]
        return value === undefined || value === null || value === ""
            ? i18n("Not available") : value
    }

    function listValue(key) {
        const value = page.certificate[key]
        if (!value || value.length === 0)
            return i18n("Not available")
        return value.join("\n")
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
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 0

            SectionHeading { text: i18n("General") }
            DetailRow { label: i18n("Subject"); value: page.value("subject") }
            DetailRow { label: i18n("Issuer"); value: page.value("issuer") }
            DetailRow { label: i18n("Serial number"); value: page.value("serialDecimal") }
            DetailRow { label: i18n("Serial number (hexadecimal)"); value: page.value("serialHex") }
            DetailRow { label: i18n("Valid from"); value: page.value("validFrom") }
            DetailRow { label: i18n("Valid until"); value: page.value("validUntil") }

            SectionSeparator {}
            SectionHeading { text: i18n("Public key") }
            DetailRow { label: i18n("Algorithm"); value: page.value("publicKeyAlgorithm") }
            DetailRow { label: i18n("Key size"); value: page.certificate.publicKeyBits
                    ? i18n("%1 bits", page.certificate.publicKeyBits) : i18n("Not available") }
            DetailRow { label: i18n("Signature algorithm"); value: page.value("signatureAlgorithm") }
            DetailRow { label: i18n("Key usage"); value: page.listValue("keyUsage") }
            DetailRow { label: i18n("Extended key usage"); value: page.listValue("extendedKeyUsage") }

            SectionSeparator {}
            SectionHeading { text: i18n("Identifiers") }
            DetailRow { label: i18n("Subject alternative names"); value: page.listValue("alternativeNames") }
            DetailRow { label: i18n("Certificate policies"); value: page.listValue("policyOids") }
            DetailRow { label: i18n("Certificate statements"); value: page.listValue("qcStatements") }
            DetailRow { label: i18n("Private key match"); value: page.value("privateKeyMatch") }

            SectionSeparator {}
            SectionHeading { text: i18n("Fingerprints") }
            DetailRow { label: i18n("SHA-256"); value: page.value("fingerprintSha256") }
            DetailRow { label: i18n("SHA-1"); value: page.value("fingerprintSha1") }

            Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
        }
    }
}
