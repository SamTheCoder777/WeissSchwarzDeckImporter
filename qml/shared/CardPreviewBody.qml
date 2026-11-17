import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Controls
import QtQuick.Layouts
import "TriggerIcons.js" as Triggers

Column {
    id: detailsColumn
    property var card: ({})
    property url imageSource: ""
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
            source: detailsColumn.imageSource
            asynchronous: true
            fillMode: Image.PreserveAspectFit
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: detailsColumn.card.cardName || ""
                textFormat: Text.PlainText
                color: "#e6e6e6"
                font.bold: true
                font.pixelSize: 13
                wrapMode: Text.Wrap
                clip: true
            }
            Label {
                Layout.fillWidth: true
                text: detailsColumn.card.cardCode || ""
                textFormat: Text.PlainText
                color: "#9aa0aa"
                font.pixelSize: 10
                wrapMode: Text.Wrap
                clip: true
            }
            Label {
                Layout.fillWidth: true
                text: [detailsColumn.card.cardKind, detailsColumn.card.color, detailsColumn.card.rarity].filter(function (value) {
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
            color: "#9aa0aa"
            font.pixelSize: 11
        }
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: detailsColumn.card.level === undefined || detailsColumn.card.level === "" ? "—" : String(detailsColumn.card.level)
            color: "#e6e6e6"
            font.pixelSize: 11
            wrapMode: Text.Wrap
            clip: true
        }
        Label {
            text: "Cost:"
            color: "#9aa0aa"
            font.pixelSize: 11
        }
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: detailsColumn.card.cost === undefined || detailsColumn.card.cost === "" ? "—" : String(detailsColumn.card.cost)
            color: "#e6e6e6"
            font.pixelSize: 11
            wrapMode: Text.Wrap
            clip: true
        }
        Label {
            text: "Power:"
            color: "#9aa0aa"
            font.pixelSize: 11
        }
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: detailsColumn.card.power || "—"
            color: "#e6e6e6"
            font.pixelSize: 11
            wrapMode: Text.Wrap
            clip: true
        }
        Label {
            text: "Soul:"
            color: "#9aa0aa"
            font.pixelSize: 11
        }
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: Triggers.inlineText(detailsColumn.card.soul || "—", 14)
            textFormat: Text.RichText
            color: "#e6e6e6"
            font.pixelSize: 11
            wrapMode: Text.Wrap
            clip: true
        }
        Label {
            text: "Trigger:"
            color: "#9aa0aa"
            font.pixelSize: 11
        }
        TriggerIconRow {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            value: detailsColumn.card.cardTrigger || ""
            iconSize: 14
        }
    }
    Label {
        width: parent.width
        text: [detailsColumn.card.feature1, detailsColumn.card.feature2, detailsColumn.card.feature3].filter(function (value) {
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
        color: "#3a3c40"
    }
    Label {
        width: parent.width
        text: Triggers.inlineText(detailsColumn.card.text || "", 14)
        textFormat: Text.RichText
        color: "#e6e6e6"
        font.pixelSize: 11
        wrapMode: Text.Wrap
        clip: true
    }
}
