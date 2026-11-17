import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../shared" as Shared

Rectangle {
    id: root
    color: "#1e1f22"
    property alias activeTab: toolbar.currentIndex
    readonly property bool showingDeck: activeTab === 1

    function showDeck() {
        activeTab = 1;
    }
    function importIntoDeck(format) {
        activeTab = 1;
        if (format === "txt") deckBuilder.openText();
        else deckBuilder.importDeckCode();
    }
    function exportCurrent(format) {
        if (showingDeck) {
            if (format === "txt") deckBuilder.saveText();
            else if (format === "png") deckBuilder.exportImage();
            else deckBuilder.copyDeckCode();
        } else {
            galleryController.exportDetected(format);
        }
    }
    function detectedImage(card) {
        var revision = cardDatabase.dataRevision;
        return card.cardCode ? cardDatabase.cardImageSource(card.cardCode) : "";
    }
    function deckImage(card) {
        return card.picture ? deckBuilder.imageSource(card.picture) : "";
    }
    Connections {
        target: galleryController
        function onShowDeckRequested() { root.showDeck(); }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Shared.GalleryToolbar {
            id: toolbar
            Layout.fillWidth: true
            activePane: root.showingDeck ? deckPane : detectedPane
            popupParent: root.Overlay.overlay || root
            onImportRequested: function(format) { root.importIntoDeck(format); }
            onExportRequested: function(format) { root.exportCurrent(format); }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.activeTab
            Shared.CollectionPane {
                id: detectedPane
                cards: galleryController.detectedRows
                sourceFor: root.detectedImage
                showOrderingControls: false
                groupMode: "level"
                sortMode: "composite"
                descending: true
            }
            Shared.CollectionPane {
                id: deckPane
                cards: deckBuilder.deckRows
                editable: true
                sourceFor: root.deckImage
                showOrderingControls: false
                groupMode: "level"
                sortMode: "composite"
                descending: true
            }
        }
        RowLayout {
            visible: deckBuilder.exporting
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Shared.ActionButton {
                text: "Cancel export"
                enabled: deckBuilder.exportCancellable
                onClicked: deckBuilder.cancelExport()
            }
        }
    }
}
