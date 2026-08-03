import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Index download page. Data from C++: catalog (IndexCatalog).
Rectangle {
    id: root
    color: "#1b1d21"
    implicitWidth: 800
    implicitHeight: 600

    readonly property color accent:    "#4aa3ff"
    readonly property color okColor:   "#3ecf7a"
    readonly property color warnColor: "#f0a340"
    readonly property color panel:     "#24272c"
    readonly property color text1:     "#e8eaed"
    readonly property color text2:     "#9aa0a6"

    Component.onCompleted: catalog.refresh();

    Connections {
        target: catalog
        function onRefreshFinished() {
            catalog.useById(config.curIndexId)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "Card indexes"
                    color: root.text1; font.pixelSize: 20; font.bold: true
                }
                Label {
                    text: catalog.status
                    color: root.text2; font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            Button {
                text: catalog.busy ? "Cancel" : "Check for updates"
                implicitHeight: 34
                onClicked: catalog.busy ? catalog.cancel() : catalog.refresh()
                background: Rectangle {
                    radius: 8
                    color: catalog.busy ? root.warnColor
                         : parent.down ? Qt.darker(root.accent, 1.3)
                         : parent.hovered ? Qt.lighter(root.accent, 1.1) : root.accent
                }
                contentItem: Text {
                    text: parent.text; color: "white"; font.bold: true; font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        TextField {
                    Layout.fillWidth: true
                    placeholderText: "Search indexes… [set title or set code]"
                    color: root.text1
                    placeholderTextColor: root.text2
                    onTextChanged: indexList.setSearch(text)
                    background: Rectangle {
                        radius: 8
                        color: "#24272c"
                        border.color: activeFocus ? root.accent : "#33373d"
                    }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200
            radius: 12
            color: root.panel
            clip: true

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "No indexes found.\nCheck your connection and press “Check for updates”."
                horizontalAlignment: Text.AlignHCenter
                color: root.text2; font.pixelSize: 13
            }

            ListView {
                id: list
                WheelHandler {
                        onWheel: (event) => {
                            list.contentY = Math.max(0,
                                Math.min(list.contentHeight - list.height,
                                         list.contentY - event.angleDelta.y));
                            event.accepted = true;
                        }
                }
                anchors.fill: parent
                anchors.margins: 10
                model: indexList
                spacing: 10
                clip: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 104
                    radius: 10
                    color: "#2b2f35"

                    // statusCode: 0 = not installed, 1 = installed, 2 = update available
                    readonly property bool isInstalled: statusCode === 1
                    readonly property bool hasUpdate:   statusCode === 2

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    spacing: 8
                                    Label {
                                        text: name; color: root.text1
                                        font.pixelSize: 15; font.bold: true
                                    }
                                    Rectangle {
                                        visible: isInstalled || hasUpdate
                                        radius: 4
                                        height: 17
                                        width: tag.implicitWidth + 12
                                        color: hasUpdate ? root.warnColor : root.okColor
                                        Label {
                                            id: tag
                                            anchors.centerIn: parent
                                            text: hasUpdate ? "update available" : "installed"
                                            color: "#12151a"; font.pixelSize: 10; font.bold: true
                                        }
                                    }
                                }
                                Label {
                                    text: desc + "   ·   v" + version + "   ·   " + sizeText
                                        + (installedVersion.length > 0 && hasUpdate
                                           ? "   (you have v" + installedVersion + ")" : "")
                                    color: root.text2; font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                visible: isInstalled || hasUpdate
                                text: "Use"
                                implicitWidth: 62; implicitHeight: 32
                                onClicked: catalog.use(index)
                                background: Rectangle { radius: 7; color: parent.hovered ? "#464c55" : "#3a3f46" }
                                contentItem: Text {
                                    text: parent.text; color: "white"; font.pixelSize: 12; font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            Button {
                                text: downloading ? "Downloading…"
                                     : hasUpdate   ? "Update"
                                     : isInstalled ? "Re-download" : "Download"
                                enabled: !catalog.busy || downloading
                                implicitWidth: 118; implicitHeight: 32
                                onClicked: {
                                    console.log("clicked, catalog.busy =", catalog.busy, "downloading =", downloading, "id =", model.idStr)
                                    downloading ? catalog.cancel() : catalog.downloadById(model.idStr)
                                }
                                background: Rectangle {
                                    radius: 7
                                    color: downloading ? root.warnColor
                                         : hasUpdate   ? root.warnColor
                                         : isInstalled ? "#3a3f46"
                                         : root.accent
                                    opacity: parent.enabled ? 1 : 0.4
                                }
                                contentItem: Text {
                                    text: parent.text; color: "white"; font.pixelSize: 12; font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 6
                            radius: 3
                            color: "#3a3f46"
                            visible: downloading || progress > 0
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, progress))
                                height: parent.height; radius: 3
                                color: progress >= 1 ? root.okColor : root.accent
                                Behavior on width { NumberAnimation { duration: 120 } }
                            }
                        }
                    }
                }
            }
        }

        Label {
            text: "Installed to: " + catalog.installRoot
            color: root.text2; font.pixelSize: 10
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
    }
}
