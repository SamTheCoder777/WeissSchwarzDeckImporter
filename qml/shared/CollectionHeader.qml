import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Controls
import QtQuick.Layouts

Basic.AbstractButton {
    id: control
    property string title: ""
    property int cardCount: 0
    property bool collapsed: false
    property color markerColor: "#8494ab"
    implicitHeight: 34
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    hoverEnabled: true
    text: title + " · " + cardCount
    Accessible.name: (collapsed ? "Expand " : "Collapse ") + title + ", " + cardCount + " cards"
    background: Rectangle {
        color: control.hovered ? "#272e38" : "transparent"
        radius: 5
    }
    contentItem: RowLayout {
        spacing: 9
        Label {
            text: control.collapsed ? "▸" : "▾"
            color: "#a5afbd"
            font.pixelSize: 12
        }
        Rectangle {
            width: 4
            height: 16
            radius: 2
            color: control.markerColor
        }
        Label {
            text: control.title
            color: "#dce3ed"
            font.pixelSize: 13
            font.bold: true
        }
        Label {
            text: control.cardCount
            color: "#8e9aac"
            font.pixelSize: 11
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 5
            height: 1
            color: "#343d4a"
        }
    }
    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
