import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#1b1d21"
    implicitWidth: 470
    implicitHeight: 700

    readonly property color accent: "#4aa3ff"
    readonly property color okColor: "#3ecf7a"
    readonly property color warnColor: "#f0a340"
    readonly property color panel: "#24272c"
    readonly property color text1: "#e8eaed"
    readonly property color text2: "#9aa0a6"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            Layout.fillHeight: false
            radius: 10
            color: root.panel
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Label {
                    text: bridge.summaryText
                    color: root.text1
                    font.pixelSize: 13
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                }

                Button {
                    id: indexSelector
                    Layout.preferredWidth: 120
                    Layout.maximumWidth: 120
                    Layout.minimumWidth: 120
                    implicitHeight: 30
                    enabled: true
                    onClicked: {
                        indexPopup.opened ? indexPopup.close() : indexPopup.open();
                    }

                    contentItem: Text {
                        text: {
                            if (!catalog.activeIndexId)
                                return "Select index…";
                            return catalog.activeIndexName || catalog.activeIndexId;
                        }
                        color: root.text1
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Rectangle {
                        radius: 7
                        color: indexSelector.down ? "#111315" : indexSelector.hovered ? "#2c3033" : "#212427"
                        border.width: 1
                        border.color: indexSelector.hovered ? root.accent : "#F1F1F1"
                    }

                    Popup {
                        id: indexPopup
                        y: indexSelector.height + 4
                        x: indexSelector.width - width
                        width: 260
                        padding: 6
                        focus: true

                        onOpened: {
                            searchField.text = "";
                            searchField.forceActiveFocus();
                        }
                        onClosed: installedIndexes.setFilterFixedString("")

                        background: Rectangle {
                                radius: 8
                                color: Qt.lighter(root.panel, 1.25)
                                border.width: 1
                                border.color: root.panel
                            }

                        contentItem: ColumnLayout {
                            spacing: 6
                            TextField {
                                id: searchField
                                Layout.fillWidth: true
                                placeholderText: "Search…"
                                placeholderTextColor: root.text2
                                color: root.text1
                                onTextChanged: installedIndexes.setFilterFixedString(text)
                                background: Rectangle { color: Qt.lighter(root.panel, 1.25) }
                            }
                            ListView {
                                id: idxList
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(contentHeight, 260)
                                clip: true
                                model: installedIndexes
                                delegate: ItemDelegate {
                                    width: idxList.width
                                    contentItem: Text {
                                        text: model.name
                                        color: root.text1
                                        font.pixelSize: 14
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                    highlighted: model.idStr === catalog.activeIndexId
                                    onClicked: { catalog.useById(model.idStr); indexPopup.close() }
                                    background: Rectangle {
                                        radius: 5
                                        color: parent.highlighted ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.20)
                                             : parent.hovered ? Qt.lighter(root.panel, 1.5)
                                             : "transparent"
                                    }
                                }
                                ScrollBar.vertical: ScrollBar {}

                            }
                        }
                    }
                }

                Button {
                    text: "Export .txt"
                    implicitHeight: 30
                    onClicked: bridge.exportDeck()
                    background: Rectangle {
                        radius: 7
                        color: parent.down ? Qt.darker(root.accent, 1.3) : parent.hovered ? Qt.lighter(root.accent, 1.1) : root.accent
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.bold: true
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // ── selections (collapsible height, never eats the candidate area) ─
        Label {
            text: "Cards on image"
            color: root.text2
            font.pixelSize: 11
            Layout.fillHeight: false
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(110, Math.max(38, selView.count * 34 + 12))
            Layout.fillHeight: false
            radius: 10
            color: root.panel
            clip: true
            ListView {
                id: selView
                WheelHandler {
                    onWheel: event => {
                        selView.contentY = Math.max(0, Math.min(selView.contentHeight - selView.height, selView.contentY - event.angleDelta.y));
                        event.accepted = true;
                    }
                }
                anchors.fill: parent
                anchors.margins: 6
                model: selModel
                spacing: 3
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 31
                    radius: 6
                    color: index === bridge.currentIndex ? Qt.rgba(0.29, 0.64, 1, 0.22) : "transparent"
                    Behavior on color {
                        ColorAnimation {
                            duration: 110
                        }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: confirmed ? root.okColor : root.warnColor
                        }
                        Label {
                            text: number + ".  " + label
                            color: confirmed ? root.okColor : root.text1
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: bridge.selectCard(index)
                    }
                }
            }
        }

        // ── crop preview + confirmed state + quantity ──────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            Layout.fillHeight: false
            radius: 10
            color: root.panel
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 96
                    Layout.fillHeight: true
                    radius: 8
                    color: "#111318"
                    border.color: "#33373d"
                    border.width: 1
                    clip: true
                    MouseArea {
                        id: imageArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: bridge.openCompare()
                        Image {
                            id: cropImage
                            anchors.fill: parent
                            anchors.margins: 4
                            fillMode: Image.PreserveAspectFit
                            cache: true
                            source: bridge.currentIndex >= 0 ? "image://crop/current?rev=" + bridge.cropRev : ""
                        }

                        Rectangle {
                            width: parent.width
                            height: parent.height
                            anchors.centerIn: parent
                            color: Qt.rgba(0, 0, 0, 0.50)

                            Text {
                                text: "Click to expand"
                                color: "yellow"
                                font.pixelSize: 12
                                opacity: imageArea.containsMouse ? 1.0 : 0.0
                            }
                        }

                        Rectangle {
                            id: dropdownBar
                            width: parent.width
                            anchors.margins: 4
                            anchors.bottom: parent.bottom
                            height: 28
                            color: Qt.rgba(0, 0, 0, 0.50)
                            radius: 12

                            y: imageArea.containsMouse ? 0 : height
                            opacity: imageArea.containsMouse ? 1.0 : 0.0

                            Behavior on y {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutCubic
                                }
                            }
                            Behavior on opacity {
                                NumberAnimation {
                                    duration: 150
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.ArrowCursor
                            }

                            // Rotate image buttons
                            Row {
                                anchors.centerIn: parent
                                spacing: 20

                                Rectangle {
                                    width: 26
                                    height: 26
                                    radius: 13
                                    color: leftBtnArea.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : "transparent"

                                    Image {
                                        anchors.centerIn: parent
                                        source: "qrc:/icon/rotate_l.svg"
                                        sourceSize: Qt.size(20, 20)
                                    }

                                    MouseArea {
                                        id: leftBtnArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: bridge.rotateCardRequested(bridge.currentIndex, -90)
                                    }
                                }

                                Rectangle {
                                    width: 26
                                    height: 26
                                    radius: 13
                                    color: rightBtnArea.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : "transparent"

                                    Image {
                                        anchors.centerIn: parent
                                        source: "qrc:/icon/rotate_r.svg"
                                        sourceSize: Qt.size(20, 20)
                                    }

                                    MouseArea {
                                        id: rightBtnArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: bridge.rotateCardRequested(bridge.currentIndex, 90)
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: bridge.currentIndex < 0
                        text: "no card\nselected"
                        color: root.text2
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    Label {
                        text: "Your crop"
                        color: root.text2
                        font.pixelSize: 11
                    }
                    Label {
                        text: bridge.confirmedText
                        color: bridge.isConfirmed ? root.okColor : root.warnColor
                        font.pixelSize: 14
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        spacing: 5
                        Label {
                            text: "Copies"
                            color: root.text2
                            font.pixelSize: 11
                        }
                        Repeater {
                            model: 4
                            Rectangle {
                                width: 32
                                height: 28
                                radius: 6
                                opacity: bridge.isConfirmed ? 1 : 0.3
                                color: (index + 1) === bridge.quantity ? root.accent : "#2e3238"
                                Behavior on color {
                                    ColorAnimation {
                                        duration: 100
                                    }
                                }
                                Label {
                                    anchors.centerIn: parent
                                    text: "x" + (index + 1)
                                    color: (index + 1) === bridge.quantity ? "white" : root.text2
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: bridge.isConfirmed
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: bridge.setQuantity(index + 1)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── candidates: THE stretchy section (gets all remaining space) ────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Label {
                text: "Top matches — pick the correct card"
                color: root.text2
                font.pixelSize: 11
                Layout.fillWidth: true
            }
            Label {
                text: candView.count + " results"
                color: root.text2
                font.pixelSize: 11
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true              // <- only this expands
            Layout.minimumHeight: 220            // <- and it can never collapse
            radius: 10
            color: root.panel
            clip: true

            Label {
                anchors.centerIn: parent
                visible: candView.count === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: root.text2
                font.pixelSize: 12
                text: bridge.currentIndex < 0 ? "Select cards on the image, then press Detect." : "No results for this card yet — press Detect."
            }

            ListView {
                id: candView
                anchors.fill: parent
                anchors.margins: 8
                model: candModel
                spacing: 8
                clip: true
                reuseItems: true
                WheelHandler {
                    onWheel: event => {
                        candView.contentY = Math.max(0, Math.min(candView.contentHeight - candView.height, candView.contentY - event.angleDelta.y));
                        event.accepted = true;
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
                delegate: Rectangle {
                    id: delegateRoot
                    width: ListView.view.width
                    height: 126
                    radius: 10
                    color: isConfirmed ? Qt.rgba(0.24, 0.81, 0.48, 0.16) : "#2b2f35"
                    border.color: isConfirmed ? root.okColor : "transparent"
                    border.width: isConfirmed ? 2 : 0
                    Behavior on color {
                        ColorAnimation {
                            duration: 120
                        }
                    }

                    property string code: deckCode
                    property var _cardData: ({})
                    property bool isOfficial: _cardData.source === "official"
                    property bool imageLoaded: false
                    property bool fetchFailed: false
                    property string failReason: ""
                    property bool dataLoading: false

                    function refreshCardData() {
                        if (code)
                            _cardData = cardDatabase.cardDataFor(code);
                    }

                    onCodeChanged: {
                        _cardData = ({});
                        fetchFailed = false;
                        failReason = "";
                        imageLoaded = false;
                        if (code) {
                            dataLoading = true;
                            cardDatabase.ensureCardData(code);
                        }
                    }

                    Component.onCompleted: {
                        if (code) {
                            cardDatabase.ensureCardData(code);
                        }
                    }

                    Connections {
                        target: cardDatabase
                        function onCardReady(c) {
                            if (c === delegateRoot.code && !delegateRoot.imageLoaded) {
                                delegateRoot.dataLoading = false;
                                delegateRoot.fetchFailed = false;
                                delegateRoot.refreshCardData();
                                delegateRoot.imageLoaded = true;
                                candImg.rev++;
                            }
                        }
                        function onCardFetchFailed(c, reason) {
                            if (c === delegateRoot.code) {
                                delegateRoot.dataLoading = false;
                                delegateRoot.fetchFailed = true;
                                delegateRoot.failReason = reason;
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 80
                            Layout.fillHeight: true
                            radius: 6
                            color: "#0e1013"
                            clip: true
                            Image {
                                id: candImg
                                property int rev: 0
                                anchors.fill: parent
                                anchors.margins: 2
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                source: deckCode ? "image://cardcache/" + encodeURIComponent(deckCode) + "?r=" + candImg.rev : ""
                                cache: true
                                visible: !delegateRoot.fetchFailed && status === Image.Ready
                            }
                            BusyIndicator {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                running: !delegateRoot.fetchFailed && (delegateRoot.dataLoading || candImg.status === Image.Loading)
                                visible: running
                            }
                            Text {
                                anchors.centerIn: parent
                                width: parent.width - 6
                                visible: delegateRoot.fetchFailed
                                text: delegateRoot.failReason
                                color: "#9aa0a6"
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 3
                            RowLayout {
                                spacing: 6
                                Rectangle {
                                    width: 22
                                    height: 17
                                    radius: 4
                                    color: rank <= 3 ? root.accent : "#3a3f46"
                                    Label {
                                        anchors.centerIn: parent
                                        text: rank
                                        color: "white"
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                }
                                Label {
                                    text: deckCode
                                    color: root.text1
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Rectangle {
                                    visible: delegateRoot._cardData.source === "official"
                                    width: 18
                                    height: 18
                                    radius: 9
                                    color: "#5e4416"
                                    Label {
                                        anchors.centerIn: parent
                                        text: "!"
                                        color: "#fcd9a0"
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                    HoverHandler {
                                        id: warnHover
                                    }
                                    ToolTip {
                                        visible: warnHover.hovered
                                        text: "EncoreDecks doesn't have this card.\n" + "Using the official card API — translation data may be missing."
                                        delay: 200
                                    }
                                }
                            }
                            Label {
                                text: cardId
                                color: root.text2
                                font.pixelSize: 10
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                spacing: 8
                                Rectangle {
                                    width: 96
                                    height: 6
                                    radius: 3
                                    color: "#3a3f46"
                                    Rectangle {
                                        width: parent.width * Math.max(0, Math.min(1, score))
                                        height: parent.height
                                        radius: 3
                                        color: score > 0.7 ? root.okColor : score > 0.6 ? root.accent : root.warnColor
                                    }
                                }
                                Label {
                                    text: score.toFixed(4)
                                    color: root.text2
                                    font.pixelSize: 10
                                }
                            }
                            Item {
                                Layout.fillHeight: true
                            }
                        }

                        Button {
                            Layout.preferredWidth: 104
                            Layout.preferredHeight: 34
                            text: isConfirmed ? "✓ Confirmed" : "Confirm"
                            onClicked: bridge.confirm(index)
                            background: Rectangle {
                                radius: 7
                                color: isConfirmed ? root.okColor : parent.down ? Qt.darker(root.accent, 1.3) : parent.hovered ? Qt.lighter(root.accent, 1.15) : "#3a3f46"
                                Behavior on color {
                                    ColorAnimation {
                                        duration: 100
                                    }
                                }
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
