import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Button {
    id: control
    property bool primary: false
    implicitHeight: 32
    implicitWidth: Math.max(40, contentItem.implicitWidth + 24)
    padding: 8
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    font.pixelSize: 12
    font.family: "Segoe UI"
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? "white" : "#e6e6e6"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 7
        color: control.primary ? (control.down ? "#347dce" : control.hovered ? "#65acff" : "#4d9dff")
                               : control.down ? "#3d424b" : control.hovered ? "#343941" : "#2b3038"
        border.color: control.primary ? "#77b8ff" : "#505966"
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
