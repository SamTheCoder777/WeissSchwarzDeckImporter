import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../gallery"

Item {
    id: root
    property var cards: []
    property bool editable: false
    property bool showOrderingControls: true
    property var sourceFor: function (card) {
        return "";
    }
    property string selectedCode: ""
    property string groupMode: "color"
    property string sortMode: "composite"
    property bool descending: true
    readonly property var selectedCard: {
        for (var i = 0; i < cards.length; ++i)
            if (cards[i].cardCode === selectedCode)
                return cards[i];
        return ({});
    }
    readonly property var overlayItem: root.Overlay.overlay || root
    onVisibleChanged: if (!visible)
        references.close()
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            visible: root.showOrderingControls
            DarkCombo {
                Layout.preferredWidth: 130
                model: ["Color", "Level", "No groups"]
                currentIndex: ["color", "level", "none"].indexOf(root.groupMode)
                onActivated: function (index) {
                    root.groupMode = ["color", "level", "none"][index];
                }
            }
            DarkCombo {
                Layout.fillWidth: true
                model: ["Color > level > cost > power", "Name", "Card ID", "Level", "Power"]
                currentIndex: ["composite", "name", "code", "level", "power"].indexOf(root.sortMode)
                onActivated: function (index) {
                    root.sortMode = ["composite", "name", "code", "level", "power"][index];
                }
            }
            DarkCombo {
                Layout.preferredWidth: 120
                model: ["Descending", "Ascending"]
                currentIndex: root.descending ? 0 : 1
                onActivated: function (index) {
                    root.descending = index === 0;
                }
            }
        }
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            CollectionGrid {
                SplitView.preferredWidth: parent.width * 0.62
                SplitView.minimumWidth: 190
                cards: root.cards
                editable: root.editable
                selectedCode: root.selectedCode
                sourceFor: root.sourceFor
                overlayItem: root.overlayItem
                groupMode: root.groupMode
                sortMode: root.sortMode
                descending: root.descending
                onCardSelected: function (card) {
                    root.selectedCode = card.cardCode;
                }
            }
            Rectangle {
                SplitView.preferredWidth: parent.width * 0.38
                SplitView.minimumWidth: 240
                color: "#262b33"
                radius: 8
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    CardDetailPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!root.selectedCard.cardCode
                        card: root.selectedCard
                        requestCardData: false
                        imageSourceOverride: root.sourceFor(root.selectedCard)
                        onReferenceRequested: function (kind, value) {
                            references.showReference(kind, value, root.groupMode, root.sortMode, root.descending);
                        }
                    }
                    QuantityButtons {
                        Layout.fillWidth: true
                        visible: root.editable && !!root.selectedCard.cardCode
                        card: root.selectedCard
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: !root.selectedCard.cardCode
                    text: "Select a card to see details"
                    color: "#9aa0aa"
                }
            }
        }
    }
    ReferenceResultsPopup {
        id: references
        parent: root.overlayItem
        sourceRows: root.cards
        editable: root.editable
        sourceFor: root.sourceFor
        onCardSelected: function (card) {
            root.selectedCode = card.cardCode;
        }
    }
}
