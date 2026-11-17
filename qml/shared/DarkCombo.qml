import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Basic.ComboBox {
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
        color: "#e6e6e6"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 10
        y: (control.height - height) / 2
        text: "▾"
        color: "#9aa0aa"
    }
    background: Rectangle {
        radius: 7
        color: control.hovered ? "#30353d" : "#262a30"
        border.color: control.activeFocus ? "#4d9dff" : "#3a3c40"
    }
    delegate: Basic.ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: control.width
        implicitHeight: 32
        contentItem: Text {
            text: String(option.modelData)
            color: option.highlighted ? "#80baff" : "#e6e6e6"
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
