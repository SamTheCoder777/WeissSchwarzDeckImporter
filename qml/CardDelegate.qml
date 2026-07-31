import QtQuick 2.15
import QtQuick.Controls 2.15

// One cell in the detection gallery.
// Pulls the full row (picture, name, rarity, etc.) from cardDataFor()
// ONCE, caches it, and hands that same cached object to the detail
// panel on click — so selecting a card never re-queries the DB.
Rectangle {
    id: root

    // ---- roles coming from SelectionModel ----
    // NOTE: these string names must match SelectionModel::roleNames().
    // Based on your enum (LabelRole, ConfirmedRole, QtyRole, CardIdRole)
    // I'm assuming camelCase keys below — adjust if yours differ.
    required property string cardId
    required property string label
    required property bool confirmed
    required property int qty
    required property int index

    property var fullData: ({})
    readonly property bool loaded: fullData && fullData.picture !== undefined

    signal cardClicked(var data)

    width: GridView.view ? GridView.view.cellWidth - 14 : 140
    height: GridView.view ? GridView.view.cellHeight - 14 : 210
    radius: 8
    color: "#2b2d31"
    border.width: GridView.isCurrentItem ? 2 : 1
    border.color: GridView.isCurrentItem ? "#4d9dff" : "#3a3c40"

    Behavior on border.color { ColorAnimation { duration: 120 } }

    function fetchData() {
        fullData = cardDatabase.cardDataFor(cardId)
    }

    Component.onCompleted: fetchData()
    onCardIdChanged: fetchData()   // needed because GridView.reuseItems recycles this item

    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        Rectangle {
            id: art
            width: parent.width
            height: parent.width * 1.3
            radius: 6
            color: "#1c1d1f"
            clip: true

            Image {
                anchors.fill: parent
                source: root.loaded && root.fullData.picture ? root.fullData.picture : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
            }

            BusyIndicator {
                anchors.centerIn: parent
                width: 22
                height: 22
                running: !root.loaded
                visible: running
            }

            // quantity badge
            Rectangle {
                visible: root.qty > 1
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 4
                width: 22
                height: 22
                radius: 11
                color: "#4d9dff"
                Label {
                    anchors.centerIn: parent
                    text: "x" + root.qty
                    color: "white"
                    font.pixelSize: 10
                    font.bold: true
                }
            }
        }

        Label {
            width: parent.width
            text: root.label
            color: "#e6e6e6"
            font.pixelSize: 12
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.GridView.view.currentIndex = index
            root.cardClicked(root.fullData)
        }
    }
}
