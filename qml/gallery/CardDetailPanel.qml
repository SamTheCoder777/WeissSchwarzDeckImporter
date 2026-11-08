import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../shared" as Shared
import "../shared/TriggerIcons.js" as Triggers
import "../shared/ReferenceText.js" as References

ScrollView {
    id: root
    property var card: ({})
    property bool requestCardData: true
    property var imageSourceOverride: undefined
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    signal referenceRequested(string kind, string value)

    // for ref highlighting
    function escapeHtml(value) {
        return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;");
    }

    function referenceHtml(value, hoveredReference) {
        return References.referenceHtml(value, hoveredReference);
    }

    function activateReference(link) {
        var match = /^cardref:(trait|name|ability):(.*)$/.exec(link);

        if (!match)
            return;

        var value;

        try {
            value = decodeURIComponent(match[2]).trim();
        } catch (error) {
            return;
        }

        if (value.length === 0)
            return;

        root.referenceRequested(match[1], value);
    }
    // ----------End of Ref Highlighting----------

    Component.onCompleted: {
        var id = card.cardCode || card.cardId;
        if (root.requestCardData && id)
            cardDatabase.ensureCardData(id);
    }

    onCardChanged: {
        var id = card.cardCode || card.cardId;
        if (root.requestCardData && id && card.localeAvailable === undefined)
            cardDatabase.ensureCardData(id);
    }

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
                source: {
                    if (root.imageSourceOverride !== undefined)
                        return root.imageSourceOverride;

                    var revision = cardDatabase.dataRevision;

                    return cardDatabase.cardImageSource(root.card.cardCode || root.card.cardId || "");
                }
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
                text: "Cost"
                color: "#888"
                font.pixelSize: 12
            }
            SelText {
                Layout.fillWidth: true
                text: root.card.cost || "-"
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
                textFormat: TextEdit.RichText
                text: Triggers.inlineText(root.card.soul || "-", 14)
                color: "#e6e6e6"
                font.pixelSize: 12
            }

            Label {
                text: "Trigger"
                color: "#888"
                font.pixelSize: 12
            }
            Shared.TriggerIconRow {
                Layout.fillWidth: true
                value: root.card.cardTrigger || ""
                iconSize: 14
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
            id: abilityText

            Layout.fillWidth: true

            readonly property string referenceUnderPointer: referenceHover.hovered ? abilityText.linkAt(referenceHover.point.position.x, referenceHover.point.position.y) : ""

            textFormat: TextEdit.RichText

            text: root.referenceHtml(root.card.text || "", abilityText.referenceUnderPointer)

            color: "#e6e6e6"
            font.pixelSize: 13

            visible: (root.card.text || "") !== ""

            onLinkActivated: function (link) {
                root.activateReference(link);
            }

            HoverHandler {
                id: referenceHover

                cursorShape: abilityText.referenceUnderPointer !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
            }
        }

        Item {
            Layout.preferredHeight: 12
        }
    }
}
