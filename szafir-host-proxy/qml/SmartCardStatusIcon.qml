import QtQuick
import Qt5Compat.GraphicalEffects
import org.kde.kirigami as Kirigami

/*!
  \brief Smart card icon with the status badge composited behind the card body.

  \c smartcard-symbolic has its bottom-right corner cut away and a detached
  crescent next to the cut, so anything painted underneath shows through that
  opening. Drawing the badge first and the tinted card body on top makes the
  opening read as the badge colour (green check for a present card, red cross
  otherwise) instead of as the page background.
 */
Item {
    id: root

    /// Whether a card is present in the reader; selects the badge colour.
    property bool present: false

    implicitWidth: Kirigami.Units.iconSizes.medium
    implicitHeight: Kirigami.Units.iconSizes.medium

    Kirigami.Icon {
        anchors.fill: parent
        source: root.present
            ? "qrc:/icons/smartcard_present_badge.svg"
            : "qrc:/icons/smartcard_fail_badge.svg"
    }

    Kirigami.Icon {
        id: body
        anchors.fill: parent
        source: "smartcard-symbolic"
    }

    ColorOverlay {
        anchors.fill: parent
        source: body
        color: Kirigami.Theme.textColor
    }
}
