import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One stem: its controls on the left, its waveform on the right. Clicking
// or dragging on the waveform seeks.
Rectangle {
    id: row

    required property int index
    required property string name
    required property bool muted
    required property bool soloed
    required property real volume
    required property bool audible
    required property var peaks
    property string label: name

    readonly property bool selected: mixer.selected === index
    readonly property color wave: audible ? theme.accent : "#4a4a52"

    Material.roundedScale: theme.radius
    radius: theme.radius
    color: selected ? "#1d1d22" : "#151518"
    border.width: 1
    border.color: selected ? Qt.rgba(Material.accent.r, Material.accent.g, Material.accent.b, 0.55) : "#222227"

    TapHandler { onTapped: mixer.selected = row.index }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        anchors.leftMargin: 14
        spacing: 14

        ColumnLayout {
            Layout.preferredWidth: 230
            Layout.maximumWidth: 230
            Layout.fillHeight: true
            spacing: 4

            RowLayout {
                spacing: 8
                Label {
                    text: (row.index + 1).toLocaleString(Qt.locale(), "f", 0)
                    color: "#6c6c75"
                    font.features: { "tnum": 1 }
                }
                Label {
                    Layout.fillWidth: true
                    text: row.label
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    opacity: row.audible ? 1 : 0.5
                }
                // M and S stay as they are on every mixing desk.
                ChipButton {
                    text: "M"
                    active: row.muted
                    activeColor: "#c2543f"
                    activeText: "white"
                    onClicked: { mixer.selected = row.index; mixer.toggleMute(row.index) }
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Mute (%1)").arg(row.index + 1)
                }
                ChipButton {
                    text: "S"
                    active: row.soloed
                    onClicked: { mixer.selected = row.index; mixer.toggleSolo(row.index) }
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Solo (Ctrl+%1)").arg(row.index + 1)
                }
            }

            RowLayout {
                spacing: 6
                Slider {
                    id: volumeSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    stepSize: 0.01
                    value: row.volume
                    // Only a finished drag goes on the undo stack, as one step.
                    live: false
                    onMoved: mixer.setVolume(row.index, value)
                    onPressedChanged: if (pressed) mixer.selected = row.index
                }
                Label {
                    Layout.preferredWidth: 38
                    horizontalAlignment: Text.AlignRight
                    text: Math.round((volumeSlider.pressed ? volumeSlider.valueAt(volumeSlider.visualPosition) : row.volume) * 100)
                              .toLocaleString(Qt.locale(), "f", 0) + "%"
                    color: "#8a8a93"
                    font.features: { "tnum": 1 }
                }
            }
        }

        Item {
            id: waveArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Canvas {
                id: canvas
                anchors.fill: parent
                property var peaks: row.peaks
                property color ink: row.wave
                onPeaksChanged: requestPaint()
                onInkChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    if (!peaks || peaks.length === 0)
                        return
                    ctx.fillStyle = ink
                    const mid = height / 2
                    const step = 2
                    const bins = peaks.length
                    for (let x = 0; x < width; x += step) {
                        // The loudest bin under each pixel column, so peaks don't vanish.
                        const a = Math.floor(x / width * bins)
                        const b = Math.max(a + 1, Math.floor((x + step) / width * bins))
                        let p = 0
                        for (let i = a; i < b && i < bins; ++i)
                            p = Math.max(p, peaks[i])
                        const h = Math.max(1, p * (height - 4) / 2)
                        ctx.fillRect(x, mid - h, step - 0.6, h * 2)
                    }
                }
            }

            // What's still to come is dimmed; the playhead sits on the line.
            readonly property real playX: mixer.duration > 0 ? width * mixer.position / mixer.duration : 0
            Rectangle {
                x: waveArea.playX
                width: waveArea.width - x
                height: parent.height
                color: row.color
                opacity: 0.45
            }
            Rectangle {
                x: waveArea.playX - 1
                width: 2
                height: parent.height
                color: "#f2f2f5"
                opacity: 0.85
            }

            MouseArea {
                anchors.fill: parent
                preventStealing: true
                cursorShape: Qt.IBeamCursor
                function seekTo(px) { mixer.seek(Math.max(0, Math.min(1, px / width)) * mixer.duration) }
                onPressed: (mouse) => { mixer.selected = row.index; seekTo(mouse.x) }
                onPositionChanged: (mouse) => { if (pressed) seekTo(mouse.x) }
            }
        }
    }
}
