import QtQuick
import QtQuick.Controls
import QtQuick.Layouts


Rectangle {
    id: root
    color: "#1e1e1e"
    implicitWidth: 800
    implicitHeight: 600

    readonly property color line:  "#26282b"
    readonly property color lineSoft: "#1c1e20"
    readonly property color fg:    "#f4f5f6"
    readonly property color fg2:   "#8b9096"
    readonly property color fg3:   "#565b60"
    readonly property color rowHover: "#242628"

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 36
        anchors.rightMargin: 36
        anchors.topMargin: 32
        anchors.bottomMargin: 24
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 20
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: "CARD SETS"
                    color: root.fg; font.pixelSize: 13; font.weight: Font.Bold
                    font.letterSpacing: 2.5
                }
                Label {
                    text: catalog.status
                    color: root.fg3; font.pixelSize: 12
                    elide: Text.ElideRight; Layout.fillWidth: true
                }
            }
            Label {
                text: seriesList.rowCount !== undefined ? "" : ""   // placeholder
                visible: false
            }
            Button {
                id: refreshBtn
                text: catalog.busy ? "CANCEL" : "UPDATE SERIES LIST"
                implicitHeight: 34
                implicitWidth: contentItem.implicitWidth + 28
                onClicked: catalog.busy ? catalog.cancel() : catalog.refreshSeriesList()
                background: Rectangle {
                    color: refreshBtn.down ? "#1c1e20" : refreshBtn.hovered ? "#161719" : "transparent"
                    border.width: 1
                    border.color: catalog.busy ? root.fg2 : (refreshBtn.hovered ? root.fg2 : root.line)
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
                contentItem: Text {
                    text: refreshBtn.text
                    color: catalog.busy ? root.fg : root.fg2
                    font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.2
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: root.line }

        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: "transparent"
            Row {
                anchors.fill: parent
                spacing: 12
                Text {
                    text: "\u2315"; color: root.fg3; font.pixelSize: 18
                    anchors.verticalCenter: parent.verticalCenter
                }
                TextField {
                    id: searchField
                    width: parent.width - 30
                    anchors.verticalCenter: parent.verticalCenter
                    placeholderText: "Search set code or name"
                    color: root.fg; placeholderTextColor: root.fg3
                    font.pixelSize: 14
                    background: Item {}
                    onTextChanged: seriesList.setSearch(text)
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1
                color: searchField.activeFocus ? root.fg : root.line
                Behavior on color { ColorAnimation { duration: 150 } }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 4
            model: seriesList
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: 0

            ScrollBar.vertical: ScrollBar {
                id: vbar
                policy: ScrollBar.AsNeeded
                width: 10; implicitWidth: 10
                anchors.right: parent.right
                contentItem: Rectangle {
                    implicitWidth: 3
                    color: vbar.pressed ? root.fg : vbar.hovered ? root.fg2 : root.fg3
                    opacity: vbar.active ? 0.8 : 0.25
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                }
                background: Rectangle { color: "transparent" }
            }

            WheelHandler {
                onWheel: (event) => {
                    list.contentY = Math.max(0,
                        Math.min(list.contentHeight - list.height,
                                 list.contentY - event.angleDelta.y));
                    event.accepted = true;
                }
            }

            Column {
                anchors.centerIn: parent
                visible: list.count === 0
                spacing: 8
                Text {
                    text: "NO SETS"
                    color: root.fg3; font.pixelSize: 13; font.weight: Font.Bold
                    font.letterSpacing: 2
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: "Press UPDATE SERIES LIST to fetch available sets"
                    color: root.fg3; font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }

            delegate: Item {
                id: rowItem
                width: ListView.view.width - 12
                height: 66

                readonly property bool isDownloaded: statusCode === 1
                property bool hovered: false
                HoverHandler { onHoveredChanged: rowItem.hovered = hovered }

                Rectangle {
                    anchors.fill: parent
                    color: rowItem.hovered ? root.rowHover : "transparent"
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
                Rectangle {
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                    height: 1; color: root.lineSoft
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 6
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: setLabel.implicitWidth + 16; height: 24
                        color: "#242628"; radius: 4
                        Label {
                            id: setLabel
                            anchors.centerIn: parent
                            text: setCode; color: root.fg2
                            font.pixelSize: 11; font.weight: Font.Bold
                            font.family: "monospace"; font.letterSpacing: 1
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            text: name; color: root.fg
                            font.pixelSize: 14; font.weight: Font.DemiBold
                            elide: Text.ElideRight; Layout.fillWidth: true
                        }
                        Label {
                            visible: rowItem.isDownloaded
                            text: "DOWNLOADED"
                            color: root.fg3; font.pixelSize: 9; font.weight: Font.Bold
                            font.letterSpacing: 1.5
                        }

                        Rectangle {
                            visible: downloading
                            Layout.fillWidth: true
                            Layout.rightMargin: 12
                            height: 2; color: root.line
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, progress))
                                height: parent.height; color: root.fg
                                Behavior on width { NumberAnimation { duration: 120 } }
                            }
                        }
                    }

                    Label {
                        id: dlBtn
                        property bool enabled: !catalog.busy || downloading
                        opacity: enabled ? (dlMa.containsMouse ? 1 : 0.75) : 0.3
                        text: downloading ? "DOWNLOADING…"
                             : rowItem.isDownloaded ? "RE-DOWNLOAD" : "DOWNLOAD"
                        color: root.fg
                        font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1.2
                        Layout.rightMargin: 8
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                        MouseArea {
                            id: dlMa
                            anchors.fill: parent; anchors.margins: -8
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: dlBtn.enabled
                            onClicked: catalog.downloadCardList(idStr)
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: root.lineSoft }
        Label {
            Layout.topMargin: 12
            text: "Cards saved to local database"
            color: root.fg3; font.pixelSize: 10; font.family: "monospace"
            Layout.fillWidth: true
        }
    }
}
