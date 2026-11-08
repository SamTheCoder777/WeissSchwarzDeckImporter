import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts 1.15
import "../gallery"
import "../shared" as Shared
import "../shared/TriggerIcons.js" as Triggers

Rectangle {
    id: root
    color: "#1e1f22"
    readonly property color surface: "#26282c"
    readonly property color line: "#3a3c40"
    readonly property color muted: "#9aa0aa"
    readonly property color ink: "#e6e6e6"
    readonly property color accent: "#4d9dff"
    readonly property var overlayItem: root.Overlay.overlay
    property string titleNumber: ""
    property string preferredGroupId: ""
    property var appliedFilters: ({})
    property string groupMode: "color"
    property string sortMode: "colorLevelCostPower"
    property bool descending: true
    property var referenceSortOverride: null
    readonly property string referenceGroupMode: referenceSortOverride ? referenceSortOverride.group : groupMode
    readonly property string referenceSortMode: referenceSortOverride ? referenceSortOverride.sort : sortMode
    readonly property bool referenceDescending: referenceSortOverride ? referenceSortOverride.descending : descending
    property var collapsedGroups: ({})
    property var referenceCollapsedGroups: ({})
    property bool previewOpenFromClick: false
    property var browserRowCache: ({})
    property var referenceRowCache: ({})
    property var previewAnchor: null
    property var previewCard: ({})
    readonly property int referenceColumns: Math.max(1, Math.floor(referenceList.width / 180))
    ListModel {
        id: browserRowsModel
    }
    ListModel {
        id: referenceRowsModel
    }
    readonly property int browserColumns: Math.max(1, Math.floor(browser.width / 180))
    readonly property string currentTitleLabel: {
        for (var i = 0; i < deckBuilder.titles.length; ++i)
            if (deckBuilder.titles[i].titleNumber === titleNumber)
                return deckBuilder.titles[i].label;
        return "Choose a set…";
    }
    readonly property string currentSetName: {
        for (var i = 0; i < deckBuilder.titles.length; ++i)
            if (deckBuilder.titles[i].titleNumber === titleNumber)
                return deckBuilder.titles[i].setNames;
        return "";
    }
    readonly property string deckStatsText: {
        var s = deckBuilder.deckStats;
        return "Character: " + s.character + " · Event: " + s.event + " · Climax: " + s.climax + " · Soul trigger icons: " + s.soulTriggers + "\nLv 0: " + s.level0 + " · Lv 1: " + s.level1 + " · Lv 2: " + s.level2 + " · Lv 3: " + s.level3;
    }
    readonly property string bottomDeckStatsText: root.deckStatsText.replace(/ · Soul trigger icons: [^\n]*/, "")
    readonly property string deckStatsHtml: root.deckStatsText.replace("Soul trigger icons:", "Trigger icons:").replace(/\n/g, "<br>")
    readonly property string orderingSummary: {
        var direction = descending ? "descending" : "ascending";
        if (sortMode === "colorLevelCostPower")
            return "Color groups · level " + direction + " · cost " + direction + " · power " + direction;
        var group = groupMode === "color" ? "Color groups · " : groupMode === "level" ? "Level groups " + direction + " · " : "";
        var sort = sortMode === "name" ? "Name" : sortMode === "level" ? "Level" : sortMode === "power" ? "Power" : "Card number";
        return group + sort + " " + direction;
    }

    component DarkButton: Basic.Button {
        id: control
        property bool primary: false
        property bool quiet: false
        property bool neutral: false
        property bool danger: false
        implicitHeight: 32
        implicitWidth: Math.max(36, contentItem.implicitWidth + 24)
        padding: 8
        font.family: "Segoe UI"
        font.pixelSize: 12
        hoverEnabled: true
        opacity: enabled ? 1 : 0.42
        contentItem: Text {
            text: control.text
            font: control.font
            color: control.primary ? "white" : control.danger ? "#eea6a6" : root.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: control.primary ? (control.down ? "#347dce" : control.hovered ? "#65acff" : root.accent) : control.down ? "#3d424b" : control.hovered ? "#343941" : control.neutral ? "#2c3037" : control.quiet ? "#2c323c" : "#354151"
            border.width: control.primary ? 0 : 1
            border.color: control.activeFocus ? root.accent : control.neutral ? root.line : "#58677b"
        }
        HoverHandler {
            enabled: control.enabled
            cursorShape: Qt.PointingHandCursor
        }
    }
    component DarkField: Basic.TextField {
        id: control
        implicitHeight: 32
        leftPadding: 11
        rightPadding: 11
        color: root.ink
        placeholderTextColor: "#777f8b"
        selectionColor: root.accent
        selectedTextColor: "white"
        font.family: "Segoe UI"
        font.pixelSize: 12
        background: Rectangle {
            radius: 7
            color: "#202226"
            border.color: control.activeFocus ? root.accent : root.line
        }
    }
    component DarkCombo: Basic.ComboBox {
        id: control
        implicitHeight: 32
        leftPadding: 11
        rightPadding: 27
        font.family: "Segoe UI"
        font.pixelSize: 12
        hoverEnabled: true
        opacity: enabled ? 1 : 0.45
        contentItem: Text {
            text: control.displayText
            font: control.font
            color: root.ink
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - 10
            y: (control.height - height) / 2
            text: "▾"
            color: root.muted
        }
        background: Rectangle {
            radius: 7
            color: control.hovered ? "#30353d" : "#262a30"
            border.color: control.activeFocus ? root.accent : root.line
        }
        delegate: Basic.ItemDelegate {
            id: option
            required property var modelData
            required property int index
            width: control.width
            implicitHeight: 32
            contentItem: Text {
                text: String(option.modelData)
                color: option.highlighted ? "#80baff" : root.ink
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            highlighted: control.highlightedIndex === option.index
            background: Rectangle {
                color: option.highlighted || option.hovered ? "#353c48" : "transparent"
                radius: 4
            }
        }
        popup: Basic.Popup {
            y: control.height + 5
            width: control.width
            padding: 5
            implicitHeight: Math.min(280, contentItem.implicitHeight + 10)
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
                Basic.ScrollBar.vertical: Basic.ScrollBar {}
            }
            background: Rectangle {
                color: "#282d35"
                border.color: "#4a5260"
                radius: 8
            }
        }
        HoverHandler {
            enabled: control.enabled
            cursorShape: Qt.PointingHandCursor
        }
    }
    component DarkCheck: Basic.CheckBox {
        id: control
        implicitHeight: 32
        spacing: 8
        leftPadding: 0
        indicator: Rectangle {
            x: control.leftPadding
            y: (control.height - height) / 2
            width: 17
            height: 17
            radius: 4
            color: control.checked ? root.accent : "#202226"
            border.color: control.checked ? root.accent : "#626a76"
            Text {
                anchors.centerIn: parent
                text: "✓"
                color: "white"
                visible: control.checked
                font.pixelSize: 13
            }
        }
        contentItem: Text {
            text: control.text
            leftPadding: 25
            verticalAlignment: Text.AlignVCenter
            color: root.ink
            font.pixelSize: 12
        }
    }
    component QuantityStepper: RowLayout {
        id: stepper
        property var card: ({})
        readonly property string code: String(card.cardCode || "")
        readonly property int copies: {
            var revision = deckBuilder.deckRevision;
            return code ? deckBuilder.quantity(code) : 0;
        }
        spacing: 6
        DarkButton {
            text: "−"
            Layout.preferredWidth: 34
            Layout.minimumWidth: 34
            Layout.maximumWidth: 34
            implicitHeight: 32
            enabled: stepper.copies > 0
            onClicked: deckBuilder.changeQuantity(stepper.card, -1)
        }
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 32
            text: String(stepper.copies)
            color: stepper.copies ? "#80baff" : root.muted
            font.pixelSize: 14
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }
        DarkButton {
            text: "+"
            primary: true
            Layout.preferredWidth: 34
            Layout.minimumWidth: 34
            Layout.maximumWidth: 34
            implicitHeight: 32
            enabled: !!stepper.code
            onClicked: deckBuilder.addCard(stepper.card)
        }
    }
    component PreviewDetails: Column {
        id: detailsColumn
        width: parent.width
        spacing: 8
        readonly property bool artworkPending: previewArtworkView.source.toString().length > 0 && previewArtworkView.status !== Image.Ready && previewArtworkView.status !== Image.Error
        RowLayout {
            width: parent.width
            spacing: 10
            Image {
                id: previewArtworkView
                Layout.preferredWidth: 70
                Layout.preferredHeight: 98
                Layout.maximumHeight: 98
                source: root.previewCard.picture ? deckBuilder.imageSource(root.previewCard.picture) : ""
                asynchronous: true
                fillMode: Image.PreserveAspectFit
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    text: root.previewCard.cardName || ""
                    textFormat: Text.PlainText
                    color: root.ink
                    font.bold: true
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    clip: true
                }
                Label {
                    Layout.fillWidth: true
                    text: root.previewCard.cardCode || ""
                    textFormat: Text.PlainText
                    color: root.muted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    clip: true
                }
                Label {
                    Layout.fillWidth: true
                    text: [root.previewCard.cardKind, root.previewCard.color, root.previewCard.rarity].filter(function (value) {
                        return !!value;
                    }).join(" · ")
                    textFormat: Text.PlainText
                    color: "#aabbd2"
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    clip: true
                }
            }
        }
        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 10
            rowSpacing: 4
            Label {
                text: "Level:"
                color: root.muted
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.previewCard.level === undefined || root.previewCard.level === "" ? "—" : String(root.previewCard.level)
                color: root.ink
                font.pixelSize: 11
                wrapMode: Text.Wrap
                clip: true
            }
            Label {
                text: "Cost:"
                color: root.muted
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.previewCard.cost === undefined || root.previewCard.cost === "" ? "—" : String(root.previewCard.cost)
                color: root.ink
                font.pixelSize: 11
                wrapMode: Text.Wrap
                clip: true
            }
            Label {
                text: "Power:"
                color: root.muted
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.previewCard.power || "—"
                color: root.ink
                font.pixelSize: 11
                wrapMode: Text.Wrap
                clip: true
            }
            Label {
                text: "Soul:"
                color: root.muted
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: Triggers.inlineText(root.previewCard.soul || "—", 14)
                textFormat: Text.RichText
                color: root.ink
                font.pixelSize: 11
                wrapMode: Text.Wrap
                clip: true
            }
            Label {
                text: "Trigger:"
                color: root.muted
                font.pixelSize: 11
            }
            Shared.TriggerIconRow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                value: root.previewCard.cardTrigger || ""
                iconSize: 14
            }
        }
        Label {
            width: parent.width
            text: [root.previewCard.feature1, root.previewCard.feature2, root.previewCard.feature3].filter(function (value) {
                return !!value && value !== "-";
            }).join(" · ")
            visible: text.length > 0
            textFormat: Text.PlainText
            color: "#77d9a0"
            font.pixelSize: 11
            wrapMode: Text.Wrap
            clip: true
        }
        Rectangle {
            width: parent.width
            height: 1
            color: root.line
        }
        Label {
            width: parent.width
            text: Triggers.inlineText(root.previewCard.text || "", 14)
            textFormat: Text.RichText
            color: root.ink
            font.pixelSize: 11
            wrapMode: Text.Wrap
            clip: true
        }
    }

    component CardTile: Rectangle {
        id: tile
        required property var card
        property bool referenceResult: false
        readonly property bool deckPreviewAnchor: false
        readonly property bool hovered: hover.hovered
        readonly property bool selected: deckBuilder.selectedCard.cardCode === card.cardCode
        height: 300
        radius: 9
        color: hover.hovered ? "#2d323a" : "#262a30"
        border.color: selected ? root.accent : hover.hovered ? "#657286" : "#363c45"
        border.width: selected ? 2 : 1
        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
            onHoveredChanged: {
                if (!tile.referenceResult)
                    return;
                if (hovered)
                    root.queueReferencePreview(tile, tile.card);
                else if (root.previewAnchor === tile)
                    root.schedulePreviewHide();
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (tile.referenceResult)
                    root.showReferencePreview(tile, tile.card);
                else
                    deckBuilder.selectCard(tile.card);
            }
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                Image {
                    id: art
                    anchors.fill: parent
                    source: deckBuilder.imageSource(tile.card.picture || "")
                    sourceSize: Qt.size(300, 420)
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                }
                Label {
                    anchors.centerIn: parent
                    visible: art.status === Image.Error || !art.source.toString()
                    text: "Image unavailable"
                    color: root.muted
                    font.pixelSize: 11
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                text: tile.card.cardName || tile.card.cardCode
                textFormat: Text.PlainText
                color: root.ink
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: tile.card.cardCode + (tile.card.rarity ? " · " + tile.card.rarity : "")
                textFormat: Text.PlainText
                color: root.muted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            QuantityStepper {
                Layout.fillWidth: true
                card: tile.card
            }
        }
    }

    function colorGroupBorder(key) {
        var colors = {
            "0": "#df6672",
            "1": "#e1bd50",
            "2": "#62b780",
            "3": "#669de0",
            "4": "#b692dc",
            "5": "#8b95a4"
        };
        return colors[String(key)] || root.line;
    }
    function applyReferenceSort() {
        deckBuilder.setReferenceSort(root.referenceGroupMode, root.referenceSortMode, root.referenceDescending);
    }
    function changeReferenceSort(group, sort, descending) {
        root.referenceSortOverride = {
            group: group,
            sort: sort,
            descending: descending
        };
        root.applyReferenceSort();
    }
    function runSearch(page) {
        var filters = Object.assign({}, root.appliedFilters);
        filters._deckQuickSearch = quickName.text.trim();
        filters._deckGroup = root.groupMode;
        filters._deckSort = root.sortMode;
        filters._deckDescending = root.descending;
        deckBuilder.search(root.titleNumber, filters, page || 0);
    }
    function chooseTitle(title) {
        root.titleNumber = title.titleNumber;
        root.preferredGroupId = title.titleNumber;
        titlesPopup.close();
        searchTimer.stop();
        root.runSearch(0);
    }
    function resetFilters() {
        quickName.text = "";
        root.appliedFilters = {};
        advanced.resetFields();
        searchTimer.stop();
        root.runSearch(0);
    }
    function syncRows(model, source, columns, collapsible, cache, collapsedState) {
        cache = cache || {};
        collapsedState = collapsedState || root.collapsedGroups;
        var groups = [], desired = [];
        for (var i = 0; i < source.length; ++i) {
            var key = String(source[i].deckGroup || "");
            if (!groups.length || groups[groups.length - 1].key !== key)
                groups.push({
                    key: key,
                    label: String(source[i].deckGroupLabel || ""),
                    cards: []
                });
            groups[groups.length - 1].cards.push(source[i]);
        }
        for (var g = 0; g < groups.length; ++g) {
            var group = groups[g];
            var collapsed = collapsible && !!collapsedState[group.key];
            if (collapsible && group.key) {
                desired.push({
                    rowKey: "h:" + group.key,
                    rowType: "header",
                    groupKey: group.key,
                    heading: group.label,
                    counter: group.cards.length,
                    collapsed: collapsed,
                    cardJson: "[]"
                });
            }
            if (collapsed)
                continue;
            for (var start = 0; start < group.cards.length; start += columns) {
                var cards = group.cards.slice(start, start + columns);
                var cacheKey = group.key + ":" + columns + ":" + cards[0].cardCode + ":" + cards[cards.length - 1].cardCode + ":" + cards.length;
                if (!Object.prototype.hasOwnProperty.call(cache, cacheKey))
                    cache[cacheKey] = JSON.stringify(cards);
                desired.push({
                    rowKey: "r:" + group.key + ":" + cards[0].cardCode,
                    rowType: "cards",
                    groupKey: group.key,
                    heading: "",
                    counter: 0,
                    collapsed: false,
                    cardJson: cache[cacheKey]
                });
            }
        }
        // Keep existing rows/delegates when appending. Only change the final partial row
        // and insert new rows; collapsing removes the affected card rows only.
        for (var row = 0; row < desired.length; ++row) {
            var wanted = desired[row];
            if (row < model.count && model.get(row).rowKey !== wanted.rowKey) {
                var found = -1;
                for (var next = row + 1; next < model.count; ++next) {
                    if (model.get(next).rowKey === wanted.rowKey) {
                        found = next;
                        break;
                    }
                }
                if (found >= 0)
                    model.remove(row, found - row);
                else {
                    model.insert(row, wanted);
                    continue;
                }
            }
            if (row === model.count) {
                model.append(wanted);
                continue;
            }
            var current = model.get(row);
            var properties = ["rowType", "groupKey", "heading", "counter", "collapsed", "cardJson"];
            for (var p = 0; p < properties.length; ++p) {
                var property = properties[p];
                if (current[property] !== wanted[property])
                    model.setProperty(row, property, wanted[property]);
            }
        }
        if (model.count > desired.length)
            model.remove(desired.length, model.count - desired.length);
    }
    function rebuildRows() {
        root.syncRows(browserRowsModel, deckBuilder.results, root.browserColumns, true, root.browserRowCache);
    }
    function rebuildReferenceRows() {
        root.syncRows(referenceRowsModel, deckBuilder.referenceResults, root.referenceColumns, true, root.referenceRowCache, root.referenceCollapsedGroups);
    }
    function toggleGroup(key) {
        var groups = Object.assign({}, root.collapsedGroups);
        groups[key] = !groups[key];
        root.collapsedGroups = groups;
        root.rebuildRows();
        loadTimer.restart();
    }
    function toggleReferenceGroup(key) {
        root.hideReferencePreview();
        var groups = Object.assign({}, root.referenceCollapsedGroups);
        groups[key] = !groups[key];
        root.referenceCollapsedGroups = groups;
        root.rebuildReferenceRows();
        referenceLoadTimer.restart();
    }
    function maybeLoadMore() {
        if (root.visible && !deckBuilder.busy && deckBuilder.hasMore && !deckBuilder.loadError && browser.contentY + browser.height >= browser.contentHeight - 600)
            deckBuilder.loadMore();
    }
    function maybeLoadReferences() {
        if (referencePopup.visible && !deckBuilder.referenceBusy && deckBuilder.referenceHasMore && !deckBuilder.referenceError && referenceList.contentY + referenceList.height >= referenceList.contentHeight - 500)
            deckBuilder.loadMoreReferences();
    }
    function openExportMenu(anchor) {
        root.hideReferencePreview();
        var point = anchor.mapToItem(exportMenu.parent, 0, 0);
        exportMenu.x = Math.max(8, Math.min(point.x, exportMenu.parent.width - exportMenu.width - 8));
        exportMenu.y = Math.max(8, point.y - exportMenu.height - 6);
        exportMenu.open();
    }
    function schedulePreviewHide() {
        previewHideTimer.restart();
    }
    function queueReferencePreview(anchor, card) {
        previewMeasureTimer.stop();
        root.previewOpenFromClick = false;
        previewHideTimer.stop();
        previewTimer.stop();
        referencePreview.close();
        root.previewAnchor = anchor;
        root.previewCard = card;
        previewTimer.restart();
    }
    function hideReferencePreview() {
        previewMeasureTimer.stop();
        root.previewOpenFromClick = false;
        previewHideTimer.stop();
        previewTimer.stop();
        referencePreview.close();
        root.previewAnchor = null;
    }
    function prepareReferencePreview(fromClick) {
        root.previewOpenFromClick = fromClick;
        previewMeasureTimer.lastHeight = -1;
        previewMeasureTimer.stableFrames = 0;
        previewMeasureTimer.restart();
    }
    function showReferencePreview(anchor, card) {
        if (referencePreview.visible && root.previewAnchor === anchor)
            return;
        previewHideTimer.stop();
        previewTimer.stop();
        referencePreview.close();
        root.previewAnchor = anchor;
        root.previewCard = card;
        root.prepareReferencePreview(true);
    }
    onVisibleChanged: {
        if (!visible) {
            root.hideReferencePreview();
            exportMenu.close();
            deckDrawer.close();
            referencePopup.close();
        }
    }
    onBrowserColumnsChanged: {
        root.browserRowCache = {};
        root.rebuildRows();
    }
    onReferenceColumnsChanged: {
        root.referenceRowCache = {};
        root.rebuildReferenceRows();
    }
    Timer {
        id: loadTimer
        interval: 80
        onTriggered: root.maybeLoadMore()
    }
    Timer {
        id: referenceLoadTimer
        interval: 80
        onTriggered: root.maybeLoadReferences()
    }
    Timer {
        id: previewTimer
        interval: 350
        onTriggered: {
            if ((referencePopup.visible || deckDrawer.visible) && root.previewAnchor && root.previewAnchor.hovered)
                root.prepareReferencePreview(false);
        }
    }
    Timer {
        id: previewMeasureTimer
        interval: stableFrames >= 2 ? 60 : 16
        repeat: true
        property real lastHeight: -1
        property int stableFrames: 0
        onTriggered: {
            if (!root.previewAnchor || !(referencePopup.visible || deckDrawer.visible) || (!root.previewOpenFromClick && !root.previewAnchor.hovered)) {
                root.hideReferencePreview();
                return;
            }
            if (stableFrames < 2)
                previewMeasureBody.forceLayout();
            var measured = Math.ceil(previewMeasureBody.implicitHeight + referencePreview.topPadding + referencePreview.bottomPadding);
            stableFrames = measured === lastHeight ? stableFrames + 1 : 0;
            lastHeight = measured;
            if (stableFrames < 2 || previewMeasureBody.artworkPending || previewBody.artworkPending)
                return;
            stop();
            referencePreview.preparedHeight = Math.max(1, measured);
            referencePreview.open();
        }
    }
    Timer {
        id: previewHideTimer
        interval: 200

        onTriggered: {
            if (!root.previewAnchor || !root.previewAnchor.hovered)
                root.hideReferencePreview();
        }
    }
    Timer {
        id: searchTimer
        interval: 250
        onTriggered: root.runSearch(0)
    }
    Connections {
        target: deckBuilder
        function onTitlesChanged() {
            var previous = root.titleNumber || root.preferredGroupId;
            root.titleNumber = "";
            for (var i = 0; i < deckBuilder.titles.length; ++i) {
                if (deckBuilder.titles[i].titleNumber === previous) {
                    root.titleNumber = previous;
                    break;
                }
            }
            if (!root.titleNumber && deckBuilder.titles.length)
                root.titleNumber = deckBuilder.titles[0].titleNumber;
            if (root.titleNumber) {
                root.preferredGroupId = root.titleNumber;
                root.runSearch(0);
            }
        }
        function onResultsReset() {
            root.collapsedGroups = {};
            root.browserRowCache = {};
            browserRowsModel.clear();
            browser.positionViewAtBeginning();
        }
        function onResultsChanged() {
            root.rebuildRows();
            loadTimer.restart();
        }
        function onStateChanged() {
            loadTimer.restart();
        }
        function onReferenceReset() {
            root.hideReferencePreview();
            root.referenceRowCache = {};
            root.referenceCollapsedGroups = {};
            referenceRowsModel.clear();
            referenceList.positionViewAtBeginning();
        }
        function onReferenceChanged() {
            root.rebuildReferenceRows();
            referenceLoadTimer.restart();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            DarkButton {
                id: titlePicker
                neutral: true
                Layout.fillWidth: true
                Layout.preferredWidth: 360
                Layout.minimumWidth: 120
                text: root.currentTitleLabel + "   ▾"
                enabled: deckBuilder.titles.length > 0
                onClicked: titlesPopup.open()
                contentItem: Text {
                    text: titlePicker.text
                    color: root.ink
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
            DarkField {
                id: quickName
                Layout.fillWidth: true
                Layout.preferredWidth: 220
                Layout.minimumWidth: 100
                placeholderText: "Search card name or ID…"
                onTextEdited: searchTimer.restart()
                onAccepted: {
                    searchTimer.stop();
                    root.runSearch(0);
                }
            }
            DarkButton {
                text: "Filters"
                onClicked: advanced.open()
            }
            DarkButton {
                text: "Reset"
                onClicked: root.resetFilters()
            }
            DarkCombo {
                Layout.preferredWidth: 92
                model: ["JP cards", "EN cards"]
                currentIndex: deckBuilder.edition === "en" ? 1 : 0
                onActivated: function (index) {
                    deckBuilder.setEdition(index === 1 ? "en" : "jp");
                    currentIndex = Qt.binding(function () {
                        return deckBuilder.edition === "en" ? 1 : 0;
                    });
                }
            }
            DarkCombo {
                Layout.preferredWidth: 125
                model: ["English text", "Japanese text"]
                currentIndex: deckBuilder.language === "JP" ? 1 : 0
                onActivated: function (index) {
                    deckBuilder.setLanguage(index === 1 ? "JP" : "EN");
                    currentIndex = Qt.binding(function () {
                        return deckBuilder.language === "JP" ? 1 : 0;
                    });
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 7
            DarkField {
                Layout.preferredWidth: 155
                Layout.maximumWidth: 180
                text: deckBuilder.deckName
                placeholderText: "Deck name"
                onTextEdited: deckBuilder.deckName = text
            }
            Label {
                text: "Group"
                color: root.muted
                font.pixelSize: 10
            }
            DarkCombo {
                Layout.preferredWidth: 112
                model: ["No grouping", "By color", "By level"]
                enabled: root.sortMode !== "colorLevelCostPower"
                currentIndex: root.groupMode === "color" ? 1 : root.groupMode === "level" ? 2 : 0
                onActivated: function (index) {
                    root.groupMode = ["none", "color", "level"][index];
                    root.runSearch(0);
                    currentIndex = Qt.binding(function () {
                        return root.groupMode === "color" ? 1 : root.groupMode === "level" ? 2 : 0;
                    });
                }
            }
            Label {
                text: "Sort"
                color: root.muted
                font.pixelSize: 10
            }
            DarkCombo {
                Layout.preferredWidth: 210
                model: ["Card number", "Card name", "Level", "Power", "Color › level › cost › power"]
                currentIndex: ["code", "name", "level", "power", "colorLevelCostPower"].indexOf(root.sortMode)
                onActivated: function (index) {
                    root.sortMode = ["code", "name", "level", "power", "colorLevelCostPower"][index];
                    if (root.sortMode === "colorLevelCostPower") {
                        root.groupMode = "color";
                        root.descending = true;
                    }
                    root.runSearch(0);
                    currentIndex = Qt.binding(function () {
                        return ["code", "name", "level", "power", "colorLevelCostPower"].indexOf(root.sortMode);
                    });
                }
            }
            DarkButton {
                Layout.preferredWidth: 108
                neutral: true
                text: root.descending ? "↓ Descending" : "↑ Ascending"
                onClicked: {
                    root.descending = !root.descending;
                    root.runSearch(0);
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: deckBuilder.totalMatches + " cards · " + root.orderingSummary
                color: root.muted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            Basic.BusyIndicator {
                implicitWidth: 18
                implicitHeight: 18
                running: deckBuilder.busy
                visible: running
            }
            DarkButton {
                text: "Reload"
                enabled: !deckBuilder.busy
                onClicked: deckBuilder.reload()
            }
        }
        Basic.SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 12
                color: "transparent"
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: parent.height
                    color: root.line
                }
            }
            Item {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 300
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    ListView {
                        id: browser
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: browserRowsModel
                        onContentYChanged: loadTimer.restart()
                        onHeightChanged: loadTimer.restart()
                        clip: true
                        spacing: 12
                        reuseItems: true
                        Basic.ScrollBar.vertical: Basic.ScrollBar {}
                        delegate: Item {
                            id: cardRow
                            required property string rowType
                            required property string groupKey
                            required property string heading
                            required property int counter
                            required property bool collapsed
                            required property string cardJson
                            width: browser.width
                            height: rowType === "header" ? 38 : 300
                            DarkButton {
                                id: groupHeader
                                readonly property bool isColorGroup: root.groupMode === "color" || root.sortMode === "colorLevelCostPower"
                                anchors.fill: parent
                                visible: cardRow.rowType === "header"
                                background: Rectangle {
                                    radius: 7
                                    color: groupHeader.down ? "#3d424b" : groupHeader.hovered ? "#343941" : "#2c3037"
                                    border.width: groupHeader.isColorGroup ? 2 : 1
                                    border.color: groupHeader.isColorGroup ? root.colorGroupBorder(cardRow.groupKey) : root.line
                                }
                                onClicked: root.toggleGroup(cardRow.groupKey)
                                contentItem: RowLayout {
                                    spacing: 10
                                    Label {
                                        text: cardRow.collapsed ? "▸" : "▾"
                                        color: "#80baff"
                                        font.pixelSize: 16
                                    }
                                    Label {
                                        text: cardRow.heading
                                        color: "#d7e4f8"
                                        font.bold: true
                                        font.pixelSize: 13
                                    }
                                    Label {
                                        text: cardRow.counter + " loaded"
                                        color: root.muted
                                        font.pixelSize: 11
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: cardRow.collapsed ? "Expand" : "Collapse"
                                        color: root.muted
                                        font.pixelSize: 11
                                    }
                                }
                            }
                            Row {
                                anchors.fill: parent
                                spacing: 10
                                visible: cardRow.rowType === "cards"
                                Repeater {
                                    model: cardRow.rowType === "cards" ? JSON.parse(cardRow.cardJson) : []
                                    CardTile {
                                        required property var modelData
                                        card: modelData
                                        width: Math.floor((browser.width - (root.browserColumns - 1) * 10) / root.browserColumns)
                                    }
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: !deckBuilder.busy && browser.count === 0
                            text: root.titleNumber ? "No matching cards\nTry adjusting your filters." : "Choose a set to start browsing."
                            color: root.muted
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: 13
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: deckBuilder.loadingMore || !!deckBuilder.loadError
                        Label {
                            Layout.fillWidth: true
                            text: deckBuilder.results.length + " of " + deckBuilder.totalMatches + " cards loaded"
                            color: root.muted
                            font.pixelSize: 11
                        }
                        Basic.BusyIndicator {
                            implicitWidth: 24
                            implicitHeight: 24
                            running: deckBuilder.loadingMore
                            visible: running
                        }
                        DarkButton {
                            text: deckBuilder.loadError ? "Retry loading" : "Load more"
                            visible: !!deckBuilder.loadError || deckBuilder.hasMore
                            enabled: !deckBuilder.busy
                            onClicked: {
                                if (deckBuilder.hasMore)
                                    deckBuilder.loadMore();
                                else
                                    root.runSearch(0);
                            }
                        }
                    }
                }
            }
            Rectangle {
                SplitView.preferredWidth: 330
                SplitView.minimumWidth: 260
                color: root.surface
                radius: 10
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12
                    CardDetailPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!deckBuilder.selectedCard.cardCode
                        card: deckBuilder.selectedCard
                        requestCardData: false
                        imageSourceOverride: deckBuilder.imageSource(card.picture || "")
                        onReferenceRequested: function (kind, value) {
                            root.applyReferenceSort();
                            deckBuilder.openReference(kind, value);
                            referencePopup.open();
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !deckBuilder.selectedCard.cardCode
                        text: "Select a card\nto view its details"
                        color: root.muted
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 13
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.line
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "In deck"
                            color: root.muted
                            font.pixelSize: 12
                        }
                        QuantityStepper {
                            Layout.preferredWidth: 145
                            card: deckBuilder.selectedCard
                        }
                    }
                }
            }
        }
        Rectangle {
            id: deckBar
            Layout.fillWidth: true
            implicitHeight: 44
            radius: 8
            color: deckBarHover.hovered ? "#2c3542" : "#252e3a"
            border.color: "#405d80"
            HoverHandler {
                id: deckBarHover
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: deckDrawer.open()
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                spacing: 10
                Label {
                    text: "▤"
                    color: "#80baff"
                    font.pixelSize: 20
                }
                Label {
                    Layout.preferredWidth: 150
                    Layout.maximumWidth: 210
                    Layout.minimumWidth: 0
                    text: deckBuilder.deckName + (deckBuilder.dirty ? " •" : "")
                    textFormat: Text.PlainText
                    color: "#f0f4fb"
                    font.pixelSize: 12
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.bottomDeckStatsText
                    textFormat: Text.PlainText
                    color: "#aabbd2"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                DarkButton {
                    id: bottomExport
                    text: "Export ▴"
                    primary: true
                    enabled: !deckBuilder.exporting
                    onClicked: root.openExportMenu(bottomExport)
                }
                DarkButton {
                    text: "View Deck ↑"
                    primary: true
                    onClicked: deckDrawer.open()
                }
            }
        }
    }

    Basic.Popup {
        id: referencePopup
        parent: root.overlayItem || root
        anchors.centerIn: parent
        width: Math.max(1, Math.min(1000, parent.width - 32))
        height: Math.max(1, Math.min(740, parent.height - 32))
        padding: 18
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: {
            root.hideReferencePreview();
            deckBuilder.closeReference();
        }
        background: Rectangle {
            color: "#242a33"
            radius: 12
            border.color: "#536176"
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: (deckBuilder.referenceKind === "trait" ? "Trait · " : deckBuilder.referenceKind === "name" ? "Name contains · " : "Ability · ") + deckBuilder.referenceValue
                    textFormat: Text.PlainText
                    color: deckBuilder.referenceKind === "trait" ? "#77d9a0" : deckBuilder.referenceKind === "name" ? "#80baff" : "#efc779"
                    font.pixelSize: 18
                    font.bold: true
                    wrapMode: Text.Wrap
                }
                DarkButton {
                    text: "×"
                    quiet: true
                    onClicked: referencePopup.close()
                }
            }
            Label {
                Layout.fillWidth: true
                text: (deckBuilder.referenceDeckOnly ? "My deck" : root.currentSetName) + " · " + deckBuilder.referenceTotal + " matching cards · Hover to preview; use + / − to edit your deck."
                color: root.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                DarkCombo {
                    Layout.preferredWidth: 180
                    model: ["Name contains", "Trait", "Ability contains"]
                    currentIndex: ["name", "trait", "ability"].indexOf(deckBuilder.referenceKind)
                    onActivated: function (index) {
                        deckBuilder.openReference(["name", "trait", "ability"][index], deckBuilder.referenceValue);
                        currentIndex = Qt.binding(function () {
                            return ["name", "trait", "ability"].indexOf(deckBuilder.referenceKind);
                        });
                    }
                }
                Label {
                    text: "Search in"
                    color: root.muted
                    font.pixelSize: 12
                }
                DarkCombo {
                    Layout.preferredWidth: 180
                    model: ["Current set", "My deck"]
                    currentIndex: deckBuilder.referenceDeckOnly ? 1 : 0
                    onActivated: function (index) {
                        deckBuilder.referenceDeckOnly = index === 1;
                        currentIndex = Qt.binding(function () {
                            return deckBuilder.referenceDeckOnly ? 1 : 0;
                        });
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                DarkButton {
                    text: "Use builder order"
                    enabled: root.referenceSortOverride !== null
                    onClicked: {
                        root.referenceSortOverride = null;
                        root.applyReferenceSort();
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: "Order"
                    color: root.muted
                    font.pixelSize: 12
                }
                DarkCombo {
                    Layout.preferredWidth: 125
                    model: ["No grouping", "Color", "Level"]
                    enabled: root.referenceSortMode !== "colorLevelCostPower"
                    currentIndex: root.referenceGroupMode === "color" ? 1 : root.referenceGroupMode === "level" ? 2 : 0
                    onActivated: function (index) {
                        root.changeReferenceSort(["none", "color", "level"][index], root.referenceSortMode, root.referenceDescending);
                        currentIndex = Qt.binding(function () {
                            return root.referenceGroupMode === "color" ? 1 : root.referenceGroupMode === "level" ? 2 : 0;
                        });
                    }
                }
                DarkCombo {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 130
                    model: ["Card number", "Card name", "Level", "Power", "Color › level › cost › power"]
                    currentIndex: ["code", "name", "level", "power", "colorLevelCostPower"].indexOf(root.referenceSortMode)
                    onActivated: function (index) {
                        var sort = ["code", "name", "level", "power", "colorLevelCostPower"][index];
                        root.changeReferenceSort(sort === "colorLevelCostPower" ? "color" : root.referenceGroupMode, sort, root.referenceDescending);
                        currentIndex = Qt.binding(function () {
                            return ["code", "name", "level", "power", "colorLevelCostPower"].indexOf(root.referenceSortMode);
                        });
                    }
                }
                DarkButton {
                    neutral: true
                    Layout.preferredWidth: 108
                    text: root.referenceDescending ? "↓ Descending" : "↑ Ascending"
                    onClicked: root.changeReferenceSort(root.referenceGroupMode, root.referenceSortMode, !root.referenceDescending)
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !!deckBuilder.referenceError
                text: deckBuilder.referenceError
                textFormat: Text.PlainText
                color: "#eea6a6"
                wrapMode: Text.Wrap
            }
            ListView {
                id: referenceList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: referenceRowsModel
                clip: true
                spacing: 10
                reuseItems: true
                onContentYChanged: {
                    root.hideReferencePreview();
                    referenceLoadTimer.restart();
                }
                onHeightChanged: referenceLoadTimer.restart()
                Basic.ScrollBar.vertical: Basic.ScrollBar {}
                delegate: Item {
                    id: referenceRow
                    required property string rowType
                    required property string groupKey
                    required property string heading
                    required property int counter
                    required property bool collapsed
                    required property string cardJson
                    width: referenceList.width
                    height: rowType === "header" ? 38 : 300
                    DarkButton {
                        id: referenceHeader
                        readonly property bool isColorGroup: root.referenceGroupMode === "color" || root.referenceSortMode === "colorLevelCostPower"
                        anchors.fill: parent
                        visible: referenceRow.rowType === "header"
                        onClicked: root.toggleReferenceGroup(referenceRow.groupKey)
                        background: Rectangle {
                            radius: 7
                            color: referenceHeader.down ? "#3d424b" : referenceHeader.hovered ? "#343941" : "#2c3037"
                            border.width: referenceHeader.isColorGroup ? 2 : 1
                            border.color: referenceHeader.isColorGroup ? root.colorGroupBorder(referenceRow.groupKey) : root.line
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Label {
                                text: referenceRow.collapsed ? "▸" : "▾"
                                color: "#80baff"
                                font.pixelSize: 16
                            }
                            Label {
                                text: referenceRow.heading
                                color: "#d7e4f8"
                                font.bold: true
                                font.pixelSize: 13
                            }
                            Label {
                                text: referenceRow.counter + " loaded"
                                color: root.muted
                                font.pixelSize: 11
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Label {
                                text: referenceRow.collapsed ? "Expand" : "Collapse"
                                color: root.muted
                                font.pixelSize: 11
                            }
                        }
                    }
                    Row {
                        anchors.fill: parent
                        spacing: 10
                        visible: referenceRow.rowType === "cards"
                        Repeater {
                            model: referenceRow.rowType === "cards" ? JSON.parse(referenceRow.cardJson) : []
                            CardTile {
                                required property var modelData
                                card: modelData
                                referenceResult: true
                                width: Math.floor((referenceList.width - (root.referenceColumns - 1) * 10) / root.referenceColumns)
                            }
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    visible: !deckBuilder.referenceBusy && referenceRowsModel.count === 0 && !deckBuilder.referenceError
                    text: deckBuilder.referenceDeckOnly ? "No matching cards in your deck." : "No matching cards in this set."
                    horizontalAlignment: Text.AlignHCenter
                    color: root.muted
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: deckBuilder.referenceResults.length + " of " + deckBuilder.referenceTotal + " loaded"
                    color: root.muted
                    font.pixelSize: 11
                }
                Basic.BusyIndicator {
                    implicitWidth: 26
                    implicitHeight: 26
                    running: deckBuilder.referenceBusy
                    visible: running
                }
                DarkButton {
                    text: deckBuilder.referenceError ? "Retry" : "Load more"
                    visible: !!deckBuilder.referenceError || deckBuilder.referenceHasMore
                    enabled: !deckBuilder.referenceBusy
                    onClicked: {
                        if (deckBuilder.referenceHasMore)
                            deckBuilder.loadMoreReferences();
                        else
                            deckBuilder.openReference(deckBuilder.referenceKind, deckBuilder.referenceValue);
                    }
                }
            }
        }
    }

    Item {
        id: previewMeasureHost
        x: -10000
        width: Math.max(1, referencePreview.width - referencePreview.leftPadding - referencePreview.rightPadding)
        height: previewMeasureBody.implicitHeight
        opacity: 0
        enabled: false
        visible: !!root.previewAnchor
        PreviewDetails {
            id: previewMeasureBody
            width: parent.width
        }
    }

    Basic.Popup {
        id: referencePreview
        parent: root.overlayItem || root
        z: referencePopup.z + 2
        width: Math.max(1, Math.min(340, parent.width - 24))
        property real preparedHeight: 1
        height: preparedHeight
        padding: 12
        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 90
            }
        }
        exit: Transition {}
        modal: false
        focus: false
        clip: true
        closePolicy: Popup.NoAutoClose
        function reposition() {
            if (!root.previewAnchor)
                return;
            var point = root.previewAnchor.mapToItem(parent, 0, 0);
            if (root.previewAnchor.deckPreviewAnchor === true) {
                var anchor = root.previewAnchor;
                var pointer = anchor.mapToItem(parent, anchor.previewPointerX, anchor.previewPointerY);

                var edge = 12;
                var gap = 20;

                var maxX = Math.max(edge, parent.width - width - edge);
                var maxY = Math.max(edge, parent.height - height - edge);

                x = Math.max(edge, Math.min(pointer.x + gap, maxX));

                var above = point.y - height - gap;
                var below = point.y + anchor.height + gap;

                if (above >= edge) {
                    y = above;
                } else if (below + height <= parent.height - edge) {
                    y = below;
                } else {
                    var right = point.x + anchor.width + gap;
                    var left = point.x - width - gap;

                    if (right + width <= parent.width - edge) {
                        x = right;
                    } else if (left >= edge) {
                        x = left;
                    } else {
                        root.hideReferencePreview();
                        return;
                    }

                    y = Math.max(edge, Math.min(pointer.y - 24, maxY));
                }
            } else {
                var rightSpace = parent.width - point.x - root.previewAnchor.width - 20;
                var leftSpace = point.x - 20;
                if (rightSpace >= width)
                    x = point.x + root.previewAnchor.width + 10;
                else if (leftSpace >= width)
                    x = point.x - width - 10;
                else
                    x = (parent.width - width) / 2;
                x = Math.max(12, Math.min(x, parent.width - width - 12));
                y = Math.max(12, Math.min(point.y, parent.height - height - 12));
            }
        }
        onAboutToShow: reposition()
        onWidthChanged: {
            if (visible)
                root.hideReferencePreview();
        }
        Connections {
            target: referencePreview.parent
            function onHeightChanged() {
                if (referencePreview.visible)
                    root.hideReferencePreview();
            }
        }
        background: Rectangle {
            color: "#20252d"
            radius: 10
            border.color: "#6a7c95"
        }
        contentItem: Item {
            implicitHeight: previewBody.implicitHeight
            clip: true
            HoverHandler {
                id: previewHover
                margin: referencePreview.padding
                onHoveredChanged: {
                    if (hovered && referencePreview.visible)
                        root.hideReferencePreview();
                }
            }
            PreviewDetails {
                id: previewBody
                width: Math.max(1, referencePreview.width - referencePreview.leftPadding - referencePreview.rightPadding)
            }
        }
    }

    Basic.Popup {
        id: titlesPopup
        parent: root.overlayItem || root
        width: Math.max(1, Math.min(620, parent.width - 24))
        height: Math.max(1, Math.min(440, parent.height - 24))
        padding: 12
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        readonly property var filteredTitles: {
            var query = titleSearch.text.trim().toLowerCase();
            var matches = [];
            for (var i = 0; i < deckBuilder.titles.length; ++i) {
                var title = deckBuilder.titles[i];
                if (!query || String(title.label).toLowerCase().indexOf(query) >= 0)
                    matches.push(title);
            }
            return matches;
        }
        onAboutToShow: {
            var position = titlePicker.mapToItem(parent, 0, titlePicker.height + 6);
            x = Math.max(12, Math.min(position.x, parent.width - width - 12));
            y = Math.max(12, Math.min(position.y, parent.height - height - 12));
        }
        onOpened: titleSearch.forceActiveFocus()
        onClosed: titleSearch.text = ""
        background: Rectangle {
            color: "#282d35"
            radius: 10
            border.color: "#536176"
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: "Choose a set"
                color: root.ink
                font.pixelSize: 14
                font.bold: true
            }
            DarkField {
                id: titleSearch
                Layout.fillWidth: true
                placeholderText: "Search set name or code…"
                onTextChanged: titleList.currentIndex = titlesPopup.filteredTitles.length ? 0 : -1
                onAccepted: {
                    if (titleList.currentIndex >= 0)
                        root.chooseTitle(titlesPopup.filteredTitles[titleList.currentIndex]);
                }
                Keys.onDownPressed: titleList.forceActiveFocus()
            }
            Label {
                Layout.fillWidth: true
                visible: !!deckBuilder.catalogueNotice
                text: deckBuilder.catalogueNotice
                color: "#d8bd82"
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            ListView {
                id: titleList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: titlesPopup.filteredTitles
                clip: true
                spacing: 4
                currentIndex: count ? 0 : -1
                keyNavigationEnabled: true
                Basic.ScrollBar.vertical: Basic.ScrollBar {}
                Keys.onReturnPressed: {
                    if (currentIndex >= 0)
                        root.chooseTitle(titlesPopup.filteredTitles[currentIndex]);
                }
                delegate: Basic.ItemDelegate {
                    id: titleOption
                    required property var modelData
                    required property int index
                    width: titleList.width
                    height: 58
                    hoverEnabled: true
                    background: Rectangle {
                        radius: 6
                        color: titleOption.hovered || titleList.currentIndex === titleOption.index ? "#354256" : "transparent"
                    }
                    contentItem: ColumnLayout {
                        spacing: 3
                        Label {
                            Layout.fillWidth: true
                            text: titleOption.modelData.setNames
                            color: "#8fc3ff"
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            text: titleOption.modelData.setCodes.join(" / ") + " · " + titleOption.modelData.cardCount + " cards"
                            textFormat: Text.PlainText
                            color: root.ink
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    onClicked: root.chooseTitle(modelData)
                }
                Label {
                    anchors.centerIn: parent
                    visible: titleList.count === 0
                    text: "No matching sets"
                    color: root.muted
                }
            }
        }
    }

    Basic.Popup {
        id: exportMenu
        parent: root.overlayItem || root
        z: referencePreview.z + 2
        width: 190
        height: 92
        padding: 8
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: "#282f39"
            radius: 8
            border.color: "#536176"
        }
        contentItem: ColumnLayout {
            spacing: 4
            DarkButton {
                text: "Export as .txt"
                Layout.fillWidth: true
                onClicked: {
                    exportMenu.close();
                    deckBuilder.saveText();
                }
            }
            DarkButton {
                text: "Export as .png"
                Layout.fillWidth: true
                enabled: deckBuilder.distinctCards > 0
                onClicked: {
                    exportMenu.close();
                    deckBuilder.exportImage();
                }
            }
        }
    }

    Basic.Drawer {
        id: deckDrawer
        // Stay inside the embedded page, clear of the Widgets sidebar.
        parent: root
        edge: Qt.BottomEdge
        x: 0
        width: Math.max(1, root.width)
        height: Math.max(1, Math.min(650, root.height * 0.82))
        onClosed: root.hideReferencePreview()
        modal: true
        leftPadding: 0
        rightPadding: 0
        topPadding: 24
        bottomPadding: 24
        background: Rectangle {
            color: "#22262d"
            border.width: 0
            Rectangle {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 6
                width: 42
                height: 4
                radius: 2
                color: "#637084"
            }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 52
                Layout.rightMargin: 52
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label {
                        text: deckBuilder.deckName + (deckBuilder.dirty ? " •" : "")
                        textFormat: Text.PlainText
                        font.pixelSize: 21
                        font.bold: true
                        color: root.ink
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.deckStatsHtml
                        textFormat: Text.RichText
                        color: root.muted
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                DarkButton {
                    text: "New Deck"
                    enabled: !deckBuilder.exporting
                    onClicked: deckBuilder.newDeck()
                }
                DarkButton {
                    text: "Open TXT"
                    enabled: !deckBuilder.exporting
                    onClicked: deckBuilder.openText()
                }
                Item {
                    Layout.fillWidth: true
                }
                DarkButton {
                    text: "Cancel export"
                    danger: true
                    visible: deckBuilder.exporting
                    enabled: deckBuilder.exportCancellable
                    onClicked: deckBuilder.cancelExport()
                }
                DarkButton {
                    id: drawerExport
                    text: "Export ▴"
                    primary: true
                    enabled: !deckBuilder.exporting
                    onClicked: root.openExportMenu(drawerExport)
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                text: deckBuilder.status
                textFormat: Text.PlainText
                color: root.muted
                wrapMode: Text.Wrap
                font.pixelSize: 11
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: deckBuilder.deckModel
                onContentYChanged: root.hideReferencePreview()
                clip: true
                spacing: 6
                Basic.ScrollBar.vertical: Basic.ScrollBar {}
                delegate: Rectangle {
                    id: deckRow
                    required property var cardData
                    required property int copies
                    width: ListView.view.width
                    x: 0
                    Component.onDestruction: {
                        if (root.previewAnchor === deckInfoArea)
                            root.hideReferencePreview();
                    }
                    height: 90
                    color: deckInfoHover.hovered ? "#303843" : "#2a3039"
                    radius: 8
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 16
                        Item {
                            id: deckInfoArea
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80
                            Layout.fillHeight: true
                            readonly property bool hovered: deckInfoHover.hovered
                            readonly property bool deckPreviewAnchor: true
                            readonly property real previewPointerX: deckInfoHover.point.position.x
                            readonly property real previewPointerY: deckInfoHover.point.position.y
                            HoverHandler {
                                id: deckInfoHover
                                cursorShape: Qt.PointingHandCursor
                                onHoveredChanged: {
                                    if (hovered)
                                        root.queueReferencePreview(deckInfoArea, deckRow.cardData);
                                    else if (root.previewAnchor === deckInfoArea)
                                        root.schedulePreviewHide();
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.hideReferencePreview();
                                    deckBuilder.selectCard(deckRow.cardData);
                                    deckDrawer.close();
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                spacing: 12
                                Image {
                                    Layout.preferredWidth: 44
                                    Layout.preferredHeight: 62
                                    source: deckBuilder.imageSource(deckRow.cardData.picture || "")
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 4
                                    Label {
                                        Layout.fillWidth: true
                                        text: deckRow.cardData.cardName
                                        textFormat: Text.PlainText
                                        color: root.ink
                                        font.pixelSize: 13
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: deckRow.cardData.cardCode
                                        color: root.muted
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                        DarkButton {
                            text: "Remove"
                            quiet: true
                            danger: true
                            onClicked: deckBuilder.changeQuantity(deckRow.cardData, -deckRow.copies)
                        }
                        QuantityStepper {
                            Layout.preferredWidth: 145
                            Layout.minimumWidth: 145
                            Layout.maximumWidth: 145
                            Layout.alignment: Qt.AlignRight
                            card: deckRow.cardData
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: deckBuilder.distinctCards === 0
                    text: "Your deck is empty\nUse + on a card to add your first copy."
                    color: root.muted
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                }
            }
        }
    }

    Basic.Popup {
        id: advanced
        parent: root.overlayItem || root
        anchors.centerIn: parent
        width: Math.max(1, Math.min(640, parent.width - 32))
        height: Math.max(1, Math.min(700, parent.height - 32))
        padding: 20
        modal: true
        focus: true
        property var kinds: []
        property var colors: []
        function toggle(list, value) {
            var next = list.slice(), index = next.indexOf(value);
            if (index < 0)
                next.push(value);
            else
                next.splice(index, 1);
            return next;
        }
        function resetFields() {
            nameFilter.text = "";
            codeFilter.text = "";
            textFilter.text = "";
            traitFilter.text = "";
            rarityFilter.text = "";
            triggerFilter.text = "";
            kinds = [];
            colors = [];
            levelMin.text = "";
            levelMax.text = "";
            costMin.text = "";
            costMax.text = "";
            powerMin.text = "";
            powerMax.text = "";
            soulMin.text = "";
            soulMax.text = "";
        }
        function fields() {
            return {
                name: nameFilter.text,
                code: codeFilter.text,
                text: textFilter.text,
                trait: traitFilter.text,
                rarity: rarityFilter.text,
                trigger: triggerFilter.text,
                cardType: kinds,
                colors: colors,
                levelMin: levelMin.text,
                levelMax: levelMax.text,
                costMin: costMin.text,
                costMax: costMax.text,
                powerMin: powerMin.text,
                powerMax: powerMax.text,
                soulMin: soulMin.text,
                soulMax: soulMax.text
            };
        }
        onAboutToShow: {
            var f = root.appliedFilters;
            nameFilter.text = f.name || "";
            codeFilter.text = f.code || "";
            textFilter.text = f.text || "";
            traitFilter.text = f.trait || "";
            rarityFilter.text = f.rarity || "";
            var triggerKeys = Triggers.tokens(f.trigger || "");
            triggerFilter.text = String(f.trigger || "").toLowerCase() === "none" ? "none" : triggerKeys.length ? triggerKeys[0] : "";
            kinds = f.cardType ? f.cardType.slice() : [];
            colors = f.colors ? f.colors.slice() : [];
            levelMin.text = f.levelMin || "";
            levelMax.text = f.levelMax || "";
            costMin.text = f.costMin || "";
            costMax.text = f.costMax || "";
            powerMin.text = f.powerMin || "";
            powerMax.text = f.powerMax || "";
            soulMin.text = f.soulMin || "";
            soulMax.text = f.soulMax || "";
        }
        background: Rectangle {
            color: "#282d35"
            radius: 12
            border.color: "#536176"
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Label {
                text: "Advanced Search"
                color: root.ink
                font.pixelSize: 20
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: "Current set only. Searches original and translated card text together."
                color: root.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            Basic.ScrollView {
                id: filterScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: filterScroll.availableWidth
                    spacing: 10
                    DarkField {
                        id: nameFilter
                        Layout.fillWidth: true
                        placeholderText: "Name contains"
                    }
                    DarkField {
                        id: codeFilter
                        Layout.fillWidth: true
                        placeholderText: "Card code contains"
                    }
                    DarkField {
                        id: textFilter
                        Layout.fillWidth: true
                        placeholderText: "Original or translated card text contains"
                    }
                    DarkField {
                        id: traitFilter
                        Layout.fillWidth: true
                        placeholderText: "Exact trait"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        DarkField {
                            id: rarityFilter
                            Layout.fillWidth: true
                            placeholderText: "Rarity, e.g. RR"
                        }
                        DarkCombo {
                            id: triggerFilter
                            Layout.fillWidth: true
                            property string text: ""
                            model: Triggers.filterOptions(["CHOICE", "COMEBACK", "SALVAGE", "DRAW", "BOOK", "FOCUS", "GATE", "PANTS", "POOL", "STOCK", "BAG", "RETURN", "BOUNCE", "SHOT", "SOUL", "STANDBY", "TREASURE", "BAR", "DISCOVERY", "CHANCE"])
                            textRole: "label"
                            currentIndex: {
                                for (var i = 0; i < model.length; ++i)
                                    if (model[i].value === text)
                                        return i;
                                return 0;
                            }
                            onActivated: function (index) {
                                text = model[index].value;
                                currentIndex = Qt.binding(function () {
                                    for (var i = 0; i < model.length; ++i)
                                        if (model[i].value === text)
                                            return i;
                                    return 0;
                                });
                            }
                            contentItem: Item {
                                implicitHeight: 20
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 6
                                    Shared.TriggerIconRow {
                                        Layout.preferredWidth: 18
                                        Layout.preferredHeight: 14
                                        Layout.alignment: Qt.AlignVCenter
                                        visible: triggerKeys.length > 0
                                        value: triggerFilter.text
                                        iconSize: 14
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        text: triggerFilter.displayText
                                        color: root.ink
                                        font: triggerFilter.font
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            delegate: Basic.ItemDelegate {
                                required property var modelData
                                required property int index
                                width: triggerFilter.width
                                highlighted: triggerFilter.highlightedIndex === index
                                implicitHeight: 34
                                contentItem: Item {
                                    implicitHeight: 20
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 8
                                        Shared.TriggerIconRow {
                                            Layout.preferredWidth: 18
                                            Layout.preferredHeight: 14
                                            Layout.alignment: Qt.AlignVCenter
                                            visible: triggerKeys.length > 0
                                            value: modelData.value
                                            iconSize: 14
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            text: modelData.label
                                            color: root.ink
                                            font.pixelSize: 12
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                background: Rectangle {
                                    color: hovered || highlighted ? "#3b485b" : "#292f38"
                                }
                            }
                        }
                    }
                    Label {
                        text: "KINDS  ·  Leave empty for Any"
                        color: root.muted
                        font.pixelSize: 10
                        font.letterSpacing: 0.7
                    }
                    RowLayout {
                        Repeater {
                            model: [
                                {
                                    code: "CH",
                                    name: "Character"
                                },
                                {
                                    code: "EV",
                                    name: "Event"
                                },
                                {
                                    code: "CX",
                                    name: "Climax"
                                }
                            ]
                            DarkCheck {
                                required property var modelData
                                text: modelData.name
                                checked: advanced.kinds.indexOf(modelData.code) >= 0
                                onClicked: advanced.kinds = advanced.toggle(advanced.kinds, modelData.code)
                            }
                        }
                    }
                    Label {
                        text: "COLORS  ·  Leave empty for Any"
                        color: root.muted
                        font.pixelSize: 10
                        font.letterSpacing: 0.7
                    }
                    RowLayout {
                        Repeater {
                            model: ["RED", "BLUE", "GREEN", "YELLOW"]
                            DarkCheck {
                                required property string modelData
                                text: modelData
                                checked: advanced.colors.indexOf(modelData) >= 0
                                onClicked: advanced.colors = advanced.toggle(advanced.colors, modelData)
                            }
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 10
                        rowSpacing: 8
                        Label {
                            text: "STAT"
                            color: root.muted
                            font.pixelSize: 10
                        }
                        Label {
                            text: "MINIMUM"
                            color: root.muted
                            font.pixelSize: 10
                        }
                        Label {
                            text: "MAXIMUM"
                            color: root.muted
                            font.pixelSize: 10
                        }
                        Label {
                            text: "Level"
                            color: root.ink
                            font.pixelSize: 12
                        }
                        DarkField {
                            id: levelMin
                            Layout.fillWidth: true
                            placeholderText: "Any"
                            validator: IntValidator {
                                bottom: 0
                                top: 999999
                            }
                        }
                        DarkField {
                            id: levelMax
                            Layout.fillWidth: true
                            placeholderText: "Any"
                            validator: IntValidator {
                                bottom: 0
                                top: 999999
                            }
                        }
                        Label {
                            text: "Cost"
                            color: root.ink
                            font.pixelSize: 12
                        }
                        DarkField {
                            id: costMin
                            Layout.fillWidth: true
                            placeholderText: "Any"
                            validator: IntValidator {
                                bottom: 0
                                top: 999999
                            }
                        }
                        DarkField {
                            id: costMax
                            Layout.fillWidth: true
                            placeholderText: "Any"
                            validator: IntValidator {
                                bottom: 0
                                top: 999999
                            }
                        }
                        Label {
                            text: "Power"
                            color: root.ink
                            font.pixelSize: 12
                        }
                        DarkField {
                            id: powerMin
                            Layout.fillWidth: true
                            placeholderText: "Any"
                            validator: IntValidator {
                                bottom: 0
                                top: 999999
                            }
                        }
                        DarkField {
                            id: powerMax
                            Layout.fillWidth: true
                            placeholderText: "Any"
                            validator: IntValidator {
                                bottom: 0
                                top: 999999
                            }
                        }
                        Label {
                            text: "Soul"
                            color: root.ink
                            font.pixelSize: 12
                        }
                        DarkField {
                            id: soulMin
                            Layout.fillWidth: true
                            placeholderText: "Any"
                            validator: IntValidator {
                                bottom: 0
                                top: 999999
                            }
                        }
                        DarkField {
                            id: soulMax
                            Layout.fillWidth: true
                            placeholderText: "Any"
                            validator: IntValidator {
                                bottom: 0
                                top: 999999
                            }
                        }
                    }
                }
            }
            RowLayout {
                DarkButton {
                    text: "Reset fields"
                    onClicked: advanced.resetFields()
                }
                Item {
                    Layout.fillWidth: true
                }
                DarkButton {
                    text: "Cancel"
                    onClicked: advanced.close()
                }
                DarkButton {
                    text: "Apply Filters"
                    primary: true
                    onClicked: {
                        quickName.text = "";
                        searchTimer.stop();
                        root.appliedFilters = advanced.fields();
                        root.runSearch(0);
                        advanced.close();
                    }
                }
            }
        }
    }
}
