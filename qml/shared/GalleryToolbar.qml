import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Item {
    id: root
    property alias currentIndex: tabs.currentIndex
    property var activePane: null
    property Item popupParent: null
    readonly property bool deckMode: currentIndex === 1
    signal importRequested(string format)
    signal exportRequested(string format)
    implicitHeight: 46
    property int popupGeneration: 0

    function openPopup(popup, anchor) {
        closePopups();
        var generation = popupGeneration;
        Qt.callLater(function () {
            if (generation !== root.popupGeneration || !root.visible)
                return;
            var point = anchor.mapToItem(popup.parent, anchor.width, anchor.height + 6);
            popup.x = Math.max(8, Math.min(point.x - popup.width, popup.parent.width - popup.width - 8));
            popup.y = Math.max(8, Math.min(point.y, popup.parent.height - popup.height - 8));
            popup.open();
        });
    }
    function chooseAction(format) {
        var importing = actions.mode === "import";
        actions.close();
        if (importing)
            importRequested(format);
        else
            exportRequested(format);
    }
    function closePopups() {
        ++popupGeneration;
        if (actions)
            actions.close();
        if (options)
            options.close();
    }
    onCurrentIndexChanged: closePopups()
    onVisibleChanged: if (!visible)
        closePopups()

    RowLayout {
        anchors.fill: parent
        spacing: 8
        Basic.TabBar {
            id: tabs
            Layout.preferredWidth: 320
            Layout.minimumWidth: 190
            Layout.maximumWidth: 380
            Layout.fillHeight: true
            currentIndex: 0
            spacing: 4
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: "#3d4654"
                }
            }
            Repeater {
                model: ["Detected Cards", "My Deck"]
                delegate: Basic.TabButton {
                    id: tab
                    required property int index
                    required property string modelData
                    text: modelData
                    hoverEnabled: true
                    leftPadding: 12
                    rightPadding: 12
                    contentItem: RowLayout {
                        spacing: 8
                        Label {
                            Layout.fillWidth: true
                            text: tab.modelData
                            color: tab.checked ? "#f0f4fa" : "#9ba7b8"
                            font.pixelSize: 13
                            font.bold: tab.checked
                            elide: Text.ElideRight
                        }
                        Label {
                            text: tab.index === 0 ? galleryController.detectedRows.length : deckBuilder.totalCards
                            color: tab.checked ? "#80baff" : "#7d899b"
                            font.pixelSize: 11
                        }
                    }
                    background: Rectangle {
                        color: tab.checked ? "#272f3a" : tab.hovered ? "#242b34" : "transparent"
                        radius: 5
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 3
                            color: tab.checked ? "#4d9dff" : "transparent"
                        }
                    }
                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
        }
        ActionButton {
            visible: !root.deckMode
            text: root.width < 720 ? "Copy" : "Copy to My Deck"
            enabled: !deckBuilder.exporting && galleryController.detectedRows.length > 0
            onClicked: galleryController.transferDetected(false)
        }
        ActionButton {
            id: importButton
            text: "Import ▾"
            enabled: !deckBuilder.exporting
            onClicked: {
                actions.mode = "import";
                root.openPopup(actions, importButton);
            }
        }
        ActionButton {
            id: exportButton
            text: "Export ▾"
            primary: true
            enabled: !deckBuilder.exporting
            onClicked: {
                actions.mode = "export";
                root.openPopup(actions, exportButton);
            }
        }
        ActionButton {
            id: viewButton
            text: "⋯"
            Accessible.name: "View options"
            onClicked: root.openPopup(options, viewButton)
        }
    }

    Basic.Popup {
        id: actions
        property string mode: "export"
        parent: root.popupParent || root.parent
        popupType: Popup.Item
        width: Math.max(1, Math.min(230, parent.width - 16))
        height: actions.mode === "import" ? 84 : 120
        padding: 8
        z: 100
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 8
            color: "#282f39"
            border.color: "#536176"
        }
        contentItem: ColumnLayout {
            spacing: 4
            ActionButton {
                Layout.fillWidth: true
                text: actions.mode === "import" ? "Open .txt in My Deck" : "Export .txt"
                onClicked: root.chooseAction("txt")
            }
            ActionButton {
                Layout.fillWidth: true
                text: actions.mode === "import" ? "Paste deck code" : "Export .png"
                onClicked: root.chooseAction(actions.mode === "import" ? "code" : "png")
            }
            ActionButton {
                Layout.fillWidth: true
                visible: actions.mode === "export"
                text: "Copy deck code"
                onClicked: root.chooseAction("code")
            }
        }
    }

    Basic.Popup {
        id: options
        parent: root.popupParent || root.parent
        popupType: Popup.Item
        width: Math.max(1, Math.min(310, parent.width - 16))
        height: contentItem.implicitHeight + topPadding + bottomPadding
        padding: 14
        z: 100
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 9
            color: "#252d38"
            border.color: "#536176"
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "View options"
                color: "#e6e6e6"
                font.bold: true
                font.pixelSize: 14
            }
            Label {
                visible: !root.deckMode
                text: "Card data"
                color: "#9aa8ba"
                font.pixelSize: 11
            }
            DarkCombo {
                Layout.fillWidth: true
                visible: !root.deckMode
                model: ["EncoreDecks", "Local database"]
                currentIndex: cardDatabase.cardDataSource === "local" ? 1 : 0
                onActivated: function (index) {
                    cardDatabase.setCardDataSource(index ? "local" : "encore");
                }
            }
            DarkCombo {
                Layout.fillWidth: true
                visible: !root.deckMode
                model: ["Detected edition: JP", "Detected edition: EN"]
                currentIndex: galleryController.detectedEdition === "en" ? 1 : 0
                onActivated: function (index) {
                    galleryController.detectedEdition = index ? "en" : "jp";
                }
            }
            DarkCombo {
                Layout.fillWidth: true
                model: ["Display text: EN", "Display text: JP"]
                currentIndex: (root.deckMode ? deckBuilder.language : cardDatabase.locale) === "JP" ? 1 : 0
                onActivated: function (index) {
                    if (root.deckMode)
                        deckBuilder.setLanguage(index ? "JP" : "EN");
                    else
                        cardDatabase.setLocale(index ? "JP" : "EN");
                }
            }
            Label {
                text: "Collections"
                color: "#9aa8ba"
                font.pixelSize: 11
            }
            Label {
                text: "Card size"
                color: "#9aa8ba"
                font.pixelSize: 11
            }

            DarkCombo {
                id: cardSizeCombo

                Layout.fillWidth: true

                readonly property var percentages: [80, 100, 110, 120, 130, 140, 150, 175, 200]

                model: ["80%", "100%", "110%", "120%", "130%", "140%", "150%", "175%", "200%"]

                currentIndex: percentages.indexOf(galleryController.cardScalePercent)

                // Also displays a custom percentage loaded from config.ini.
                displayText: galleryController.cardScalePercent + "%"

                onActivated: function (index) {
                    galleryController.cardScalePercent = cardSizeCombo.percentages[index];

                    currentIndex = Qt.binding(function () {
                        return cardSizeCombo.percentages.indexOf(galleryController.cardScalePercent);
                    });
                }
            }
            DarkCombo {
                Layout.fillWidth: true
                model: ["Group by color", "Group by level", "No collections"]
                currentIndex: root.activePane ? ["color", "level", "none"].indexOf(root.activePane.groupMode) : 0
                onActivated: function (index) {
                    if (root.activePane)
                        root.activePane.groupMode = ["color", "level", "none"][index];
                }
            }
            Label {
                text: "Sort within collections"
                color: "#9aa8ba"
                font.pixelSize: 11
            }
            DarkCombo {
                Layout.fillWidth: true
                model: ["Color > level > cost > power", "Name", "Card ID", "Level", "Power"]
                currentIndex: root.activePane ? ["composite", "name", "code", "level", "power"].indexOf(root.activePane.sortMode) : 0
                onActivated: function (index) {
                    if (root.activePane)
                        root.activePane.sortMode = ["composite", "name", "code", "level", "power"][index];
                }
            }
            DarkCombo {
                Layout.fillWidth: true
                model: ["Descending", "Ascending"]
                currentIndex: root.activePane && !root.activePane.descending ? 1 : 0
                onActivated: function (index) {
                    if (root.activePane)
                        root.activePane.descending = index === 0;
                }
            }
            ActionButton {
                Layout.fillWidth: true
                visible: root.deckMode
                text: "Start a new deck"
                enabled: !deckBuilder.exporting
                onClicked: {
                    options.close();
                    deckBuilder.newDeck();
                }
            }
        }
    }
}
