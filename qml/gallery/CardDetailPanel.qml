import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ScrollView {
    id: root
    property var card: ({})
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    signal referenceRequested(string kind, string value)

    // for ref highlighting
    function escapeHtml(value) {
        return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;");
    }

    function referenceHtml(value, hoveredReference) {
        var input = String(value || "").replace(/<[/\\]?\s*br\s*[\/\\]?>/gi, "\n");

        var pattern = /《([^《》\r\n]+)》|「([^「」\r\n]+)」|『([^『』\r\n]+)』/g;

        var output = "";
        var previous = 0;
        var match;

        while ((match = pattern.exec(input)) !== null) {
            output += escapeHtml(input.slice(previous, match.index));

            var kind;
            var term;
            var color;

            if (match[1] !== undefined) {
                kind = "trait";
                term = match[1];
                color = "#77d9a0";
            } else if (match[2] !== undefined) {
                kind = "name";
                term = match[2];
                color = "#80baff";
            } else {
                kind = "ability";
                term = match[3];
                color = "#efc779";
            }

            var link = "cardref:" + kind + ":" + encodeURIComponent(term);

            var hovered = link === hoveredReference;

            var background = kind === "trait" ? "#294638" : kind === "name" ? "#293e56" : "#49402b";

            var style = "color:" + color + "; text-decoration:none;";

            if (hovered)
                style += " background-color:" + background + ";";

            output += '<a href="' + escapeHtml(link) + '"' + ' style="' + style + '">' + escapeHtml(match[0]) + "</a>";

            previous = pattern.lastIndex;
        }

        output += escapeHtml(input.slice(previous));

        return output.replace(/\r\n/g, "\n").replace(/\r/g, "\n").replace(/\n/g, "<br>");
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
        if (id)
            cardDatabase.ensureCardData(id);
    }

    onCardChanged: {
        var id = card.cardCode || card.cardId;
        if (id && card.localeAvailable === undefined)
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
