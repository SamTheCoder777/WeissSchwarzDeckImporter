import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    anchors.fill: parent

    property var selectedCard: ({})
    readonly property bool hasSelection: selectedCard && Object.keys(selectedCard).length > 0

    property var sections: []          // [{ title, cards: [ {cardId,label,qty,confirmed,data} ] }]
    property var _requested: ({})      // cardId -> true, so we ensureCardData ONCE

    function powerNum(d) {
        var p = d && d.power !== undefined ? parseInt(d.power, 10) : NaN;
        return isNaN(p) ? -1 : p;
    }
    function colorOf(d) {
        if (!d || !d.color)
            return "zzz";
        var m = /\[\[(\w+)\.gif\]\]/.exec(d.color);
        return m ? m[1] : d.color;
    }
    function isClimax(d) {
        return d && (String(d.cardKind).toUpperCase() === "CX" || String(d.cardType) === "4");
    }
    function levelOf(d) {
        if (!d || isClimax(d))
            return null;
        var lv = d.level;
        if (lv === undefined || lv === null || lv === "" || isNaN(parseInt(lv, 10)))
            return 0;
        return parseInt(lv, 10);
    }

    function rebuild() {
        if (referencePopup.visible)
            referencePopup.close();

        var items = [];
        for (var i = 0; i < selModel.rowCount(); ++i) {
            var cardId = selModel.dataAt(i, "cardId");
            if (!cardId || cardId.length === 0)
                continue;
            var d = cardDatabase.cardDataFor(cardId);
            if ((!d || d.cardName === undefined) && !root._requested[cardId]) {
                root._requested[cardId] = true;      // ask only once, ever
                cardDatabase.ensureCardData(cardId);
            }
            items.push({
                cardId: cardId,
                label: selModel.dataAt(i, "label"),
                qty: selModel.dataAt(i, "qty"),
                confirmed: selModel.dataAt(i, "confirmed"),
                data: d || ({})                      // never undefined
            });
        }

        var byLevel = {};
        for (var j = 0; j < items.length; ++j) {
            var lv = levelOf(items[j].data);
            var key = (lv === null) ? "cx" : String(lv);
            (byLevel[key] = byLevel[key] || []).push(items[j]);
        }

        function sortBucket(arr) {
            arr.sort(function (a, b) {
                var ca = colorOf(a.data), cb = colorOf(b.data);
                if (ca !== cb)
                    return ca < cb ? -1 : 1;    // group by color
                return powerNum(b.data) - powerNum(a.data);// big power -> small
            });
        }

        var out = [];
        var levels = Object.keys(byLevel).filter(function (k) {
            return k !== "cx";
        }).map(function (k) {
            return parseInt(k, 10);
        }).sort(function (a, b) {
            return b - a;
        });
        for (var L = 0; L < levels.length; ++L) {
            var arr = byLevel[String(levels[L])];
            sortBucket(arr);
            out.push({
                title: "Level " + levels[L],
                cards: arr
            });
        }
        if (byLevel["cx"]) {
            sortBucket(byLevel["cx"]);
            out.push({
                title: "Climax",
                cards: byLevel["cx"]
            });
        }
        root.sections = out;

        if (root.hasSelection && root.selectedCard.cardId) {
            var oldId = root.selectedCard.cardId;
            var fresh = Object.assign({}, cardDatabase.cardDataFor(oldId));
            fresh.cardId = oldId; // Never lose the ID
            root.selectedCard = fresh;
        }
    }

    // gallery only search
    function galleryReferenceCodes(kind, value) {
        function normalized(text) {
            return String(text || "").trim().toLowerCase();
        }

        var term = normalized(value);
        var matches = [];
        var seen = {};

        if (!term)
            return matches;

        for (var s = 0; s < root.sections.length; ++s) {
            var cards = root.sections[s].cards || [];

            for (var c = 0; c < cards.length; ++c) {
                var entry = cards[c];
                var code = String(entry.cardId || "");

                if (!code || seen[code])
                    continue;

                seen[code] = true;

                // Search the data already displayed in the gallery.
                var data = entry.data || {};
                var matched = false;

                if (kind === "trait") {
                    matched =
                        normalized(data.feature1) === term
                        || normalized(data.feature2) === term
                        || normalized(data.feature3) === term;

                } else if (kind === "name") {
                    matched =
                        normalized(data.cardName).indexOf(term) !== -1;

                } else if (kind === "ability") {
                    var abilityTerm = normalizedAbility(value);

                    matched = abilityTerm.length > 0
                        && normalizedAbility(data.text)
                            .indexOf(abilityTerm) !== -1;
                }

                if (matched)
                    matches.push(code);
            }
        }

        matches.sort();
        return matches;
    }

    function normalizedAbility(text) {
        return String(text || "")
            .replace(/<[/\\]?\s*br\s*[\/\\]?>/gi, "\n")
            .replace(/\s+/g, "")
            .toLowerCase();
    }

    // Update just one card's data when it arrives (does NOT re-run rebuild,
    // which would call ensureCardData again and loop forever).
    function patchCardData(code) {
        var fresh = cardDatabase.cardDataFor(code);
        if (!fresh || !fresh.cardName)
            return;
        if (!fresh || fresh.cardName === undefined)
            return;                         // nothing usable yet; ignore

        var changed = false;
        var secs = root.sections;
        for (var s2 = 0; s2 < secs.length; ++s2) {
            var cards = secs[s2].cards;
            for (var c = 0; c < cards.length; ++c) {
                if (cards[c].cardId === code && (!cards[c].data || cards[c].data.cardName === undefined)) {
                    cards[c].data = fresh;  // ONLY patch cards still missing data
                    changed = true;
                }
            }
        }
        if (changed)
            root.sections = secs.slice();

        if (root.selectedCard && root.selectedCard.cardId === code && (!root.selectedCard.cardName)) {
            var copy = Object.assign({}, fresh);
            copy.cardId = code; // Never lose the ID
            root.selectedCard = copy;
        }
    }

    Component.onCompleted: rebuild()
    Connections {
        target: selModel
        function onModelReset() {
            root.rebuild();
        }
        function onDataChanged() {
            root.rebuild();
        }
        function onRowsInserted() {
            root.rebuild();
        }
        function onRowsRemoved() {
            root.rebuild();
        }
    }
    Connections {
        target: cardDatabase
        function onCardReady(code) {
            root.patchCardData(code);
        }
        function onCardDataChanged() {
            referencePopup.close();

            referencePopup.allCodes = [];
            referencePopup.pageRows = [];

            root._requested = {};
            root.rebuild();
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

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
                            text: "Detected Cards"
                            color: "#e6e6e6"
                            font.pixelSize: 15
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        ComboBox {
                            model: ["EncoreDecks", "Local database"]

                            currentIndex: cardDatabase.cardDataSource === "local" ? 1 : 0

                            onActivated: {
                                cardDatabase.setCardDataSource(index === 1 ? "local" : "encore");

                                currentIndex = Qt.binding(function () {
                                    return cardDatabase.cardDataSource === "local" ? 1 : 0;
                                });
                            }
                        }
                        Row {
                            spacing: 0
                            Repeater {
                                model: ["EN", "JP"]
                                delegate: Rectangle {
                                    width: 44
                                    height: 26
                                    color: cardDatabase.locale === modelData ? "#3a3d40" : "transparent"
                                    border.color: "#26282b"
                                    border.width: 1
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: cardDatabase.locale === modelData ? "#f4f5f6" : "#8b9096"
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: cardDatabase.setLocale(modelData)
                                    }
                                }
                            }
                        }

                        Button {
                            text: "Export .txt"
                            onClicked: bridge.exportDeck()
                            background: Rectangle {
                                radius: 7
                                color: parent.down ? "#3a7fd0" : parent.hovered ? "#5aa8ff" : "#4d9dff"
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.bold: true
                                font.pixelSize: 13
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
                    contentWidth: availableWidth

                    Column {
                        id: sectionsColumn
                        width: scroller.availableWidth
                        spacing: 6
                        padding: 14

                        Repeater {
                            model: root.sections
                            delegate: Column {
                                required property var modelData
                                width: sectionsColumn.width - 28   // minus padding*2
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
                                            color: "#f0f0f0"
                                            font.pixelSize: 15
                                            font.bold: true
                                        }
                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 24
                                            height: 18
                                            radius: 9
                                            color: "#3a3c40"
                                            Label {
                                                anchors.centerIn: parent
                                                text: modelData.cards.length
                                                color: "#c9c9c9"
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: 1
                                        color: "#3a3c40"
                                    }
                                }

                                Flow {
                                    width: parent.width
                                    spacing: 10
                                    Repeater {
                                        model: modelData.cards
                                        delegate: CardDelegate {
                                            required property var modelData
                                            cardId: modelData.cardId
                                            label: modelData.label
                                            qty: modelData.qty
                                            confirmed: modelData.confirmed
                                            preloaded: modelData.data
                                            selected: root.selectedCard && root.selectedCard.cardId === modelData.cardId
                                            onCardClicked: data => {
                                                if (data && !data.cardId)
                                                    data.cardId = modelData.cardId;
                                                root.selectedCard = data;
                                            }
                                        }
                                    }
                                }

                                Item {
                                    width: 1
                                    height: 8
                                }
                            }
                        }

                        Label {
                            visible: root.sections.length === 0
                            text: "No confirmed cards yet"
                            color: "#777"
                            font.pixelSize: 14
                        }
                    }
                }
            }
        }

        Rectangle {
            SplitView.preferredWidth: parent.width * 0.38
            SplitView.minimumWidth: 280
            color: "#26282c"

            CardDetailPanel {
                anchors.fill: parent
                anchors.margins: 18
                card: root.selectedCard
                visible: root.hasSelection
                onReferenceRequested: function (kind, value) {
                    referencePopup.openReference(kind, value);
                }
            }
            Label {
                anchors.centerIn: parent
                visible: !root.hasSelection
                text: "Select a card to see details"
                color: "#777"
                font.pixelSize: 14
            }
        }
    }
    Popup {
        id: referencePopup

        parent: Overlay.overlay
        anchors.centerIn: parent

        width: Math.max(1, Math.min(780, parent.width - 32))
        height: Math.max(1, Math.min(650, parent.height - 32))

        modal: true
        focus: true
        padding: 18

        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string referenceKind: ""
        property string referenceValue: ""
        property string errorMessage: ""

        property var allCodes: []
        property var pageRows: []

        property int pageIndex: 0
        readonly property int pageSize: 30

        readonly property int pageCount: Math.max(1, Math.ceil(allCodes.length / pageSize))

        readonly property color accent: referenceKind === "trait" ? "#77d9a0" : referenceKind === "name" ? "#80baff" : "#efc779"

        readonly property string category: referenceKind === "trait" ? "Trait" : referenceKind === "name" ? "Name contains" : "Ability reference"

        function openReference(kind, value) {
            referenceKind = kind;
            referenceValue = value;
            errorMessage = "";
            allCodes = [];
            pageRows = [];
            pageIndex = 0;

            allCodes = root.galleryReferenceCodes(kind, value);

            loadPage();
            open();
        }

        function loadPage() {
            var rows = [];

            var first = pageIndex * pageSize;
            var last = Math.min(first + pageSize, allCodes.length);

            for (var i = first; i < last; ++i) {
                var code = String(allCodes[i]);
                var data = cardDatabase.cardDataFor(code) || {};

                rows.push({
                    cardCode: code,
                    name: data.cardName || code,
                    rarity: data.rarity || ""
                });
            }

            pageRows = rows;
            referenceList.positionViewAtBeginning();
        }

        function showCard(code) {
            var data = Object.assign({}, cardDatabase.cardDataFor(code) || {});

            data.cardId = code;
            data.cardCode = code;

            root.selectedCard = data;
            close();
        }

        background: Rectangle {
            color: "#22252b"
            radius: 12
            border.color: "#414650"
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 12

            RowLayout {
                Layout.fillWidth: true

                Rectangle {
                    width: 4
                    height: 42
                    radius: 2
                    color: referencePopup.accent
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Label {
                        Layout.fillWidth: true

                        text: referencePopup.category + " · " + referencePopup.referenceValue

                        textFormat: Text.PlainText
                        color: referencePopup.accent
                        font.pixelSize: 18
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true

                        text: "Current Gallery" + " · " + referencePopup.allCodes.length + (referencePopup.allCodes.length === 1 ? " matching card" : " matching cards")

                        color: "#a7adb8"
                        font.pixelSize: 12
                    }
                }

                Button {
                    text: "Close"
                    onClicked: referencePopup.close()
                }
            }

            Label {
                Layout.fillWidth: true

                text: "Select a result to view its card details."
                color: "#a7adb8"
                font.pixelSize: 12
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3f48"
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: referenceList

                    anchors.fill: parent
                    clip: true
                    spacing: 6

                    model: referencePopup.pageRows

                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        id: resultDelegate

                        required property var modelData

                        width: referenceList.width
                        height: 96
                        hoverEnabled: true

                        onClicked: referencePopup.showCard(modelData.cardCode)

                        background: Rectangle {
                            radius: 7

                            color: resultDelegate.down ? "#374151" : resultDelegate.hovered ? "#303640" : "#292d34"

                            border.color: resultDelegate.hovered ? referencePopup.accent : "#363c45"
                        }

                        contentItem: RowLayout {
                            spacing: 14

                            Rectangle {
                                Layout.preferredWidth: 54
                                Layout.preferredHeight: 76

                                color: "#1b1e23"
                                radius: 4

                                Image {
                                    anchors.fill: parent

                                    source: {
                                        var revision = cardDatabase.dataRevision;

                                        return cardDatabase.cardImageSource(resultDelegate.modelData.cardCode);
                                    }

                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    Layout.fillWidth: true

                                    text: resultDelegate.modelData.name
                                    textFormat: Text.PlainText

                                    color: "#f0f2f5"
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true

                                    text: resultDelegate.modelData.cardCode + (resultDelegate.modelData.rarity ? " · " + resultDelegate.modelData.rarity : "")

                                    textFormat: Text.PlainText
                                    color: "#a7adb8"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            Label {
                                text: "View"
                                color: referencePopup.accent
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    width: parent.width - 24

                    visible: referencePopup.errorMessage !== "" || referencePopup.allCodes.length === 0

                    text: referencePopup.errorMessage !== "" ? referencePopup.errorMessage : "No matching cards in the current gallery."

                    color: referencePopup.errorMessage !== "" ? "#ef9a9a" : "#a7adb8"

                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: "Previous"
                    enabled: referencePopup.pageIndex > 0

                    onClicked: {
                        referencePopup.pageIndex--;
                        referencePopup.loadPage();
                    }
                }

                Label {
                    Layout.fillWidth: true

                    text: "Page " + (referencePopup.pageIndex + 1) + " of " + referencePopup.pageCount

                    color: "#b9bec8"
                    horizontalAlignment: Text.AlignHCenter
                }

                Button {
                    text: "Next"

                    enabled: referencePopup.pageIndex + 1 < referencePopup.pageCount

                    onClicked: {
                        referencePopup.pageIndex++;
                        referencePopup.loadPage();
                    }
                }
            }
        }
    }
}
