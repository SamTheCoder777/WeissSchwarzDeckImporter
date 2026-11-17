import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property var card: ({})
    readonly property int copies: {
        var revision = deckBuilder.deckRevision;
        return deckBuilder.quantity(String(card.cardCode || ""));
    }
    spacing: 5
    ActionButton {
        text: "−"
        Layout.preferredWidth: 30
        Layout.minimumWidth: 30
        enabled: root.copies > 0
        onClicked: deckBuilder.changeQuantity(root.card, -1)
    }
    Label {
        text: root.copies
        color: "#80baff"
        horizontalAlignment: Text.AlignHCenter
        Layout.fillWidth: true
        Layout.minimumWidth: 24
        font.bold: true
    }
    ActionButton {
        text: "+"
        primary: true
        Layout.preferredWidth: 30
        Layout.minimumWidth: 30
        enabled: deckBuilder.totalCards < 50
        onClicked: deckBuilder.addCard(root.card)
    }
}
