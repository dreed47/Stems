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

    // Cents as the language writes numbers, signed: "+20 ¢", "−5 ¢", "0 ¢".
    function centsLabel(cents) {
        translator.language
        const number = Math.abs(cents).toLocaleString(Qt.locale(), "f", 0)
        return (cents > 0 ? "+" : cents < 0 ? "−" : "") + number + " ¢"
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
        onActivated: replaceDialog.visible ? replaceDialog.reject() : askDialog.visible ? askDialog.reject()
                     : settingsDialog.visible ? settingsDialog.close() : linkDialog.visible ? linkDialog.close() : win.helpVisible ? win.helpVisible = false
                     : backend.error.length > 0 ? backend.dismissError() : backend.cancel() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: mixer.loaded ? mixer.togglePlay() : backend.open() }
    Shortcut { sequences: ["O", StandardKey.Open]; context: Qt.ApplicationShortcut; onActivated: backend.open() }
    Shortcut { sequences: [StandardKey.Paste]; context: Qt.ApplicationShortcut; enabled: !linkDialog.visible; onActivated: backend.pasteLink() }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; enabled: !linkDialog.visible; onActivated: linkDialog.show() }
    Shortcut { sequences: ["E", "Ctrl+E"]; context: Qt.ApplicationShortcut; enabled: mixer.loaded; onActivated: backend.exportMix() }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; onActivated: backend.showFolder() }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; enabled: !backend.engineReady; onActivated: backend.installEngine() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: !backend.busy; onActivated: backend.switchStems(!backend.sixStems) }
    Shortcut { sequences: [",", "Ctrl+,"]; context: Qt.ApplicationShortcut; onActivated: settingsDialog.open() }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; onActivated: mixer.selected = mixer.selected - 1 }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; onActivated: mixer.selected = mixer.selected + 1 }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; onActivated: mixer.toggleMute(mixer.selected) }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; onActivated: mixer.toggleSolo(mixer.selected) }
    Shortcut { sequences: ["+", "="]; context: Qt.ApplicationShortcut; onActivated: mixer.nudgeVolume(mixer.selected, 0.1) }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; onActivated: mixer.nudgeVolume(mixer.selected, -0.1) }
    Shortcut { sequence: "0"; context: Qt.ApplicationShortcut; onActivated: mixer.resetMix() }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; onActivated: mixer.nudgeCents(10) }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; onActivated: mixer.nudgeCents(-10) }
    Shortcut { sequence: "}"; context: Qt.ApplicationShortcut; onActivated: mixer.nudgeCents(1) }
    Shortcut { sequence: "{"; context: Qt.ApplicationShortcut; onActivated: mixer.nudgeCents(-1) }
    Shortcut { sequence: "\\"; context: Qt.ApplicationShortcut; onActivated: mixer.setCents(0) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; onActivated: mixer.skip(-5) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; onActivated: mixer.skip(5) }
    Shortcut { sequence: "Shift+Left"; context: Qt.ApplicationShortcut; onActivated: mixer.skip(-30) }
    Shortcut { sequence: "Shift+Right"; context: Qt.ApplicationShortcut; onActivated: mixer.skip(30) }
    Shortcut { sequence: "Home"; context: Qt.ApplicationShortcut; onActivated: mixer.toStart() }
    Shortcut { sequence: "B"; context: Qt.ApplicationShortcut; autoRepeat: false; onActivated: mixer.tap() }
    Shortcut { sequence: "Shift+B"; context: Qt.ApplicationShortcut; onActivated: mixer.clearTap() }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; onActivated: mixer.toggleCountIn() }
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
                        text: backend.busy && backend.song.length > 0 ? backend.song : mixer.loaded ? mixer.title : qsTr("Stems")
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
                                onClicked: backend.switchStems(modelData)
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
                        text: qsTr("Link")
                        flat: true
                        onClicked: linkDialog.show()
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Split a song from a YouTube link (L)")
                    }
                    Button {
                        text: qsTr("Split again")
                        flat: true
                        visible: mixer.loaded && backend.source.length > 0
                        enabled: !backend.busy
                        onClicked: backend.resplit()
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Split this song again, into 4 or 6 stems")
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
                        text: "⚙"
                        font.pixelSize: 18
                        flat: true
                        onClicked: settingsDialog.open()
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Settings (,)")
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
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("or paste a YouTube link with Ctrl+V")
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
                        visible: backend.downloading
                        wrapMode: Text.WordWrap
                        text: backend.progress >= 0
                              ? qsTr("%1% downloaded. The song is split next.")
                                    .arg(Math.round(backend.progress * 100).toLocaleString(Qt.locale(), "f", 0))
                              : qsTr("Getting the audio with yt-dlp.")
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

                // The count-in, beat by beat, over the stems.
                Rectangle {
                    anchors.centerIn: parent
                    width: 160
                    height: 160
                    radius: width / 2
                    color: Qt.rgba(0.055, 0.055, 0.063, 0.85)
                    border.width: 3
                    border.color: theme.accent
                    visible: mixer.countBeat > 0
                    Label {
                        anchors.centerIn: parent
                        text: mixer.countBeat.toLocaleString(Qt.locale(), "f", 0)
                        font.pixelSize: 84
                        font.weight: Font.Bold
                        color: theme.accent
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
                    id: transport
                    // Narrow windows drop the labels and dividers, so the
                    // scrubber keeps its room.
                    readonly property bool roomy: width >= 900
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 20
                    spacing: roomy ? 14 : 8

                    // Back to the beginning: a bar and a triangle pointing left.
                    AbstractButton {
                        id: toStartButton
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        onClicked: mixer.toStart()
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Back to the start (Home)")
                        background: Rectangle {
                            radius: width / 2
                            color: toStartButton.hovered ? "#26262c" : "transparent"
                            border.width: 1
                            border.color: "#34343b"
                        }
                        contentItem: Canvas {
                            property color ink: "#e6e6ea"
                            onInkChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = ink
                                const cx = width / 2, cy = height / 2
                                ctx.fillRect(cx - 7, cy - 6, 2.5, 12)
                                ctx.beginPath()
                                ctx.moveTo(cx + 6, cy - 6)
                                ctx.lineTo(cx - 4, cy)
                                ctx.lineTo(cx + 6, cy + 6)
                                ctx.closePath()
                                ctx.fill()
                            }
                        }
                    }

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

                    ToolSeparator { visible: transport.roomy }

                    // Tempo, found in the drums or tapped, and the count-in
                    // that follows it.
                    Label {
                        Layout.preferredWidth: 72
                        horizontalAlignment: Text.AlignHCenter
                        text: mixer.bpm > 0 ? qsTr("%1 BPM").arg(Math.round(mixer.bpm).toLocaleString(Qt.locale(), "f", 0))
                                            : qsTr("– BPM")
                        color: mixer.tempoTapped ? theme.accent : mixer.bpm > 0 ? "#e6e6ea" : win.dim
                        font.features: { "tnum": 1 }
                        font.pixelSize: 15
                        font.weight: mixer.tempoTapped ? Font.DemiBold : Font.Normal
                        TapHandler { onDoubleTapped: mixer.clearTap() }
                        HoverHandler { id: tempoHover }
                        ToolTip.visible: tempoHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: mixer.tempoTapped ? qsTr("Tapped tempo. Double-click or press Shift+B to go back to the drums' tempo.")
                                    : mixer.bpm > 0 ? qsTr("Tempo found in the drums. Tap along to change it.")
                                    : qsTr("No steady tempo found. Tap along to set one.")
                    }
                    ChipButton {
                        text: qsTr("Tap")
                        // On press, not release: that's when the beat is.
                        onPressed: mixer.tap()
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Tap along with the beat to set the tempo (B)")
                    }
                    ChipButton {
                        text: qsTr("Count-in")
                        active: mixer.countIn
                        onClicked: mixer.toggleCountIn()
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: mixer.bpm > 0 ? qsTr("Click %n beats before the song plays (C)", "", mixer.countInBeats)
                                                    : qsTr("Counting in needs a tempo: tap one first (C)")
                    }

                    ToolSeparator { visible: transport.roomy }

                    // Pitch, in cents, up to a half step either way.
                    Label {
                        visible: transport.roomy
                        text: qsTr("Pitch")
                        color: win.dim
                    }
                    ChipButton {
                        text: "−"
                        enabled: mixer.cents > -100
                        autoRepeat: true
                        onClicked: mixer.nudgeCents(-10)
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Lower the pitch 10 cents ([, or { for 1 cent)")
                    }
                    Label {
                        Layout.preferredWidth: 64
                        horizontalAlignment: Text.AlignHCenter
                        text: win.centsLabel(mixer.cents)
                        color: mixer.cents !== 0 ? theme.accent : "#e6e6ea"
                        font.features: { "tnum": 1 }
                        font.pixelSize: 15
                        font.weight: mixer.cents !== 0 ? Font.DemiBold : Font.Normal
                        TapHandler { onDoubleTapped: mixer.setCents(0) }
                        HoverHandler { id: centsHover }
                        ToolTip.visible: centsHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("100 cents is a half step. Double-click or press \\ to reset.")
                    }
                    ChipButton {
                        text: "+"
                        enabled: mixer.cents < 100
                        autoRepeat: true
                        onClicked: mixer.nudgeCents(10)
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Raise the pitch 10 cents (], or } for 1 cent)")
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

    // A link to download and split. It starts with the clipboard's link, if
    // there is one, so Enter is often all it takes.
    PanelDialog {
        id: linkDialog
        width: Math.min(win.width - 48, 560)
        title: qsTr("Split a song from a link")
        standardButtons: Dialog.Cancel | Dialog.Ok

        function show() {
            linkField.text = backend.clipboardLink()
            open()
        }

        onOpened: {
            linkField.forceActiveFocus()
            linkField.selectAll()
        }
        onAccepted: backend.openLink(linkField.text)

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            TextField {
                id: linkField
                Layout.fillWidth: true
                placeholderText: qsTr("YouTube link")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: linkDialog.accept()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: win.dim
                text: qsTr("The audio is saved in %1, then split like any other song.").arg(backend.stemsRoot + "/Downloads")
            }
        }
    }

    // Another launch asked to open a song while a job runs.
    Connections {
        target: backend
        function onPendingChanged() {
            if (backend.pendingSong.length > 0)
                replaceDialog.open()
            else
                replaceDialog.close()
        }
    }

    PanelDialog {
        id: replaceDialog
        objectName: "replaceDialog"
        width: Math.min(win.width - 48, 560)
        title: qsTr("Stop the current job and open %1?").arg(backend.pendingSong)
        Label {
            width: parent.width
            wrapMode: Label.Wrap
            text: qsTr("The work so far will be thrown away.")
            color: win.dim
        }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backend.replaceWithPending()
        onRejected: backend.keepCurrent()
    }

    // A song that needs a choice first: it has stems already, or the other
    // stem count was picked and it has no split for that yet.
    Connections {
        target: backend
        function onAskChanged() {
            if (backend.askKind !== 0)
                askDialog.open()
            else
                askDialog.close()
        }
    }

    PanelDialog {
        id: askDialog
        objectName: "askDialog"
        readonly property bool choosing: backend.askKind === 1
        readonly property bool has: backend.askHasStems
        width: Math.min(win.width - 48, 560)
        title: choosing ? qsTr("Open %1").arg(backend.askSong)
                        : backend.sixStems ? qsTr("Split %1 into 6 stems?").arg(backend.askSong)
                                           : qsTr("Split %1 into 4 stems?").arg(backend.askSong)
        ColumnLayout {
            width: parent.width
            spacing: 12
            Row {
                visible: askDialog.choosing
                spacing: 6
                Repeater {
                    model: [false, true]
                    delegate: ChipButton {
                        required property bool modelData
                        active: backend.sixStems === modelData
                        text: modelData ? qsTr("6 stems") : qsTr("4 stems")
                        onClicked: backend.sixStems = modelData
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Label.Wrap
                color: win.dim
                text: !askDialog.choosing ? qsTr("This song hasn't been split that way yet.")
                    : askDialog.has ? qsTr("Open the stems you have, or split it again. Splitting again replaces them.")
                                    : qsTr("Vocals, drums, bass and other, or add guitar and piano with 6 stems.")
            }
        }
        footer: DialogButtonBox {
            background: null
            padding: 12
            Button {
                text: qsTr("Split again")
                flat: true
                visible: askDialog.has
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                text: askDialog.has ? qsTr("Open") : qsTr("Split")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        // Set on the dialog: it overwrites its footer's own standardButtons.
        standardButtons: Dialog.Cancel
        onAccepted: has ? backend.openExisting() : backend.splitAgain()
        onDiscarded: {
            backend.splitAgain()
            close()
        }
        // Esc, Cancel or a click outside all leave things as they were.
        onClosed: if (backend.askKind !== 0) backend.dismissAsk()
    }

    PanelDialog {
        id: settingsDialog
        objectName: "settingsDialog"
        width: Math.min(win.width - 48, 720)
        title: qsTr("Settings")
        standardButtons: Dialog.Close
        onAboutToShow: backend.refreshDependencies()

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: qsTr("Stems folder")
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Label.Wrap
                color: win.dim
                text: qsTr("New splits and downloads go here. Songs split before stay where they are.")
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: backend.stemsRoot
                    elide: Label.ElideMiddle
                }
                Button {
                    text: qsTr("Change…")
                    flat: true
                    onClicked: backend.chooseStemsRoot()
                }
                Button {
                    text: qsTr("Default")
                    flat: true
                    visible: backend.customStemsRoot
                    onClicked: backend.resetStemsRoot()
                }
            }

            Label {
                Layout.topMargin: 12
                text: qsTr("Count-in")
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Label.Wrap
                color: win.dim
                text: qsTr("With Count-in on (C), playing starts with clicks at the song's tempo, and the song comes in on the beat.")
            }
            Row {
                spacing: 6
                Repeater {
                    model: [2, 4, 8]
                    delegate: ChipButton {
                        required property int modelData
                        active: mixer.countInBeats === modelData
                        text: qsTr("%n beats", "", modelData)
                        onClicked: mixer.countInBeats = modelData
                    }
                }
            }

            Label {
                Layout.topMargin: 12
                text: qsTr("Dependencies")
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Label.Wrap
                color: win.dim
                text: qsTr("Programs Stems runs. Point at a name to see which copy it uses.")
            }
            Repeater {
                model: backend.dependencies
                delegate: RowLayout {
                    required property var modelData
                    readonly property bool found: modelData.path.length > 0
                    Layout.fillWidth: true
                    spacing: 14
                    Label {
                        Layout.preferredWidth: 16
                        text: found ? "✔" : modelData.required ? "✖" : "–"
                        color: found ? theme.accent : modelData.required ? "#ff6b6b" : win.dim
                    }
                    Label {
                        Layout.preferredWidth: 72
                        text: modelData.name
                        font.weight: Font.DemiBold
                        ToolTip.visible: found && nameHover.hovered
                        ToolTip.text: modelData.path
                        HoverHandler { id: nameHover }
                    }
                    Label {
                        Layout.preferredWidth: 96
                        elide: Label.ElideRight
                        text: !found ? qsTr("not installed") : modelData.version.length > 0 ? modelData.version : "…"
                        color: found ? "white" : win.dim
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Label.ElideRight
                        color: win.dim
                        text: modelData.required ? modelData.purpose : qsTr("%1 (optional)").arg(modelData.purpose)
                    }
                    Button {
                        visible: !found && modelData.install.length === 0
                        text: qsTr("Install")
                        flat: true
                        enabled: !backend.busy
                        onClicked: backend.installEngine()
                    }
                    Label {
                        visible: !found && modelData.install.length > 0
                        text: modelData.install
                        font.family: "monospace"
                        color: win.dim
                    }
                }
            }
        }
    }

    PanelDialog {
        id: quitDialog
        objectName: "quitDialog"
        modal: true
        title: backend.installing ? qsTr("Stop installing and quit?")
             : backend.downloading ? qsTr("Stop downloading and quit?") : qsTr("Stop splitting and quit?")
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
