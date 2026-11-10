import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A dialog drawn as a flat panel like the shortcuts overlay, rounded like the
// windows. Material's own draws its body with an elevation effect that
// doesn't always render, leaving the middle see-through.
Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    padding: 24
    topPadding: 8
    Material.roundedScale: theme.radius

    background: Rectangle {
        radius: theme.radius
        color: "#17171a"
        border.color: "#2a2a30"
    }

    header: Label {
        text: dialog.title
        visible: dialog.title.length > 0
        elide: Label.ElideRight
        font.pixelSize: 18
        font.weight: Font.DemiBold
        padding: 24
        bottomPadding: 8
    }

    footer: DialogButtonBox {
        background: null
        padding: 12
    }
}
