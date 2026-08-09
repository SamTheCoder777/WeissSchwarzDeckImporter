import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ScrollView {
    id: root
    property var card: ({})
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    Component.onCompleted: {
        var id = card.cardCode || card.cardId;
        if (id)
            cardDatabase.ensureCardData(id);
    }

    onCardChanged: {
        var id = card.cardCode || card.cardId;
        if (id && card.localeAvailable === undefined)
            cardDatabase.ensureCardData(id);
    }

    // Connections {
    //     target: cardDatabase
    //     function onCardReady(code) {
    //         if (code === root.card.cardCode || code === root.card.cardId)
    //             root.card = cardDatabase.cardDataFor(code);
    //     }
    //     function onLocaleChanged() {
    //         var id = root.card.cardCode || root.card.cardId;
    //         if (id)
    //             root.card = cardDatabase.cardDataFor(id);
    //     }
    // }

    component SelText: TextEdit {
        readOnly: true
        selectByMouse: true
        persistentSelection: false
        selectionColor: "#4d9dff"
        selectedTextColor: "white"
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        enabled: text.length > 0
    }

    ColumnLayout {
        width: root.width
        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 200
            Layout.preferredHeight: 280
            radius: 8
            color: "#1c1d1f"
            clip: true
            Image {
                anchors.fill: parent
                source: root.card.cardCode ? "image://cardcache/" + encodeURIComponent(root.card.cardCode) : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
        }

        SelText {
            Layout.fillWidth: true
            text: !root.card.cardName ? "Data not available" : root.card.cardName
            font.pixelSize: 20
            font.bold: true
            color: "#f0f0f0"
        }

        SelText {
            Layout.fillWidth: true
            text: [root.card.setName, root.card.rarity, root.card.cardId].filter(s => !!s).join(" \u00b7 ")
            color: "#9aa0a6"
            font.pixelSize: 13
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3c40"
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 6
            columnSpacing: 12

            Label {
                text: "Color"
                color: "#888"
                font.pixelSize: 12
            }
            SelText {
                Layout.fillWidth: true
                text: root.card.color || "-"
                color: "#e6e6e6"
                font.pixelSize: 12
            }

            Label {
                text: "Power"
                color: "#888"
                font.pixelSize: 12
            }
            SelText {
                Layout.fillWidth: true
                text: root.card.power || "-"
                color: "#e6e6e6"
                font.pixelSize: 12
            }

            Label {
                text: "Soul"
                color: "#888"
                font.pixelSize: 12
            }
            SelText {
                Layout.fillWidth: true
                text: root.card.soul || "-"
                color: "#e6e6e6"
                font.pixelSize: 12
            }

            Label {
                text: "Trigger"
                color: "#888"
                font.pixelSize: 12
            }
            SelText {
                Layout.fillWidth: true
                text: root.card.cardTrigger || "-"
                color: "#e6e6e6"
                font.pixelSize: 12
            }

            Label {
                text: "Feature 1"
                color: "#888"
                font.pixelSize: 12
            }
            SelText {
                Layout.fillWidth: true
                text: root.card.feature1 || "-"
                color: "#e6e6e6"
                font.pixelSize: 12
            }

            Label {
                text: "Feature 2"
                color: "#888"
                font.pixelSize: 12
            }
            SelText {
                Layout.fillWidth: true
                text: root.card.feature2 || "-"
                color: "#e6e6e6"
                font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3c40"
            visible: (root.card.flavor || root.card.text || "") !== ""
        }

        Label {
            text: "Flavor Text"
            color: "#888"
            font.pixelSize: 12
            visible: (root.card.flavor || "") !== ""
        }
        SelText {
            Layout.fillWidth: true
            text: root.card.flavor || ""
            color: "#c9c9c9"
            font.italic: true
            font.pixelSize: 13
            visible: (root.card.flavor || "") !== ""
        }

        Label {
            text: "Card Text"
            color: "#888"
            font.pixelSize: 12
            visible: (root.card.text || "") !== ""
        }
        SelText {
            Layout.fillWidth: true
            text: root.card.text || ""
            color: "#e6e6e6"
            font.pixelSize: 13
            visible: (root.card.text || "") !== ""
        }

        Item {
            Layout.preferredHeight: 12
        }
    }
}
