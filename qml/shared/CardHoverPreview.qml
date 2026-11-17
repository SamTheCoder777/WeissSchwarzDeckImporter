import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

Item {
    id: root
    property Item overlayItem: null
    property var sourceFor: function(card) { return ""; }
    property var anchor: null
    property var card: ({})
    function queue(item, data) {
        dismiss();
        anchor = item;
        card = data;
        delay.restart();
    }
    function dismiss() {
        delay.stop();
        measure.stop();
        popup.close();
        anchor = null;
    }
    function leave(item) {
        if (anchor === item) dismiss();
    }
    Timer {
        id: delay
        interval: 350
        onTriggered: {
            measure.lastHeight = -1;
            measure.stableFrames = 0;
            measure.start();
        }
    }
    Timer {
        id: measure
        interval: 30
        repeat: true
        property int lastHeight: -1
        property int stableFrames: 0
        onTriggered: {
            if (!root.anchor || !root.anchor.hovered) {
                root.dismiss();
                return;
            }
            var next = Math.ceil(measureBody.implicitHeight + popup.topPadding + popup.bottomPadding);
            stableFrames = next === lastHeight ? stableFrames + 1 : 0;
            lastHeight = next;
            if (stableFrames < 2 || measureBody.artworkPending || visibleBody.artworkPending) return;
            stop();
            popup.height = next;
            popup.open();
        }
    }
    Item {
        x: -10000
        opacity: 0
        visible: root.anchor !== null
        width: Math.max(1, popup.width - popup.leftPadding - popup.rightPadding)
        height: measureBody.implicitHeight
        CardPreviewBody {
            id: measureBody
            width: parent.width
            card: root.card
            imageSource: root.anchor ? root.sourceFor(root.card) : ""
        }
    }
    Basic.Popup {
        id: popup
        parent: root.overlayItem || root.parent
        width: Math.max(1, Math.min(340, parent.width - 24))
        padding: 12
        z: 50
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 } }
        exit: Transition {}
        onWidthChanged: if (visible) root.dismiss()
        Connections {
            target: popup.parent
            function onHeightChanged() { if (popup.visible) root.dismiss(); }
        }
        onAboutToShow: {
            var point = root.anchor.mapToItem(parent, 0, 0);
            var right = point.x + root.anchor.width + 18;
            var left = point.x - width - 18;
            if (right + width <= parent.width - 12) x = right;
            else if (left >= 12) x = left;
            else x = Math.max(12, Math.min(point.x, parent.width - width - 12));
            if (right + width <= parent.width - 12 || left >= 12)
                y = Math.max(12, Math.min(point.y, parent.height - height - 12));
            else if (point.y - height - 18 >= 12) y = point.y - height - 18;
            else y = point.y + root.anchor.height + 18;
        }
        background: Rectangle {
            radius: 10
            color: "#20252d"
            border.color: "#6a7c95"
        }
        contentItem: Item {
            implicitHeight: visibleBody.implicitHeight
            HoverHandler {
                margin: popup.padding
                onHoveredChanged: if (hovered) root.dismiss()
            }
            CardPreviewBody {
                id: visibleBody
                width: parent.width
                card: root.card
                imageSource: root.anchor ? root.sourceFor(root.card) : ""
            }
        }
    }
    onVisibleChanged: if (!visible) dismiss()
}
