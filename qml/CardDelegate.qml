import QtQuick 2.15
import QtQuick.Controls 2.15

// One card cell. Used inside GalleryPage's section Flow.
// Card data is pre-fetched by the parent (preloaded); falls back to cardDataFor().
//
// The quantity badge floats over the TOP-RIGHT corner of the art but is a child
// of `root` (which does NOT clip) and sits in the padding gap around the art, so
// it is never cropped. The art is inset by `pad` on every side to leave room.
Rectangle {
    id: root

    required property string cardId
    property string label: ""
    property bool   confirmed: false
    property int    qty: 1
    property var    preloaded: undefined

    property var fullData: preloaded ? preloaded : ({})
    readonly property bool loaded: fullData && fullData.picture !== undefined

    signal cardClicked(var data)

    // size
    property int pad: 12                 // gap around the art where the badge can live
    width: 168
    height: 250
    radius: 8
    color: "#2b2d31"
    border.width: selected ? 2 : 1
    border.color: selected ? "#4d9dff" : "#3a3c40"
    property bool selected: false
    Behavior on border.color { ColorAnimation { duration: 120 } }
    // IMPORTANT: root must NOT clip, so the badge can overhang the art.
    clip: false

    function ensureData() {
        if (!preloaded || preloaded.picture === undefined)
            fullData = cardDatabase.cardDataFor(cardId)
    }
    Component.onCompleted: ensureData()
    onCardIdChanged: ensureData()

    Column {
        anchors.fill: parent
        anchors.margins: root.pad        // inset so the art doesn't touch card edges
        spacing: 6

        Rectangle {
            id: art
            width: parent.width
            height: parent.width * 1.3
            radius: 6
            color: "#1c1d1f"
            clip: true                   // only the ART clips (keeps the image tidy)

            Image {
                anchors.fill: parent
                source: root.loaded && root.fullData.picture ? root.fullData.picture : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
            }
            BusyIndicator {
                anchors.centerIn: parent
                width: 24; height: 24
                running: !root.loaded
                visible: running
            }
        }

        Label {
            width: parent.width
            text: root.label
            color: "#e6e6e6"
            font.pixelSize: 13
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // ── quantity badge: child of ROOT (not art). Sits in the top-right PAD gap
    //    — outside the image, but fully inside the card bounds, so no clip crops
    //    it. It overlaps the art's top-right corner slightly. ─────────────────
    Rectangle {
        visible: root.qty > 1
        // anchor to the card's top-right, fully inside root (no negative overhang)
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 2
        anchors.topMargin: 2
        width: 20; height: 20; radius: 17
        color: "#4d9dff"
        // border.color: "#1e1f22"
        // border.width: 2
        z: 20
        Label {
            anchors.centerIn: parent
            text: "x" + root.qty
            color: "white"
            font.pixelSize: 12
            font.bold: true
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.cardClicked(root.fullData)
    }
}
