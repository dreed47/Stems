---
name: stems
description: Split a song into instrument stems (vocals, drums, bass, other; or 6 stems with guitar and piano) with the `stems` command, and render mixes with stems muted or soloed — e.g. a karaoke (no vocals) or drums-only track. Use when asked to separate, isolate, remove or extract vocals or instruments from an audio file.
---

# stems

`stems` splits audio with Demucs and writes one folder per song under
`~/Music/Stems/<song>/` holding `vocals.wav`, `drums.wav`, `bass.wav`,
`other.wav` (plus `guitar.wav` and `piano.wav` with `--six`). The original
file is never changed. Every command takes `--json`, prints one JSON object
on stdout, exits 0 or 1, and writes progress and errors to stderr.

```sh
stems check --json                      # is Demucs installed? where do stems go?
stems split song.mp3 --json             # -> {"ok":true,"folder":...,"stems":[{"name","path"}]}
stems split song.mp3 --six --json       # 6 stems, into "<song> (6 stems)"
stems mix "<folder>" --mute vocals -o karaoke.wav --json
stems mix "<folder>" --solo drums,bass -o rhythm.wav --json
stems open song.mp3                     # the GUI, to listen and solo/mute by hand
```

- Splitting runs on the CPU at roughly the song's own length; tell the user
  it will take a few minutes. A song already split returns at once.
- If `check` fails because Demucs is missing, the error shows the `uv`
  command that installs it; ask before running it (about 1 GB download).
- `--solo` wins over `--mute`, like on a mixing desk.
