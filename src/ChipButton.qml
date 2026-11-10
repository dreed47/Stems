import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A small toggle that draws its own on state (mute, solo, the stem count),
// so it reads the same with every theme and renderer.
AbstractButton {
    id: chip
    property bool active: false
    property color activeColor: theme.accent
    property color activeText: theme.accentForeground

    implicitWidth: Math.max(36, label.implicitWidth + 22)
    implicitHeight: 32
    focusPolicy: Qt.NoFocus
    hoverEnabled: true

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: chip.active ? chip.activeColor : chip.hovered ? "#26262c" : "transparent"
        border.width: 1
        border.color: chip.active ? chip.activeColor : "#34343b"
        opacity: chip.enabled ? 1 : 0.5
    }

    contentItem: Label {
        id: label
        text: chip.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.weight: Font.DemiBold
        color: chip.active ? chip.activeText : "#c8c8d0"
    }
}
