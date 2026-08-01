import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Sectioned gallery: Level 3 / 2 / 1 / 0 / Climax.
// Within each section: grouped by color, sorted by power (big -> small).
// Context properties: cardDatabase (DatabaseUtil), selModel (SelectionModel), bridge (UiBridge)
Item {
    id: root
    anchors.fill: parent

    property var selectedCard: ({})
    readonly property bool hasSelection: selectedCard && Object.keys(selectedCard).length > 0

    // ── rebuild the sectioned structure whenever the model changes ──────────
    property var sections: []          // [{ title, cards: [ {cardId,label,qty,confirmed,data} ] }]

    function powerNum(d) {
        var p = d && d.power !== undefined ? parseInt(d.power, 10) : NaN;
        return isNaN(p) ? -1 : p;
    }
    function colorOf(d) {
        if (!d || !d.color) return "zzz";                 // unknowns sort last
        var m = /\[\[(\w+)\.gif\]\]/.exec(d.color);
        return m ? m[1] : d.color;
    }
    function isClimax(d) {
        return d && String(d.cardKind) === "4";           // card_kind 4 == Climax
    }
    function levelOf(d) {
        if (!d || isClimax(d)) return null;                // climax -> its own section
        var lv = d.level;
        if (lv === undefined || lv === null || lv === "" || isNaN(parseInt(lv, 10)))
            return 0;                                      // missing level -> treat as 0
        return parseInt(lv, 10);
    }

    function rebuild() {
        // 1. harvest rows from selModel joined with card data
        var items = [];
        for (var i = 0; i < selModel.rowCount(); ++i) {
            var cardId = selModel.dataAt(i, "cardId");
            if (!cardId || cardId.length === 0) continue;  // skip unconfirmed
            var d = cardDatabase.cardDataFor(cardId);
            items.push({
                cardId:    cardId,
                label:     selModel.dataAt(i, "label"),
                qty:       selModel.dataAt(i, "qty"),
                confirmed: selModel.dataAt(i, "confirmed"),
                data:      d
            });
        }

        // 2. bucket by level (null = climax)
        var byLevel = {};              // key: level int or "cx"
        for (var j = 0; j < items.length; ++j) {
            var lv = levelOf(items[j].data);
            var key = (lv === null) ? "cx" : String(lv);
            (byLevel[key] = byLevel[key] || []).push(items[j]);
        }

        // 3. sort each bucket: color group, then power desc
        function sortBucket(arr) {
            arr.sort(function(a, b) {
                var ca = colorOf(a.data), cb = colorOf(b.data);
                if (ca !== cb) return ca < cb ? -1 : 1;    // group by color
                return powerNum(b.data) - powerNum(a.data);// big power -> small
            });
        }

        // 4. emit sections in the order: highest level -> 0, then Climax
        var out = [];
        var levels = Object.keys(byLevel)
            .filter(function(k){ return k !== "cx"; })
            .map(function(k){ return parseInt(k,10); })
            .sort(function(a,b){ return b - a; });         // 3,2,1,0
        for (var L = 0; L < levels.length; ++L) {
            var arr = byLevel[String(levels[L])];
            sortBucket(arr);
            out.push({ title: "Level " + levels[L], cards: arr });
        }
        if (byLevel["cx"]) {
            sortBucket(byLevel["cx"]);
            out.push({ title: "Climax", cards: byLevel["cx"] });
        }
        root.sections = out;
    }

    Component.onCompleted: rebuild()
    Connections {
        target: selModel
        function onModelReset() { root.rebuild() }
        function onDataChanged() { root.rebuild() }
        function onRowsInserted() { root.rebuild() }
        function onRowsRemoved() { root.rebuild() }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // ---------------- LEFT: sectioned gallery ----------------
        Rectangle {
            SplitView.preferredWidth: parent.width * 0.62
            SplitView.minimumWidth: 340
            color: "#1e1f22"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // export bar
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    color: "#26282c"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        Label {
                            text: "Detected cards"
                            color: "#e6e6e6"; font.pixelSize: 15; font.bold: true
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "Export .txt"
                            onClicked: bridge.exportDeck()
                            background: Rectangle {
                                radius: 7
                                color: parent.down ? "#3a7fd0"
                                     : parent.hovered ? "#5aa8ff" : "#4d9dff"
                            }
                            contentItem: Text {
                                text: parent.text; color: "white"; font.bold: true; font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                ScrollView {
                    id: scroller
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    Column {
                        width: scroller.availableWidth
                        spacing: 6
                        padding: 14

                        Repeater {
                            model: root.sections
                            delegate: Column {
                                required property var modelData
                                width: parent.width
                                spacing: 8

                                // section header
                                Rectangle {
                                    width: parent.width
                                    height: 30
                                    color: "transparent"
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8
                                        Label {
                                            text: modelData.title
                                            color: "#f0f0f0"; font.pixelSize: 15; font.bold: true
                                        }
                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 24; height: 18; radius: 9
                                            color: "#3a3c40"
                                            Label {
                                                anchors.centerIn: parent
                                                text: modelData.cards.length
                                                color: "#c9c9c9"; font.pixelSize: 11
                                            }
                                        }
                                    }
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width; height: 1; color: "#3a3c40"
                                    }
                                }

                                // cards in this section — a wrapping flow
                                Flow {
                                    width: parent.width
                                    spacing: 10
                                    Repeater {
                                        model: modelData.cards
                                        delegate: CardDelegate {
                                            required property var modelData
                                            cardId:    modelData.cardId
                                            label:     modelData.label
                                            qty:       modelData.qty
                                            confirmed: modelData.confirmed
                                            preloaded: modelData.data      // pass cached data in
                                            onCardClicked: (data) => root.selectedCard = data
                                        }
                                    }
                                }

                                Item { width: 1; height: 8 }   // gap after section
                            }
                        }

                        Label {
                            visible: root.sections.length === 0
                            text: "No confirmed cards yet"
                            color: "#777"; font.pixelSize: 14
                        }
                    }
                }
            }
        }

        // ---------------- RIGHT: detail panel ----------------
        Rectangle {
            SplitView.preferredWidth: parent.width * 0.38
            SplitView.minimumWidth: 280
            color: "#26282c"

            CardDetailPanel {
                anchors.fill: parent
                anchors.margins: 18
                card: root.selectedCard
                visible: root.hasSelection
            }
            Label {
                anchors.centerIn: parent
                visible: !root.hasSelection
                text: "Select a card to see details"
                color: "#777"; font.pixelSize: 14
            }
        }
    }
}
