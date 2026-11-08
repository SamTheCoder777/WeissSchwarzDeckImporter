import QtQuick 2.15
import QtQuick.Controls 2.15
import "TriggerIcons.js" as Triggers

Flow {
    id: root
    property string value: ""
    property int iconSize: 14
    property color textColor: "#e6e6e6"
    readonly property var triggerKeys: Triggers.tokens(value)
    spacing: 4
    Repeater {
        model: root.triggerKeys
        delegate: Item {
            id: slot
            required property string modelData
            width: icon.status === Image.Error ? fallback.implicitWidth : root.iconSize
            height: root.iconSize
            Image {
                id: icon
                anchors.fill: parent
                source: Triggers.source(slot.modelData)
                fillMode: Image.PreserveAspectFit
                // Trigger GIFs are displayed as static symbols.
            }
            Label {
                id: fallback
                anchors.centerIn: parent
                visible: icon.status === Image.Error
                text: Triggers.label(slot.modelData)
                color: root.textColor
                font.pixelSize: 11
            }
            HoverHandler { id: hover }
            ToolTip.visible: hover.hovered
            ToolTip.text: Triggers.label(slot.modelData)
        }
    }
    Label {
        visible: root.triggerKeys.length === 0
        text: "-"
        color: root.textColor
        font.pixelSize: 12
    }
}
