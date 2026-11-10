import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: overlay
    signal closed()
    color: Qt.rgba(0, 0, 0, 0.72)

    readonly property var keys: [
        ["Space", qsTr("Play or pause (open a song when none is loaded)")],
        ["O", qsTr("Open a song or a stems folder")],
        ["1 – 6", qsTr("Mute a stem")],
        ["Ctrl+1 – 6", qsTr("Solo a stem")],
        ["↑ ↓  /  K J", qsTr("Select a stem")],
        ["M  /  S", qsTr("Mute or solo the selected stem")],
        ["+  /  −", qsTr("Selected stem louder or quieter")],
        ["0", qsTr("Reset the mix")],
        ["← →", qsTr("Back or forward 5 seconds")],
        ["Shift+← →", qsTr("Back or forward 30 seconds")],
        ["Home", qsTr("Back to the start")],
        ["E", qsTr("Export what's audible as a WAV")],
        ["T", qsTr("Switch between 4 and 6 stems")],
        ["F", qsTr("Show the stems folder")],
        ["Ctrl+Z  /  Ctrl+Shift+Z", qsTr("Undo or redo a mix change")],
        ["Esc", qsTr("Cancel the split")],
        ["?", qsTr("Show or hide these shortcuts")],
        ["Q", qsTr("Quit")]
    ]

    MouseArea { anchors.fill: parent; onClicked: overlay.closed() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, grid.implicitWidth + 56)
        height: Math.min(parent.height - 48, column.implicitHeight + 48)
        radius: theme.radius
        color: "#17171a"
        border.color: "#2a2a30"

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            Label {
                text: qsTr("Keyboard shortcuts")
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            GridLayout {
                id: grid
                columns: 2
                columnSpacing: 28
                rowSpacing: 7
                Repeater {
                    model: overlay.keys
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        text: modelData[0]
                        color: theme.accent
                        font.weight: Font.DemiBold
                    }
                }
                Repeater {
                    model: overlay.keys
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        text: modelData[1]
                    }
                }
            }
        }
    }
}
