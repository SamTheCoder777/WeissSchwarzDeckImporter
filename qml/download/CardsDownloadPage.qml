import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Series/card download page — same visual design as DownloadPage.
// Data from C++: catalog (SeriesCatalog), seriesList (SeriesSearchProxy), config.
Rectangle {
    id: root
    color: "#1e1e1e"
    implicitWidth: 800
    implicitHeight: 600

    readonly property color accent:     "#4f6bff"
    readonly property color accentSoft: "#3a4bb8"
    readonly property color warnColor:  "#e0a030"
    readonly property color okBadge:    "#1f5c3a"
    readonly property color warnBadge:  "#5e4416"
    readonly property color panel:      "#1e2123"
    readonly property color card:       "#24272a"
    readonly property color cardHover:  "#2a2e31"
    readonly property color cardBorder: "#31353a"
    readonly property color ghostBorder:"#3d4147"
    readonly property color text1:      "#f0f2f4"
    readonly property color text2:      "#9aa0a6"
    readonly property color text3:      "#676c72"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        // ── header ──────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                RowLayout {
                    spacing: 10
                    Label {
                        text: "Card Indexes"
                        color: root.text1; font.pixelSize: 21; font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        visible: list.count > 0
                        radius: 10
                        height: 20
                        width: countLabel.implicitWidth + 16
                        color: root.panel
                        border.width: 1
                        border.color: root.cardBorder
                        Label {
                            id: countLabel
                            anchors.centerIn: parent
                            text: list.count
                            color: root.text2; font.pixelSize: 11; font.weight: Font.DemiBold
                        }
                    }
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
                text: catalog.busy ? "Cancel" : "Update series list"
                implicitHeight: 36
                implicitWidth: contentItem.implicitWidth + 32
                onClicked: catalog.busy ? catalog.cancel() : catalog.refreshSeriesList()
                background: Rectangle {
                    radius: 8
                    color: catalog.busy
                        ? (refreshBtn.down ? Qt.darker(root.warnColor, 1.2) : root.warnColor)
                        : (refreshBtn.down ? "#111315" : refreshBtn.hovered ? "#2c3033" : "#212427")
                    border.width: catalog.busy ? 0 : 1
                    border.color: refreshBtn.hovered ? "#454a51" : "#3a3e44"
                    Behavior on color { ColorAnimation { duration: 130 } }
                    Behavior on border.color { ColorAnimation { duration: 130 } }
                }
                contentItem: Text {
                    text: refreshBtn.text; color: catalog.busy ? "#1a1300" : root.text1
                    font.pixelSize: 12; font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // ── search ──────────────────────────────────────────────────────────
        TextField {
            id: searchField
            Layout.fillWidth: true
            implicitHeight: 40
            placeholderText: "Search sets… (name or set code)"
            color: root.text1
            placeholderTextColor: root.text3
            leftPadding: 38
            verticalAlignment: TextInput.AlignVCenter
            background: Rectangle {
                radius: 8
                color: root.card
                border.width: 1
                border.color: searchField.activeFocus ? root.accent : root.cardBorder
                Behavior on border.color { ColorAnimation { duration: 130 } }
            }
            Text {
                x: 14; anchors.verticalCenter: parent.verticalCenter
                text: "\u2315"; color: root.text3; font.pixelSize: 17
            }
            onTextChanged: seriesList.setSearch(text)
        }

        // ── list panel ──────────────────────────────────────────────────────
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
                spacing: 6
                Label {
                    text: "No sets found"
                    color: root.text2; font.pixelSize: 14; font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: "Press \u201CUpdate series list\u201D to fetch available sets."
                    color: root.text3; font.pixelSize: 12
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 12
                model: seriesList
                spacing: 10
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {
                    id: vbar
                    policy: ScrollBar.AlwaysOn
                    width: 12
                    implicitWidth: 12
                    anchors.right: parent.right
                    contentItem: Rectangle {
                        implicitWidth: 8
                        radius: 4
                        color: vbar.pressed ? "#8a9099"
                             : vbar.hovered ? "#6e747b" : "#4a4e54"
                        opacity: vbar.active ? 0.9 : 0.4
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

                delegate: Rectangle {
                    id: delegateCard
                    width: ListView.view.width
                    height: 84
                    radius: 10
                    color: hovered ? root.cardHover : root.card
                    border.width: 1
                    border.color: hovered ? "#42474d" : root.cardBorder

                    readonly property bool isDownloaded: statusCode === 1
                    property bool hovered: false

                    Behavior on color { ColorAnimation { duration: 130 } }
                    Behavior on border.color { ColorAnimation { duration: 130 } }

                    HoverHandler { onHoveredChanged: delegateCard.hovered = hovered }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        // set-code chip
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            radius: 6
                            height: 28
                            width: setChip.implicitWidth + 18
                            color: root.panel
                            border.width: 1
                            border.color: root.cardBorder
                            Label {
                                id: setChip
                                anchors.centerIn: parent
                                text: setCode
                                color: root.text2
                                font.pixelSize: 12; font.weight: Font.Bold
                                font.family: "monospace"; font.letterSpacing: 0.5
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            RowLayout {
                                spacing: 8
                                Label {
                                    text: name; color: root.text1
                                    font.pixelSize: 14; font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Rectangle {
                                    visible: delegateCard.isDownloaded
                                    radius: 4; height: 18
                                    width: tag.implicitWidth + 14
                                    color: root.okBadge
                                    Label {
                                        id: tag
                                        anchors.centerIn: parent
                                        text: "DOWNLOADED"
                                        color: "#bdf0cf"
                                        font.pixelSize: 10; font.weight: Font.Bold
                                        font.letterSpacing: 0.3
                                    }
                                }
                            }

                            // progress bar while downloading
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
                                    color: root.accent
                                    Behavior on width { NumberAnimation { duration: 120 } }
                                }
                            }
                        }

                        // action: Download / Re-download / Downloading…
                        Button {
                            id: actionBtn
                            display: AbstractButton.TextBesideIcon
                            enabled: !catalog.busy || downloading
                            implicitWidth: 128; implicitHeight: 32
                            text: downloading ? "Downloading…"
                                 : delegateCard.isDownloaded ? "Re-download" : "Download"
                            icon.source: downloading ? "qrc:/icon/stop.svg"
                                                     : delegateCard.isDownloaded ? "qrc:/icon/refresh.svg"
                                                                                 : "qrc:/icon/download.svg"
                            icon.width: 16
                            icon.height: 16
                            icon.color: "transparent"
                            onClicked: downloading ? catalog.cancel() : catalog.downloadCardList(idStr)

                            background: Rectangle {
                                radius: 7
                                opacity: actionBtn.enabled ? 1 : 0.4
                                color: downloading
                                    ? (actionBtn.down ? Qt.darker(root.warnColor, 1.2) : root.warnColor)
                                    : delegateCard.isDownloaded
                                        ? (actionBtn.hovered ? "#2c3033" : "transparent")
                                        : (actionBtn.down ? "#111315" : actionBtn.hovered ? "#2c3033" : "#212427")
                                border.width: (!downloading && delegateCard.isDownloaded) ? 1 : 0
                                border.color: actionBtn.hovered ? "#4a4f55" : root.ghostBorder
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                            contentItem: Item {
                                anchors.fill: parent
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Image {
                                        source: actionBtn.icon.source
                                        width: actionBtn.icon.width
                                        height: actionBtn.icon.height
                                        sourceSize.width: actionBtn.icon.width
                                        sourceSize.height: actionBtn.icon.height
                                        fillMode: Image.PreserveAspectFit
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: actionBtn.icon.source.toString().length > 0
                                    }
                                    Text {
                                        text: actionBtn.text
                                        color: (!downloading && delegateCard.isDownloaded) ? root.text2
                                             : downloading ? "#1a1300" : root.text1
                                        font.pixelSize: 12; font.weight: Font.DemiBold
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Label {
            text: "Cards saved to local database"
            color: root.text3; font.pixelSize: 10
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
    }
}
