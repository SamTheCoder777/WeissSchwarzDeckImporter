import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "CollectionTools.js" as Tools

Basic.Popup {
    id: root
    property var sourceRows: []
    property bool editable: false
    property var sourceFor: function(card) { return ""; }
    property string kind: "ability"
    property string value: ""
    property string groupMode: "color"
    property string sortMode: "composite"
    property bool descending: true
    property bool customizedSort: false
    readonly property var matches: Tools.matches(sourceRows, kind, value)
    signal cardSelected(var card)

    width: Math.max(1, Math.min(960, parent.width - 32))
    height: Math.max(1, Math.min(720, parent.height - 32))
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 16
    modal: true
    focus: true
    z: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function showReference(searchKind, searchValue, group, sort, down) {
        kind = searchKind;
        value = searchValue;
        if (!customizedSort) {
            groupMode = group;
            sortMode = sort;
            descending = down;
        }
        grid.resetGroups();
        open();
    }
    background: Rectangle {
        color: "#222831"
        border.color: "#536176"
        radius: 12
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: root.value
                textFormat: Text.PlainText
                color: "#efc779"
                font.pixelSize: 18
                font.bold: true
                elide: Text.ElideRight
            }
            ActionButton { text: "×"; onClicked: root.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            DarkCombo {
                Layout.preferredWidth: 160
                model: ["Name contains", "Trait", "Ability contains"]
                currentIndex: ["name", "trait", "ability"].indexOf(root.kind)
                onActivated: function(index) { root.kind = ["name", "trait", "ability"][index]; }
            }
            DarkCombo {
                Layout.preferredWidth: 120
                model: ["Color", "Level", "No groups"]
                currentIndex: ["color", "level", "none"].indexOf(root.groupMode)
                onActivated: function(index) {
                    root.customizedSort = true;
                    root.groupMode = ["color", "level", "none"][index];
                }
            }
            DarkCombo {
                Layout.fillWidth: true
                model: ["Color > level > cost > power", "Name", "Card ID", "Level", "Power"]
                currentIndex: ["composite", "name", "code", "level", "power"].indexOf(root.sortMode)
                onActivated: function(index) {
                    root.customizedSort = true;
                    root.sortMode = ["composite", "name", "code", "level", "power"][index];
                }
            }
            DarkCombo {
                Layout.preferredWidth: 115
                model: ["Descending", "Ascending"]
                currentIndex: root.descending ? 0 : 1
                onActivated: function(index) {
                    root.customizedSort = true;
                    root.descending = index === 0;
                }
            }
        }
        Label {
            text: root.matches.length + " matching cards · current collection"
            color: "#9aa0aa"
            font.pixelSize: 12
        }
        CollectionGrid {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cards: root.matches
            editable: root.editable
            sourceFor: root.sourceFor
            overlayItem: root.parent
            groupMode: root.groupMode
            sortMode: root.sortMode
            descending: root.descending
            onCardSelected: function(card) {
                root.cardSelected(card);
                root.close();
            }
        }
    }
}
