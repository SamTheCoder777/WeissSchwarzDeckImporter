import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Right-hand panel: summary, card list, crop preview, top-15 candidates.
// Data from C++: bridge (UiBridge), selModel, candModel.
Rectangle {
    id: root
    color: "#1b1d21"
    implicitWidth: 470
    implicitHeight: 700

    readonly property color accent:    "#4aa3ff"
    readonly property color okColor:   "#3ecf7a"
    readonly property color warnColor: "#f0a340"
    readonly property color panel:     "#24272c"
    readonly property color text1:     "#e8eaed"
    readonly property color text2:     "#9aa0a6"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // ── summary + export ───────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            Layout.fillHeight: false
            radius: 10
            color: root.panel
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                Label {
                    text: bridge.summaryText
                    color: root.text1
                    font.pixelSize: 13
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                ComboBox{
                    id: indexCombo
                    Layout.fillWidth: true
                    model: installedIndexes
                    textRole: "name"
                    valueRole: "idStr"
                    enabled: count > 0

                    currentIndex: indexOfValue(catalog.activeIndexId)
                    displayText: count ===0 ? "No indexes installed" : currentIndex < 0 ? "Select an index..." : currentText

                    onActivated: catalog.useById(currentValue)
                }

                Button {
                    text: "Export .txt"
                    implicitHeight: 30
                    onClicked: bridge.exportDeck()
                    background: Rectangle {
                        radius: 7
                        color: parent.down ? Qt.darker(root.accent, 1.3)
                             : parent.hovered ? Qt.lighter(root.accent, 1.1) : root.accent
                    }
                    contentItem: Text {
                        text: parent.text; color: "white"; font.bold: true; font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // ── selections (collapsible height, never eats the candidate area) ─
        Label {
            text: "Cards on image"
            color: root.text2; font.pixelSize: 11
            Layout.fillHeight: false
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(110, Math.max(38, selView.count * 34 + 12))
            Layout.fillHeight: false
            radius: 10
            color: root.panel
            clip: true
            ListView {
                id: selView
                WheelHandler {
                        onWheel: (event) => {
                            selView.contentY = Math.max(0,
                                Math.min(selView.contentHeight - selView.height,
                                         selView.contentY - event.angleDelta.y));
                            event.accepted = true;
                        }
                }
                anchors.fill: parent
                anchors.margins: 6
                model: selModel
                spacing: 3
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 31
                    radius: 6
                    color: index === bridge.currentIndex ? Qt.rgba(0.29, 0.64, 1, 0.22) : "transparent"
                    Behavior on color { ColorAnimation { duration: 110 } }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: confirmed ? root.okColor : root.warnColor
                        }
                        Label {
                            text: number + ".  " + label
                            color: confirmed ? root.okColor : root.text1
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: bridge.selectCard(index)
                    }
                }
            }
        }

        // ── crop preview + confirmed state + quantity ──────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            Layout.fillHeight: false
            radius: 10
            color: root.panel
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 96
                    Layout.fillHeight: true
                    radius: 8
                    color: "#111318"
                    border.color: "#33373d"; border.width: 1
                    clip: true
                    MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: bridge.openCompare()
                            Image {
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectFit
                                cache: true
                                source: bridge.currentIndex >= 0
                                        ? "image://crop/current?rev=" + bridge.cropRev : ""
                            }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: bridge.currentIndex < 0
                        text: "no card\nselected"
                        color: root.text2; font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    Label { text: "Your crop"; color: root.text2; font.pixelSize: 11 }
                    Label {
                        text: bridge.confirmedText
                        color: bridge.isConfirmed ? root.okColor : root.warnColor
                        font.pixelSize: 14; font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        spacing: 5
                        Label { text: "Copies"; color: root.text2; font.pixelSize: 11 }
                        Repeater {
                            model: 4
                            Rectangle {
                                width: 32; height: 28; radius: 6
                                opacity: bridge.isConfirmed ? 1 : 0.3
                                color: (index + 1) === bridge.quantity ? root.accent : "#2e3238"
                                Behavior on color { ColorAnimation { duration: 100 } }
                                Label {
                                    anchors.centerIn: parent
                                    text: "x" + (index + 1)
                                    color: (index + 1) === bridge.quantity ? "white" : root.text2
                                    font.pixelSize: 11; font.bold: true
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: bridge.isConfirmed
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: bridge.setQuantity(index + 1)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── candidates: THE stretchy section (gets all remaining space) ────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Label {
                text: "Top matches — pick the correct card"
                color: root.text2; font.pixelSize: 11
                Layout.fillWidth: true
            }
            Label {
                text: candView.count + " results"
                color: root.text2; font.pixelSize: 11
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true              // <- only this expands
            Layout.minimumHeight: 220            // <- and it can never collapse
            radius: 10
            color: root.panel
            clip: true

            Label {
                anchors.centerIn: parent
                visible: candView.count === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: root.text2
                font.pixelSize: 12
                text: bridge.currentIndex < 0
                      ? "Select cards on the image, then press Detect."
                      : "No results for this card yet — press Detect."
            }

            ListView {
                id: candView
                anchors.fill: parent
                anchors.margins: 8
                model: candModel
                spacing: 8
                clip: true
                WheelHandler {
                        onWheel: (event) => {
                            candView.contentY = Math.max(0,
                                Math.min(candView.contentHeight - candView.height,
                                         candView.contentY - event.angleDelta.y));
                            event.accepted = true;
                        }
                }

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 126
                    radius: 10
                    color: isConfirmed ? Qt.rgba(0.24, 0.81, 0.48, 0.16) : "#2b2f35"
                    border.color: isConfirmed ? root.okColor : "transparent"
                    border.width: isConfirmed ? 2 : 0
                    Behavior on color { ColorAnimation { duration: 120 } }

                    Component.onCompleted: if (deckCode) cardDatabase.ensureCardData(deckCode)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 80
                            Layout.fillHeight: true
                            radius: 6
                            color: "#0e1013"
                            clip: true
                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                source: deckCode ? "image://cardcache/" + encodeURIComponent(deckCode) : ""
                                cache: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 3
                            RowLayout {
                                spacing: 6
                                Rectangle {
                                    width: 22; height: 17; radius: 4
                                    color: rank <= 3 ? root.accent : "#3a3f46"
                                    Label {
                                        anchors.centerIn: parent; text: rank
                                        color: "white"; font.pixelSize: 10; font.bold: true
                                    }
                                }
                                Label {
                                    text: deckCode; color: root.text1
                                    font.pixelSize: 14; font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            Label {
                                text: cardId; color: root.text2; font.pixelSize: 10
                                elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            RowLayout {
                                spacing: 8
                                Rectangle {
                                    width: 96; height: 6; radius: 3; color: "#3a3f46"
                                    Rectangle {
                                        width: parent.width * Math.max(0, Math.min(1, score))
                                        height: parent.height; radius: 3
                                        color: score > 0.7 ? root.okColor
                                             : score > 0.6 ? root.accent : root.warnColor
                                    }
                                }
                                Label {
                                    text: score.toFixed(4)
                                    color: root.text2; font.pixelSize: 10
                                }
                            }
                            Item { Layout.fillHeight: true }   // pushes content up, inside a sized parent
                        }

                        Button {
                            Layout.preferredWidth: 104
                            Layout.preferredHeight: 34
                            text: isConfirmed ? "✓ Confirmed" : "Confirm"
                            onClicked: bridge.confirm(index)
                            background: Rectangle {
                                radius: 7
                                color: isConfirmed ? root.okColor
                                     : parent.down ? Qt.darker(root.accent, 1.3)
                                     : parent.hovered ? Qt.lighter(root.accent, 1.15) : "#3a3f46"
                                Behavior on color { ColorAnimation { duration: 100 } }
                            }
                            contentItem: Text {
                                text: parent.text; color: "white"; font.bold: true; font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
