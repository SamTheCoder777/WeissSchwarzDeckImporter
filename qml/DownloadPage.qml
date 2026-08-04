import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Index download page. Data from C++: catalog (IndexCatalog), config (Config).
Rectangle {
    id: root
    color: "#1b1d21"
    implicitWidth: 800
    implicitHeight: 600

    // --- Design tokens -------------------------------------------------
    // One accent, reserved only for "needs your attention" (Update).
    // Everything else is neutral: dark/filled for primary actions,
    // ghost/quiet for optional ones (Use, Re-download).
    readonly property color accent:     "#4f6bff"   // Update only
    readonly property color warnColor:  "#e0a030"   // active download / cancel only
    readonly property color okBadge:    "#2f7a4d"
    readonly property color warnBadge:  "#7a5a1f"
    readonly property color panel:      "#232629"
    readonly property color card:       "#2a2d31"
    readonly property color cardBorder: "#34383d"
    readonly property color ghostBorder:"#3d4147"
    readonly property color text1:      "#eceef0"
    readonly property color text2:      "#9aa0a6"
    readonly property color text3:      "#6b7076"

    Component.onCompleted: catalog.refresh();

    Connections {
        target: catalog
        function onRefreshFinished() {
            catalog.useById(config.curIndexId)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // --- Header: title + status, refresh/cancel toggle -------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "Card indexes"
                    color: root.text1; font.pixelSize: 19; font.weight: Font.DemiBold
                }
                Label {
                    text: catalog.status
                    color: root.text2; font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Button {
                id: refreshBtn
                text: catalog.busy ? "Cancel" : "Check for updates"
                icon.source: catalog.busy ? "qrc:/icon/stop.svg" : "qrc:/icon/refresh.svg"
                implicitHeight: 34
                implicitWidth: contentItem.implicitWidth + 28
                onClicked: catalog.busy ? catalog.cancel() : catalog.refresh()
                background: Rectangle {
                    radius: 8
                    color: catalog.busy
                        ? (parent.down ? Qt.darker(root.warnColor, 1.2) : root.warnColor)
                        : (parent.down ? "#000" : parent.hovered ? "#2c2f33" : "#1f2226")
                    border.width: catalog.busy ? 0 : 1
                    border.color: "#3a3e44"
                }
                contentItem: Text {
                    text: parent.text; color: catalog.busy ? "#1a1300" : root.text1
                    font.pixelSize: 12; font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // --- Search ------------------------------------------------------
        TextField {
            Layout.fillWidth: true
            placeholderText: "Search indexes… (set title or set code)"
            color: root.text1
            placeholderTextColor: root.text3
            leftPadding: 34
            background: Rectangle {
                radius: 8
                color: root.card
                border.width: 1
                border.color: activeFocus ? root.accent : root.cardBorder
            }
            onTextChanged: indexList.setSearch(text)
        }

        // --- List card -----------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200
            radius: 12
            color: root.panel
            border.width: 1
            border.color: root.cardBorder
            clip: true

            ColumnLayout {
                anchors.centerIn: parent
                visible: list.count === 0
                spacing: 4
                Label {
                    text: "No indexes found"
                    color: root.text2; font.pixelSize: 14; font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: "Check your connection and press \u201CCheck for updates.\u201D"
                    color: root.text3; font.pixelSize: 12
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 12
                model: indexList
                spacing: 10
                clip: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                WheelHandler {
                    onWheel: (event) => {
                        list.contentY = Math.max(0,
                            Math.min(list.contentHeight - list.height,
                                     list.contentY - event.angleDelta.y));
                        event.accepted = true;
                    }
                }

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 92
                    radius: 10
                    color: root.card
                    border.width: 1
                    border.color: root.cardBorder

                    readonly property bool isInstalled: statusCode === 1
                    readonly property bool hasUpdate:   statusCode === 2

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        // --- name / tag / description -----------------
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            RowLayout {
                                spacing: 8
                                Label {
                                    text: name; color: root.text1
                                    font.pixelSize: 14; font.weight: Font.DemiBold
                                }
                                Rectangle {
                                    visible: isInstalled || hasUpdate
                                    radius: 4
                                    height: 18
                                    width: tag.implicitWidth + 14
                                    color: hasUpdate ? root.warnBadge : root.okBadge
                                    Label {
                                        id: tag
                                        anchors.centerIn: parent
                                        text: hasUpdate ? "UPDATE AVAILABLE" : "INSTALLED"
                                        color: hasUpdate ? "#fcd9a0" : "#bdf0cf"
                                        font.pixelSize: 10; font.weight: Font.Bold
                                        font.letterSpacing: 0.3
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

                            // progress bar sits under the description while downloading
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                height: 5
                                radius: 3
                                color: "#3a3e44"
                                visible: downloading
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(1, progress))
                                    height: parent.height; radius: 3
                                    color: root.text1
                                    Behavior on width { NumberAnimation { duration: 120 } }
                                }
                            }
                        }

                        // --- Use button: quiet, only when relevant -----
                        Button {
                            visible: (isInstalled || hasUpdate) && !downloading
                            enabled: !catalog.busy
                            text: "Use"
                            implicitWidth: 64; implicitHeight: 32
                            onClicked: catalog.useById(model.idStr)
                            background: Rectangle {
                                radius: 7
                                color: "transparent"
                                border.width: 1
                                border.color: root.ghostBorder
                                opacity: parent.enabled ? 1 : 0.4
                            }
                            contentItem: Text {
                                text: parent.text; color: root.text2
                                font.pixelSize: 12; font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        // --- main action: three-tier hierarchy ---------
                        // Download / Downloading = neutral+warn; Update = the one
                        // accent color; Re-download = quiet ghost, same tier as Use.
                        Button {
                            id: actionBtn
                            enabled: !catalog.busy || downloading
                            implicitWidth: 128; implicitHeight: 32
                            text: downloading ? "Downloading…"
                                 : hasUpdate   ? "Update"
                                 : isInstalled ? "Re-download" : "Download"
                            icon.source: downloading ? "qrc:/icon/stop.svg"
                                                     : hasUpdate ? "qrc:/icon/update.svg"
                                                     : isInstalled ? "qrc:/icon/refresh.svg" : "qrc:/icon/download.svg"

                            onClicked: downloading ? catalog.cancel() : catalog.downloadById(model.idStr)

                            background: Rectangle {
                                radius: 7
                                opacity: parent.enabled ? 1 : 0.4
                                color: downloading
                                    ? (parent.down ? Qt.darker(root.warnColor, 1.2) : root.warnColor)
                                    : hasUpdate
                                        ? (parent.down ? Qt.darker(root.accent, 1.2) : parent.hovered ? Qt.lighter(root.accent, 1.1) : root.accent)
                                        : isInstalled
                                            ? "transparent"
                                            : (parent.down ? "#000" : parent.hovered ? "#2c2f33" : "#1f2226")
                                border.width: (!downloading && isInstalled) ? 1 : 0
                                border.color: root.ghostBorder
                            }
                            contentItem: Text {
                                text: parent.text
                                color: (!downloading && isInstalled) ? root.text2
                                     : downloading ? "#1a1300" : root.text1
                                font.pixelSize: 12; font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }

        // --- Footer --------------------------------------------------------
        Label {
            text: "Installed to: " + catalog.installRoot
            color: root.text3; font.pixelSize: 10
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
    }
}
