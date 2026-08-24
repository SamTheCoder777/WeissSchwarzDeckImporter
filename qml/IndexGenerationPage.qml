import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#1e1e1e"
    implicitWidth: 820
    implicitHeight: 640

    readonly property color accent: "#4f6bff"
    readonly property color accentSoft: "#3a4bb8"
    readonly property color warnColor: "#e0a030"
    readonly property color okColor: "#3ac07a"
    readonly property color errColor: "#e0574f"
    readonly property color panel: "#1e2123"
    readonly property color card: "#24272a"
    readonly property color cardBorder: "#31353a"
    readonly property color ghostBorder: "#3d4147"
    readonly property color text1: "#f0f2f4"
    readonly property color text2: "#9aa0a6"
    readonly property color text3: "#676c72"

    property string chosenFolder: ""
    property bool building: models.busy
    property string nameError: ""
    property bool nameOk: false

    function validateName() {
        var n = nameField.text.trim();
        if (n.length === 0) {
            nameError = "";
            nameOk = false;
            return;
        }
        if (!config.isValidIndexName(n)) {
            nameError = "Invalid name (illegal characters).";
            nameOk = false;
            return;
        }
        if (config.indexNameExists(n)) {
            nameError = "An index with this name already exists.";
            nameOk = false;
            return;
        }
        nameError = "";
        nameOk = true;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Label {
            text: "Create index"
            color: root.text1
            font.pixelSize: 21
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 12
            color: root.panel
            border.width: 1
            border.color: root.cardBorder
            implicitHeight: form.implicitHeight + 40

            ColumnLayout {
                id: form
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: "Index name"
                        color: root.text2
                        font.pixelSize: 12
                    }
                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        implicitHeight: 38
                        placeholderText: "e.g. BanG_Dream"
                        color: root.text1
                        placeholderTextColor: root.text3
                        enabled: !root.building
                        background: Rectangle {
                            radius: 8
                            color: root.card
                            border.width: 1
                            border.color: root.nameError.length > 0 ? root.errColor : nameField.activeFocus ? root.accent : root.cardBorder
                        }
                        onTextChanged: root.validateName()
                    }
                    Label {
                        visible: root.nameError.length > 0
                        text: root.nameError
                        color: root.errColor
                        font.pixelSize: 11
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: "Image folder"
                        color: root.text2
                        font.pixelSize: 12
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField {
                            id: folderField
                            Layout.fillWidth: true
                            implicitHeight: 38
                            readOnly: true
                            placeholderText: "Choose a folder of card images…"
                            text: root.chosenFolder
                            color: root.text1
                            placeholderTextColor: root.text3
                            background: Rectangle {
                                radius: 8
                                color: root.card
                                border.width: 1
                                border.color: root.cardBorder
                            }
                        }
                        Button {
                            text: "Open folder"
                            enabled: !root.building
                            implicitHeight: 38
                            onClicked: {
                                var dir = config.pickFolder();
                                if (dir.length > 0)
                                    root.chosenFolder = dir;
                            }
                            background: Rectangle {
                                radius: 8
                                color: parent.down ? "#111315" : parent.hovered ? "#2c3033" : "#212427"
                                border.width: 1
                                border.color: parent.hovered ? "#454a51" : root.ghostBorder
                            }
                            contentItem: Text {
                                text: parent.text
                                color: root.text1
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Batch size"
                            color: root.text2
                            font.pixelSize: 12
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Loader {
                            id: valueLoader
                            property bool editing: false

                            sourceComponent: editing ? editorComponent : labelComponent

                            Component {
                                id: labelComponent
                                Label {
                                    text: Math.round(batchSlider.value)
                                    color: root.text1
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold

                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: !root.building
                                        cursorShape: Qt.IBeamCursor
                                        onClicked: valueLoader.editing = true
                                    }
                                }
                            }

                            Component {
                                id: editorComponent
                                TextField {
                                    id: inputField
                                    text: Math.round(batchSlider.value).toString()
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignRight
                                    selectByMouse: true

                                    validator: IntValidator {
                                        bottom: batchSlider.from
                                        top: batchSlider.to
                                    }

                                    Component.onCompleted: {
                                        forceActiveFocus();
                                        selectAll();
                                    }

                                    function commitValue() {
                                        var val = parseInt(text);
                                        if (!isNaN(val)) {
                                            val = Math.max(batchSlider.from, Math.min(batchSlider.to, val));
                                            batchSlider.value = val;
                                        }
                                        valueLoader.editing = false;
                                    }

                                    onAccepted: commitValue()
                                    onActiveFocusChanged: {
                                        if (!activeFocus)
                                            commitValue();
                                    }
                                }
                            }
                        }
                    }
                    Slider {
                        id: batchSlider
                        Layout.fillWidth: true
                        from: 1
                        to: 256
                        stepSize: 1
                        value: 1
                        enabled: !root.building
                    }
                }

                CheckBox {
                    text: "Disable Card Id Format Check"
                    checked: config.disableNameCheck
                    palette.windowText: root.text2

                    onCheckedChanged: {
                        config.toggleNameCheck();
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                id: createBtn
                text: root.building ? "Building…" : "Create index"
                enabled: models.loaded && !root.building && root.nameOk && root.chosenFolder.length > 0
                implicitHeight: 40
                Layout.preferredWidth: 160
                onClicked: {
                    logArea.text = "";
                    var savePath = config.getIndexInstallPath() + "/" + nameField.text.trim();
                    models.buildIndex(root.chosenFolder, savePath, Math.round(batchSlider.value));
                }
                background: Rectangle {
                    radius: 8
                    opacity: createBtn.enabled ? 1 : 0.4
                    color: createBtn.down ? root.accentSoft : createBtn.hovered ? Qt.lighter(root.accent, 1.1) : root.accent
                }
                contentItem: Text {
                    text: createBtn.text
                    color: "#f0f4ff"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                id: cancelBtn
                text: "Cancel"
                enabled: root.building
                implicitHeight: 40
                Layout.preferredWidth: 110
                onClicked: models.cancelIndexBuild()
                background: Rectangle {
                    radius: 8
                    opacity: cancelBtn.enabled ? 1 : 0.4
                    color: cancelBtn.down ? Qt.darker(root.warnColor, 1.2) : cancelBtn.hovered ? root.warnColor : "#3a2e18"
                    border.width: 1
                    border.color: root.warnColor
                }
                contentItem: Text {
                    text: cancelBtn.text
                    color: cancelBtn.enabled ? "#1a1300" : root.text3
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                id: openDirBtn
                text: "Open folder"
                implicitHeight: 38
                Layout.preferredWidth: 120
                onClicked: Qt.openUrlExternally(config.localFileToUrl(config.getIndexInstallPath()))
                background: Rectangle {
                    radius: 8
                    color: openDirBtn.down ? "#111315" : openDirBtn.hovered ? "#2c3033" : "#212427"
                    border.width: 1
                    border.color: openDirBtn.hovered ? "#454a51" : root.ghostBorder
                }
                contentItem: Text {
                    text: openDirBtn.text
                    color: root.text1
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Label {
                visible: root.building && progressBar.to > 0
                text: progressBar.value + " / " + progressBar.to
                color: root.text2
                font.pixelSize: 12
            }
        }

        ProgressBar {
            id: progressBar
            Layout.fillWidth: true
            from: 0
            to: 0
            value: 0
            visible: root.building
        }

        Label {
            text: "Log"
            color: root.text2
            font.pixelSize: 12
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            radius: 10
            color: "#141619"
            border.width: 1
            border.color: root.cardBorder
            clip: true

            ScrollView {
                anchors.fill: parent
                anchors.margins: 10
                TextArea {
                    id: logArea
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    color: root.text2
                    font.family: "monospace"
                    font.pixelSize: 12
                    background: null
                    text: ""
                    onTextChanged: cursorPosition = length
                }
            }
        }
    }

    Connections {
        target: models
        function onIndexLog(line) {
            logArea.text += line + "\n";
        }
        function onIndexProgress(done, total) {
            progressBar.to = total;
            progressBar.value = done;
        }
        function onIndexBuildFinished(ok) {
            logArea.text += (ok ? "✓ Index created successfully.\n" : "✗ Index build failed or cancelled.\n");
            root.validateName();
        }
    }
}
