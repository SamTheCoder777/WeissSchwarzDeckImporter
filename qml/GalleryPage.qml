import QtQuick 2.15
import QtQuick.Controls 2.15

// Top-level page loaded by MainWindow::buildGalleryPage().
// Context properties expected: `cardDatabase` (DatabaseUtil), `selModel` (SelectionModel)
Item {
    id: root
    anchors.fill: parent

    property var selectedCard: ({})
    readonly property bool hasSelection: selectedCard && Object.keys(selectedCard).length > 0

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // ---------------- LEFT: gallery grid ----------------
        Rectangle {
            SplitView.preferredWidth: parent.width * 0.62
            SplitView.minimumWidth: 340
            color: "#1e1f22"

            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: 14
                cellWidth: 150
                cellHeight: 210
                clip: true
                model: selModel
                reuseItems: true

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: CardDelegate {
                    onCardClicked: (data) => root.selectedCard = data
                }
            }

            Label {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: "No cards detected yet"
                color: "#777"
                font.pixelSize: 14
            }
        }

        // ---------------- RIGHT: detail panel ----------------
        Rectangle {
            SplitView.preferredWidth: parent.width * 0.38
            SplitView.minimumWidth: 280
            color: "#26282c"

            CardDetailPanel {
                anchors.fill: parent
                anchors.margins: 18
                card: root.selectedCard
                visible: root.hasSelection
            }

            Label {
                anchors.centerIn: parent
                visible: !root.hasSelection
                text: "Select a card to see details"
                color: "#777"
                font.pixelSize: 14
            }
        }
    }
}
