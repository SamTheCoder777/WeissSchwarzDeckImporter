import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    required property string cardId
    property string label: ""
    property bool confirmed: false
    property int qty: 1
    property var preloaded: undefined

    property var fullData: ({})
    readonly property bool loaded: fullData && fullData.picture !== undefined

    signal cardClicked(var data)

    property int pad: 12
    width: 168
    height: 250
    radius: 8
    color: "#2b2d31"
    property bool selected: false
    border.width: selected ? 3 : (mouse.containsMouse ? 2 : 1)
    border.color: selected ? "#4d9dff" : mouse.containsMouse ? "#6fb5ff" : "#3a3c40"
    Behavior on border.color {
        ColorAnimation {
            duration: 120
        }
    }
    Behavior on border.width {
        NumberAnimation {
            duration: 120
        }
    }

    scale: mouse.containsMouse && !selected ? 1.03 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: 120
            easing.type: Easing.OutQuad
        }
    }
    clip: false

    function refresh() {
        var d = cardDatabase.cardDataFor(cardId) || {};
        d.cardId = cardId;
        fullData = Object.assign({}, d);
    }

    Component.onCompleted: {
        cardDatabase.ensureCardData(cardId);
        refresh();
    }
    onCardIdChanged: {
        cardDatabase.ensureCardData(cardId);
        refresh();
    }
    onPreloadedChanged: refresh()

    Connections {
        target: cardDatabase
        function onCardReady(code) {
            if (code === root.cardId){
                root.refresh();
                img.rev++;
            }
        }
        function onLocaleChanged() {
            root.refresh();
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: root.pad
        spacing: 6

        Rectangle {
            id: art
            width: parent.width
            height: parent.width * 1.3
            radius: 6
            color: "#1c1d1f"
            clip: true

            Image {
                id: img
                property int rev: 0
                anchors.fill: parent
                source: root.loaded && root.fullData.cardCode ? "image://cardcache/" + encodeURIComponent(root.fullData.cardCode) + "?r=" + rev: ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
            }
            BusyIndicator {
                anchors.centerIn: parent
                width: 24
                height: 24
                running: img.status === Image.Loading
                visible: running
            }
        }

        Label {
            width: parent.width
            text: {
                if (!root.fullData.cardName)
                    return "Data not available";
                else
                    return root.fullData.cardName;
                //return root.label;
            }
            color: !root.fullData.cardName ? "#8b9096" : "#e6e6e6"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Rectangle {
        visible: root.qty > 1

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 2
        anchors.topMargin: 2
        width: 20
        height: 20
        radius: 10
        color: "#4d9dff"
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
        id: mouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: root.cardClicked(root.fullData)
    }
}
