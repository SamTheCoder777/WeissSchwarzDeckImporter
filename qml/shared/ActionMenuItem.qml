import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.MenuItem {
    id: control
    implicitHeight: 34
    implicitWidth: Math.max(185, contentItem.implicitWidth + 24)
    leftPadding: 12
    rightPadding: 12
    contentItem: Text {
        text: control.text
        color: control.enabled ? "#e6e6e6" : "#77808d"
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 5
        color: control.highlighted ? "#3b485b" : "transparent"
    }
}
