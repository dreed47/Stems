import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1100
    height: 720
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: mixer.title.length > 0 ? mixer.title + " — Stems" : "Stems"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color panel: "#17171a"
    readonly property color line: "#26262b"
    readonly property color dim: "#8a8a93"
    property bool helpVisible: false

    function stemLabel(name) {
        switch (name) {
        case "vocals": return qsTr("Vocals")
        case "drums": return qsTr("Drums")
        case "bass": return qsTr("Bass")
        case "guitar": return qsTr("Guitar")
        case "piano": return qsTr("Piano")
        case "other": return qsTr("Other")
        }
        return name
    }

    function clock(seconds) {
        translator.language
        const total = Math.max(0, Math.floor(seconds))
        const s = total % 60
        return Number(Math.floor(total / 60)).toLocaleString(Qt.locale(), "f", 0) + ":"
            + (s < 10 ? Number(0).toLocaleString(Qt.locale(), "f", 0) : "")
            + Number(s).toLocaleString(Qt.locale(), "f", 0)
    }

    function quit() {
        if (backend.busy)
            quitDialog.open()
        else
            Qt.quit()
    }

    onClosing: (close) => {
        if (backend.busy) {
            close.accepted = false
            quitDialog.open()
        }
    }

    // Every key works from anywhere in the window.
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.quit() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut
        onActivated: win.helpVisible ? win.helpVisible = false : backend.error.length > 0 ? backend.dismissError() : backend.cancel() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: mixer.loaded ? mixer.togglePlay() : backend.open() }
    Shortcut { sequences: ["O", StandardKey.Open]; context: Qt.ApplicationShortcut; onActivated: backend.open() }
    Shortcut { sequences: ["E", "Ctrl+E"]; context: Qt.ApplicationShortcut; enabled: mixer.loaded; onActivated: backend.exportMix() }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; onActivated: backend.showFolder() }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; enabled: !backend.engineReady; onActivated: backend.installEngine() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: !backend.busy; onActivated: backend.sixStems = !backend.sixStems }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; onActivated: mixer.selected = mixer.selected - 1 }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; onActivated: mixer.selected = mixer.selected + 1 }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; onActivated: mixer.toggleMute(mixer.selected) }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; onActivated: mixer.toggleSolo(mixer.selected) }
    Shortcut { sequences: ["+", "="]; context: Qt.ApplicationShortcut; onActivated: mixer.nudgeVolume(mixer.selected, 0.1) }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; onActivated: mixer.nudgeVolume(mixer.selected, -0.1) }
    Shortcut { sequence: "0"; context: Qt.ApplicationShortcut; onActivated: mixer.resetMix() }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; onActivated: mixer.skip(-5) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; onActivated: mixer.skip(5) }
    Shortcut { sequence: "Shift+Left"; context: Qt.ApplicationShortcut; onActivated: mixer.skip(-30) }
    Shortcut { sequence: "Shift+Right"; context: Qt.ApplicationShortcut; onActivated: mixer.skip(30) }
    Shortcut { sequence: "Home"; context: Qt.ApplicationShortcut; onActivated: mixer.seek(0) }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: mixer.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; onActivated: mixer.redo() }
    Repeater {
        model: 6
        Item {
            Shortcut { sequence: String(index + 1); context: Qt.ApplicationShortcut; onActivated: mixer.toggleMute(index) }
            Shortcut { sequence: "Ctrl+" + (index + 1); context: Qt.ApplicationShortcut; onActivated: mixer.toggleSolo(index) }
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: root
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        DropArea {
            id: drop
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: (event) => { if (event.urls.length > 0) backend.openUrl(event.urls[0]) }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Header: the song, the model choice, and the file actions.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                color: win.panel

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 12
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: mixer.loaded ? mixer.title : backend.splitting ? backend.song : qsTr("Stems")
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Label {
                        text: qsTr("Split into")
                        color: win.dim
                    }
                    Row {
                        spacing: 6
                        enabled: !backend.busy
                        Repeater {
                            model: [false, true]
                            delegate: ChipButton {
                                required property bool modelData
                                active: backend.sixStems === modelData
                                text: modelData ? qsTr("6 stems") : qsTr("4 stems")
                                onClicked: backend.sixStems = modelData
                                ToolTip.visible: hovered
                                ToolTip.delay: 600
                                ToolTip.text: modelData ? qsTr("Vocals, drums, bass, guitar, piano, other")
                                                        : qsTr("Vocals, drums, bass, other")
                            }
                        }
                    }

                    ToolSeparator {}

                    Button {
                        text: qsTr("Open")
                        flat: true
                        onClicked: backend.open()
                    }
                    Button {
                        text: qsTr("Export mix")
                        flat: true
                        enabled: mixer.loaded
                        onClicked: backend.exportMix()
                    }
                    Button {
                        text: qsTr("Folder")
                        flat: true
                        onClicked: backend.showFolder()
                    }
                    Button {
                        text: "?"
                        flat: true
                        onClicked: win.helpVisible = true
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }
            }

            // The middle: whatever the app is doing right now.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Nothing open yet.
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 64, 520)
                    spacing: 18
                    visible: !mixer.loaded && !mixer.loading && !backend.busy

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 200
                        radius: theme.radius
                        color: drop.containsDrag ? Qt.rgba(Material.accent.r, Material.accent.g, Material.accent.b, 0.08) : "transparent"
                        border.width: 2
                        border.color: drop.containsDrag ? theme.accent : win.line

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 10
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("Drop a song here")
                                font.pixelSize: 22
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("or press O to choose one")
                                color: win.dim
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.open()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: win.dim
                        text: qsTr("Stems are saved in %1, so a song only has to be split once.").arg(backend.stemsRoot)
                    }

                    // Without Demucs there's nothing to split with; say so and offer to fix it.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: engineColumn.implicitHeight + 40
                        visible: !backend.engineReady
                        radius: theme.radius
                        color: win.panel
                        border.color: win.line

                        ColumnLayout {
                            id: engineColumn
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Demucs isn't installed")
                                font.weight: Font.DemiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: win.dim
                                text: qsTr("Stems uses Demucs to split songs. It installs into your home folder with uv and runs on the CPU.")
                            }
                            ChipButton {
                                active: true
                                implicitHeight: 36
                                text: qsTr("Install Demucs (I)")
                                onClicked: backend.installEngine()
                            }
                        }
                    }
                }

                // Splitting, installing, or decoding.
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 64, 520)
                    spacing: 14
                    visible: backend.busy || mixer.loading

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        font.pixelSize: 18
                        text: mixer.loading ? qsTr("Loading the stems…") : backend.status
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        indeterminate: mixer.loading || backend.progress < 0
                        value: Math.max(0, backend.progress)
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        color: win.dim
                        visible: backend.splitting
                        wrapMode: Text.WordWrap
                        text: backend.progress >= 0
                              ? qsTr("%1% — splitting takes about as long as the song on this computer.")
                                    .arg(Math.round(backend.progress * 100).toLocaleString(Qt.locale(), "f", 0))
                              : qsTr("The first split downloads the model, about 80 MB.")
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        flat: true
                        visible: backend.busy
                        text: qsTr("Cancel (Esc)")
                        onClicked: backend.cancel()
                    }
                }

                // The mixer: one row per stem.
                ListView {
                    id: tracks
                    anchors.fill: parent
                    anchors.margins: 16
                    visible: mixer.loaded
                    model: mixer
                    spacing: 8
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    currentIndex: mixer.selected
                    delegate: TrackRow {
                        width: tracks.width
                        height: Math.max(72, Math.min(130, (tracks.height - 8 * (tracks.count - 1)) / Math.max(1, tracks.count)))
                        label: win.stemLabel(name)
                    }
                }
            }

            // Transport.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                color: win.panel
                visible: mixer.loaded
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: win.line }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 20
                    spacing: 14

                    AbstractButton {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        focusPolicy: Qt.NoFocus
                        onClicked: mixer.togglePlay()
                        // A true circle, so it keeps its radius whatever the theme.
                        background: Rectangle { radius: width / 2; color: theme.accent }
                        contentItem: Canvas {
                            id: playIcon
                            property bool playing: mixer.playing
                            property color ink: theme.accentForeground
                            onPlayingChanged: requestPaint()
                            onInkChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = ink
                                const cx = width / 2, cy = height / 2
                                if (playing) {
                                    ctx.fillRect(cx - 7, cy - 8, 5, 16)
                                    ctx.fillRect(cx + 2, cy - 8, 5, 16)
                                } else {
                                    ctx.beginPath()
                                    ctx.moveTo(cx - 5, cy - 9)
                                    ctx.lineTo(cx + 9, cy)
                                    ctx.lineTo(cx - 5, cy + 9)
                                    ctx.closePath()
                                    ctx.fill()
                                }
                            }
                        }
                    }

                    Label {
                        text: win.clock(mixer.position)
                        font.features: { "tnum": 1 }
                        font.pixelSize: 16
                    }

                    Slider {
                        id: scrubber
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(0.01, mixer.duration)
                        value: pressed ? value : mixer.position
                        onMoved: mixer.seek(value)
                    }

                    Label {
                        text: win.clock(mixer.duration)
                        color: win.dim
                        font.features: { "tnum": 1 }
                        font.pixelSize: 16
                    }

                    Label {
                        visible: mixer.anySolo
                        text: qsTr("Solo")
                        font.weight: Font.DemiBold
                        color: theme.accent
                    }
                }
            }
        }

        // Errors stay until dismissed; they say what's wrong rather than guess.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: mixer.loaded ? 80 : 20
            width: Math.min(parent.width - 40, errorRow.implicitWidth + 32)
            height: errorRow.implicitHeight + 20
            radius: theme.radius
            visible: backend.error.length > 0
            color: "#3a1d1f"
            border.color: "#6e2a2e"

            RowLayout {
                id: errorRow
                anchors.fill: parent
                anchors.margins: 10
                anchors.leftMargin: 16
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    text: backend.error
                    wrapMode: Text.WordWrap
                    color: "#ffd9d9"
                }
                Button {
                    flat: true
                    text: qsTr("Dismiss")
                    onClicked: backend.dismissError()
                }
            }
        }

        // Short confirmations fade on their own.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 76
            width: noticeLabel.implicitWidth + 32
            height: noticeLabel.implicitHeight + 16
            radius: theme.radius
            color: theme.accent
            opacity: backend.notice.length > 0 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            Label {
                id: noticeLabel
                anchors.centerIn: parent
                text: backend.notice
                color: theme.accentForeground
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onClosed: win.helpVisible = false
        }
    }

    Dialog {
        id: quitDialog
        anchors.centerIn: parent
        modal: true
        title: backend.installing ? qsTr("Stop installing and quit?") : qsTr("Stop splitting and quit?")
        Label {
            text: qsTr("The work so far will be thrown away.")
            color: win.dim
        }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            backend.cancel()
            Qt.quit()
        }
    }
}
