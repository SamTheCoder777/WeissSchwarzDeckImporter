import QtQuick
import QtQuick.Controls
import "CollectionTools.js" as Tools

Item {
    id: root
    property var cards: []
    property bool editable: false
    property string selectedCode: ""
    property string groupMode: "color"
    property string sortMode: "composite"
    property bool descending: true
    property var sourceFor: function (card) {
        return "";
    }
    property Item overlayItem: null
    property var collapsed: ({})
    property var displayRows: []

    // Both tabs share these artwork dimensions. Controls only add footer height.
    readonly property real cardSizeScale: galleryController.cardScalePercent / 100.0

    readonly property int preferredTileWidth: Math.round(150 * cardSizeScale)
    readonly property int artworkHeight: Math.round(168 * cardSizeScale)
    readonly property int tileHeight: (editable ? 260 : 208) + (artworkHeight - 168)
    readonly property real tileWidth: Math.max(1, Math.min(preferredTileWidth, list.width - 12))
    readonly property int columns: Math.max(1, Math.floor((Math.max(1, list.width - 12) + 8) / (preferredTileWidth + 8)))
    signal cardSelected(var card)

    function resetGroups() {
        collapsed = {};
    }
    function rebuild() {
        var position = list.contentY;
        displayRows = Tools.visualRows(cards, groupMode, sortMode, descending, columns, collapsed);
        Qt.callLater(function () {
            list.contentY = Math.max(list.originY, Math.min(position, Math.max(list.originY, list.contentHeight - list.height)));
        });
        preview.dismiss();
    }
    onCardsChanged: refresh.restart()
    onColumnsChanged: refresh.restart()
    onGroupModeChanged: {
        collapsed = {};
        refresh.restart();
    }
    onSortModeChanged: refresh.restart()
    onDescendingChanged: refresh.restart()
    onCollapsedChanged: refresh.restart()
    onVisibleChanged: if (!visible)
        preview.dismiss()
    Timer {
        id: refresh
        interval: 20
        onTriggered: root.rebuild()
    }
    Component.onCompleted: refresh.restart()

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 8
        model: root.displayRows
        cacheBuffer: height
        ScrollBar.vertical: ScrollBar {}
        onContentYChanged: preview.dismiss()

        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse

            onWheel: function (event) {
                const view = list;

                const distance = event.pixelDelta.y !== 0 ? event.pixelDelta.y : (event.angleDelta.y / 120) * 90;

                if (distance === 0) {
                    event.accepted = false;
                    return;
                }

                const minimum = view.originY;
                const maximum = Math.max(minimum, minimum + view.contentHeight - view.height);

                view.cancelFlick();

                view.contentY = Math.max(minimum, Math.min(maximum, view.contentY - distance));

                event.accepted = true;
            }
        }

        delegate: Item {
            id: gridRow
            required property var modelData
            width: list.width
            height: modelData.header ? 34 : root.tileHeight

            CollectionHeader {
                anchors.fill: parent
                visible: gridRow.modelData.header
                title: gridRow.modelData.label
                cardCount: gridRow.modelData.count
                collapsed: !!root.collapsed[gridRow.modelData.key]
                markerColor: root.groupMode === "color" ? ["#df6672", "#e1bd50", "#62b780", "#669de0", "#b692dc", "#8b95a4"][Number(gridRow.modelData.key)] : "#8494ab"
                onClicked: {
                    var next = Object.assign({}, root.collapsed);
                    next[gridRow.modelData.key] = !next[gridRow.modelData.key];
                    root.collapsed = next;
                }
            }
            Row {
                x: 0
                visible: !gridRow.modelData.header
                spacing: 8
                Repeater {
                    model: gridRow.modelData.cards
                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        width: root.tileWidth
                        height: root.tileHeight
                        radius: 8
                        color: infoHover.hovered ? "#2d343f" : "#262b33"
                        border.color: root.selectedCode === modelData.cardCode ? "#4d9dff" : "#3e4754"
                        border.width: root.selectedCode === modelData.cardCode ? 2 : 1

                        Column {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 4
                            Item {
                                id: info
                                width: parent.width
                                height: root.artworkHeight + 4 + 24 + (root.editable ? 16 : 0)
                                readonly property bool hovered: infoHover.hovered
                                HoverHandler {
                                    id: infoHover
                                    cursorShape: Qt.PointingHandCursor
                                    onHoveredChanged: {
                                        if (hovered)
                                            preview.queue(info, tile.modelData);
                                        else
                                            preview.leave(info);
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        preview.dismiss();
                                        root.cardSelected(tile.modelData);
                                    }
                                }
                                Column {
                                    anchors.fill: parent
                                    spacing: 4
                                    Item {
                                        id: artworkSlot
                                        width: parent.width
                                        height: root.artworkHeight
                                        clip: false
                                        Image {
                                            id: cardImage
                                            anchors.fill: parent
                                            // Symmetric margins center the artwork in its tile.
                                            anchors.margins: 8
                                            source: root.sourceFor(tile.modelData)
                                            sourceSize: Qt.size(300, 420)
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectFit
                                            horizontalAlignment: Image.AlignHCenter
                                            verticalAlignment: Image.AlignVCenter
                                        }
                                        Rectangle {
                                            visible: !root.editable && Number(tile.modelData.copies) > 0 && cardImage.status === Image.Ready && cardImage.paintedWidth > 0 && cardImage.paintedHeight > 0
                                            x: cardImage.x + (cardImage.width + cardImage.paintedWidth) / 2 - width * 0.425
                                            y: cardImage.y + (cardImage.height - cardImage.paintedHeight) / 2 - height * 0.575
                                            width: Math.max(22, quantityText.implicitWidth + 8)
                                            height: width
                                            radius: width / 2
                                            color: "#4d9dff"
                                            border.color: "#202631"
                                            border.width: 1
                                            z: 4
                                            Label {
                                                id: quantityText
                                                anchors.centerIn: parent
                                                text: String(tile.modelData.copies)
                                                textFormat: Text.PlainText
                                                font.pixelSize: 10
                                                font.bold: true
                                                color: "white"
                                            }
                                        }
                                    }
                                    Label {
                                        width: parent.width
                                        height: 24
                                        text: tile.modelData.cardName || tile.modelData.cardCode
                                        textFormat: Text.PlainText
                                        color: "#e6e6e6"
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        width: parent.width
                                        height: 12
                                        visible: root.editable
                                        text: tile.modelData.cardCode
                                        textFormat: Text.PlainText
                                        color: "#9aa0aa"
                                        font.pixelSize: 9
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            QuantityButtons {
                                width: parent.width
                                height: 32
                                visible: root.editable
                                card: tile.modelData
                            }
                        }
                    }
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: root.cards.length === 0
            text: "No cards to display"
            color: "#9aa0aa"
        }
    }
    CardHoverPreview {
        id: preview
        overlayItem: root.overlayItem
        sourceFor: root.sourceFor
    }
}
