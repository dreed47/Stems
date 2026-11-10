#include "mixer.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QMediaDevices>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <cmath>

#include "pitchshifter.h"

namespace {
constexpr int BytesPerFrame = Mixer::Channels * int(sizeof(qint16));
// Mute, solo and volume changes fade over this many frames (about 12 ms).
constexpr int RampFrames = 512;
constexpr int UndoLimit = 200;
const QString SettingsFile = QStringLiteral("mix.json");
}

// The audio sink pulls the mix from here, possibly on its own thread, so it
// only touches the immutable PCM, the atomics, and the gains under their lock.
// With the pitch moved off zero the mix runs through a PitchShifter first;
// at zero it's bypassed, so plain playback stays bit-exact.
class Mixer::Stream : public QIODevice {
public:
    explicit Stream(Mixer *mixer) : m_mixer(mixer), m_shifter(SampleRate, Channels) {}
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return std::numeric_limits<int>::max(); }

    void resetGains() {
        QMutexLocker lock(&m_mixer->m_gainLock);
        m_gains = m_mixer->m_targetGains;
    }

protected:
    qint64 writeData(const char *, qint64) override { return -1; }

    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 frames = maxSize / BytesPerFrame;
        if (frames <= 0)
            return 0;
        std::vector<float> targets;
        {
            QMutexLocker lock(&m_mixer->m_gainLock);
            targets = m_mixer->m_targetGains;
        }
        if (m_gains.size() != targets.size())
            m_gains = targets;

        auto *out = reinterpret_cast<qint16 *>(data);
        const int cents = m_mixer->m_liveCents.load();
        const bool restart = m_mixer->m_restartShift.exchange(false);
        qint64 from = m_mixer->m_frame.load();

        // Leaving the shifter: what it still held was never heard, so
        // playback picks up from there rather than skipping it.
        if (cents == 0 && m_shifting) {
            m_shifting = false;
            const qint64 back = qMax<qint64>(0, from - m_shifter.lag());
            if (!restart && m_mixer->m_frame.compare_exchange_strong(from, back))
                from = back;
            m_mixer->m_shiftLag = 0;
        }

        qint64 cursor = from;
        std::vector<float> rampFrom = m_gains;
        auto source = [&](qint16 *buffer, qint64 wanted) {
            const qint64 count = qBound<qint64>(0, m_mixer->m_frames - cursor, wanted);
            m_mixer->mixInto(buffer, cursor, count, rampFrom, targets);
            rampFrom = targets;
            cursor += count;
            return count;
        };

        qint64 written;
        if (cents != 0) {
            if (!m_shifting || restart)
                m_shifter.restart(cents);
            else
                m_shifter.setCents(cents);
            m_shifting = true;
            written = m_shifter.render(out, frames, source);
            m_mixer->m_shiftLag = m_shifter.lag();
        } else {
            written = source(out, frames);
        }
        std::fill(out + written * Channels, out + frames * Channels, qint16(0));
        m_gains = targets;

        // A seek from the UI thread in the meantime wins over our advance.
        m_mixer->m_frame.compare_exchange_strong(from, cursor);
        if (written < frames)
            m_mixer->m_ended = true;
        return frames * BytesPerFrame;
    }

private:
    Mixer *m_mixer;
    std::vector<float> m_gains;
    PitchShifter m_shifter;
    bool m_shifting = false;
};

Mixer::Mixer(QObject *parent) : QAbstractListModel(parent) {
    m_ticker.setInterval(33);
    connect(&m_ticker, &QTimer::timeout, this, &Mixer::updatePosition);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
        const QString problem = saveSettings();
        if (!problem.isEmpty())
            emit saveFailed(problem);
    });
}

Mixer::~Mixer() {
    flushSave();
    for (auto *decoder : m_decoders) {
        decoder->disconnect(this);
        decoder->kill();
        decoder->waitForFinished(1000);
    }
    if (m_sink)
        m_sink->stop();
}

int Mixer::rowCount(const QModelIndex &parent) const {
    return parent.isValid() || m_loading ? 0 : int(m_tracks.size());
}

QVariant Mixer::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= int(m_tracks.size()))
        return {};
    const Track &track = m_tracks[index.row()];
    switch (role) {
    case NameRole: return track.name;
    case MutedRole: return track.muted;
    case SoloedRole: return track.soloed;
    case VolumeRole: return track.volume;
    case AudibleRole: return targetGains()[index.row()] > 0;
    case PeaksRole: return track.peaks;
    }
    return {};
}

QHash<int, QByteArray> Mixer::roleNames() const {
    return {{NameRole, "name"}, {MutedRole, "muted"}, {SoloedRole, "soloed"},
            {VolumeRole, "volume"}, {AudibleRole, "audible"}, {PeaksRole, "peaks"}};
}

void Mixer::setSelected(int row) {
    row = qBound(0, row, qMax(0, int(m_tracks.size()) - 1));
    if (row != m_selected) {
        m_selected = row;
        emit selectedChanged();
    }
}

bool Mixer::anySolo() const {
    return std::any_of(m_tracks.begin(), m_tracks.end(), [](const Track &t) { return t.soloed; });
}

QStringList Mixer::names() const {
    QStringList list;
    for (const auto &track : m_tracks)
        list << track.name;
    return list;
}

// Solo wins over mute, the way it does on a mixing desk.
std::vector<float> Mixer::targetGains() const {
    const bool solo = anySolo();
    std::vector<float> gains;
    for (const auto &track : m_tracks)
        gains.push_back(solo ? (track.soloed ? float(track.volume) : 0.f) : (track.muted ? 0.f : float(track.volume)));
    return gains;
}

void Mixer::mixInto(qint16 *out, qint64 from, qint64 count, const std::vector<float> &startGains,
                    const std::vector<float> &endGains) const {
    std::vector<float> sum(size_t(count * Channels), 0.f);
    for (size_t t = 0; t < m_tracks.size(); ++t) {
        const float start = t < startGains.size() ? startGains[t] : 0.f;
        const float end = t < endGains.size() ? endGains[t] : 0.f;
        if (start == 0.f && end == 0.f)
            continue;
        const auto *pcm = reinterpret_cast<const qint16 *>(m_tracks[t].pcm.constData());
        const qint64 available = m_tracks[t].pcm.size() / BytesPerFrame;
        for (qint64 i = 0; i < count && from + i < available; ++i) {
            const float gain = i >= RampFrames ? end : start + (end - start) * float(i) / RampFrames;
            if (gain == 0.f)
                continue;
            const qint64 at = (from + i) * Channels;
            sum[size_t(i * Channels)] += pcm[at] * gain;
            sum[size_t(i * Channels + 1)] += pcm[at + 1] * gain;
        }
    }
    for (size_t i = 0; i < sum.size(); ++i)
        out[i] = qint16(std::clamp(sum[i], -32768.f, 32767.f));
}

void Mixer::load(const QStringList &files, const QString &folder, const QString &title) {
    clear();
    if (files.isEmpty()) {
        emit loadFailed(tr("There are no stems in %1.").arg(folder));
        return;
    }
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) {
        emit loadFailed(tr("ffmpeg isn't installed, so the stems can't be read."));
        return;
    }

    m_loading = true;
    emit loadingChanged();
    m_pending.assign(size_t(files.size()), Track{});
    m_pendingFolder = folder;
    m_pendingTitle = title;
    m_loadError.clear();
    m_decodersLeft = int(files.size());

    for (int i = 0; i < files.size(); ++i) {
        m_pending[size_t(i)].name = QFileInfo(files[i]).completeBaseName();
        auto *decoder = new QProcess(this);
        m_decoders.push_back(decoder);
        connect(decoder, &QProcess::readyReadStandardOutput, this, [this, decoder, i] {
            m_pending[size_t(i)].pcm += decoder->readAllStandardOutput();
        });
        connect(decoder, &QProcess::finished, this, [this, decoder, i, files](int code, QProcess::ExitStatus status) {
            m_pending[size_t(i)].pcm += decoder->readAllStandardOutput();
            if (code != 0 || status != QProcess::NormalExit) {
                const QString detail = QString::fromUtf8(decoder->readAllStandardError()).trimmed();
                m_loadError = tr("Could not read %1: %2").arg(QFileInfo(files[i]).fileName(), detail);
            }
            m_decoders.erase(std::remove(m_decoders.begin(), m_decoders.end(), decoder), m_decoders.end());
            decoder->deleteLater();
            if (--m_decodersLeft == 0)
                finishLoad();
        });
        decoder->start(ffmpeg, {"-nostdin", "-v", "error", "-i", files[i], "-f", "s16le", "-acodec", "pcm_s16le",
                                "-ac", "2", "-ar", QString::number(SampleRate), "pipe:1"});
    }
}

void Mixer::finishLoad() {
    if (!m_loadError.isEmpty()) {
        m_pending.clear();
        m_loading = false;
        emit loadingChanged();
        emit loadFailed(m_loadError);
        return;
    }

    // Demucs writes every stem at the song's length, but a stem folder put
    // together by hand might not be; play to the end of the longest.
    qint64 frames = 0;
    for (auto &track : m_pending) {
        const qint64 count = track.pcm.size() / BytesPerFrame;
        frames = qMax(frames, count);
        const auto *pcm = reinterpret_cast<const qint16 *>(track.pcm.constData());
        const qint64 perBin = qMax<qint64>(1, (count + PeakBins - 1) / PeakBins);
        track.peaks.clear();
        for (qint64 bin = 0; bin * perBin < count; ++bin) {
            int peak = 0;
            const qint64 end = qMin(count, (bin + 1) * perBin) * Channels;
            for (qint64 s = bin * perBin * Channels; s < end; ++s)
                peak = qMax(peak, std::abs(int(pcm[s])));
            // A square root lifts quiet passages enough to see.
            track.peaks.append(std::sqrt(peak / 32768.0));
        }
    }

    beginResetModel();
    m_tracks = std::move(m_pending);
    m_pending.clear();
    m_frames = frames;
    m_frame = 0;
    m_ended = false;
    m_position = 0;
    m_folder = m_pendingFolder;
    m_title = m_pendingTitle;
    m_loading = false;
    m_undo.clear();
    m_redo.clear();
    m_cents = 0;
    m_liveCents = 0;
    {
        QMutexLocker lock(&m_gainLock);
        m_targetGains = targetGains();
    }
    endResetModel();
    m_selected = 0;
    emit selectedChanged();
    emit loadingChanged();
    emit loadedChanged();
    emit positionChanged();
    emit historyChanged();
    emit mixChanged();
}

void Mixer::clear() {
    flushSave();
    pause();
    for (auto *decoder : m_decoders) {
        decoder->disconnect(this);
        decoder->kill();
        decoder->deleteLater();
    }
    m_decoders.clear();
    m_pending.clear();
    const bool wasLoading = m_loading;
    m_loading = false;
    beginResetModel();
    m_tracks.clear();
    {
        QMutexLocker lock(&m_gainLock);
        m_targetGains.clear();
    }
    m_frames = 0;
    m_frame = 0;
    m_position = 0;
    m_cents = 0;
    m_liveCents = 0;
    m_folder.clear();
    m_title.clear();
    m_undo.clear();
    m_redo.clear();
    endResetModel();
    if (wasLoading)
        emit loadingChanged();
    emit loadedChanged();
    emit positionChanged();
    emit historyChanged();
}

bool Mixer::ensureSink() {
    if (m_sink)
        return true;
    QAudioFormat format;
    format.setSampleRate(SampleRate);
    format.setChannelCount(Channels);
    format.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        emit audioFailed(tr("There is no audio output to play through."));
        return false;
    }
    if (!device.isFormatSupported(format)) {
        emit audioFailed(tr("%1 can't play 44.1 kHz 16-bit stereo.").arg(device.description()));
        return false;
    }
    m_stream = std::make_unique<Stream>(this);
    m_stream->open(QIODevice::ReadOnly);
    m_sink = new QAudioSink(device, format, this);
    // A short buffer keeps mutes and seeks feeling immediate.
    m_sink->setBufferSize(4096 * BytesPerFrame);
    connect(m_sink, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::StoppedState && m_sink->error() != QAudio::NoError && m_playing) {
            m_playing = false;
            m_ticker.stop();
            emit playingChanged();
            emit audioFailed(tr("Playback stopped: the audio device went away."));
            m_sink->deleteLater();
            m_sink = nullptr;
        }
    });
    return true;
}

void Mixer::play() {
    if (!loaded() || m_playing)
        return;
    if (m_frame >= m_frames)
        m_frame = 0;
    m_ended = false;
    if (!ensureSink())
        return;
    m_stream->resetGains();
    m_restartShift = true;
    if (m_sink->state() == QAudio::SuspendedState)
        m_sink->resume();
    else
        m_sink->start(m_stream.get());
    m_playing = true;
    m_ticker.start();
    emit playingChanged();
}

void Mixer::pause() {
    if (!m_playing)
        return;
    if (m_sink)
        m_sink->suspend();
    m_playing = false;
    m_ticker.stop();
    updatePosition();
    emit playingChanged();
}

void Mixer::togglePlay() {
    m_playing ? pause() : play();
}

void Mixer::seek(double seconds) {
    if (!loaded())
        return;
    m_frame = qBound<qint64>(0, qint64(seconds * SampleRate), m_frames);
    m_ended = false;
    m_restartShift = true;
    m_shiftLag = 0;
    // Drop what's already queued, so the jump is heard right away.
    if (m_sink && m_playing) {
        m_sink->reset();
        m_stream->resetGains();
        m_sink->start(m_stream.get());
    } else if (m_sink) {
        m_sink->stop();
    }
    m_position = double(m_frame) / SampleRate;
    emit positionChanged();
}

void Mixer::updatePosition() {
    if (m_ended && m_playing) {
        pause();
        m_ended = false;
        m_frame = 0;
        if (m_sink) {
            m_sink->stop();
        }
    }
    qint64 frame = m_frame.load();
    // What's buffered in the sink hasn't been heard yet.
    // What's buffered in the sink, or still inside the pitch shifter,
    // hasn't been heard yet.
    if (m_sink && m_playing)
        frame -= qMax<qint64>(0, m_sink->bufferSize() - m_sink->bytesFree()) / BytesPerFrame + m_shiftLag.load();
    const double position = double(qBound<qint64>(0, frame, m_frames)) / SampleRate;
    if (position != m_position) {
        m_position = position;
        emit positionChanged();
    }
}

Mixer::MixState Mixer::state() const {
    MixState state;
    for (const auto &track : m_tracks)
        state.tracks.push_back({track.muted, track.soloed, track.volume});
    state.cents = m_cents;
    return state;
}

void Mixer::apply(const MixState &state) {
    for (size_t i = 0; i < m_tracks.size() && i < state.tracks.size(); ++i) {
        m_tracks[i].muted = state.tracks[i].muted;
        m_tracks[i].soloed = state.tracks[i].soloed;
        m_tracks[i].volume = state.tracks[i].volume;
    }
    m_cents = state.cents;
    edited();
}

void Mixer::remember() {
    m_undo.push_back(state());
    if (m_undo.size() > UndoLimit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    emit historyChanged();
}

void Mixer::mixUpdated() {
    {
        QMutexLocker lock(&m_gainLock);
        m_targetGains = targetGains();
    }
    m_liveCents = m_cents;
    if (!m_tracks.empty())
        emit dataChanged(index(0), index(int(m_tracks.size()) - 1), {MutedRole, SoloedRole, VolumeRole, AudibleRole});
    emit mixChanged();
}

void Mixer::edited() {
    mixUpdated();
    if (m_autosave && !m_folder.isEmpty())
        m_saveTimer.start();
}

void Mixer::flushSave() {
    if (!m_saveTimer.isActive())
        return;
    m_saveTimer.stop();
    const QString problem = saveSettings();
    if (!problem.isEmpty())
        emit saveFailed(problem);
}

void Mixer::toggleMute(int row) {
    if (row < 0 || row >= int(m_tracks.size()))
        return;
    remember();
    m_tracks[size_t(row)].muted = !m_tracks[size_t(row)].muted;
    edited();
}

void Mixer::toggleSolo(int row) {
    if (row < 0 || row >= int(m_tracks.size()))
        return;
    remember();
    m_tracks[size_t(row)].soloed = !m_tracks[size_t(row)].soloed;
    edited();
}

void Mixer::setVolume(int row, double volume) {
    if (row < 0 || row >= int(m_tracks.size()))
        return;
    volume = std::round(qBound(0.0, volume, 1.0) * 100) / 100;
    if (volume == m_tracks[size_t(row)].volume)
        return;
    remember();
    m_tracks[size_t(row)].volume = volume;
    edited();
}

void Mixer::nudgeVolume(int row, double delta) {
    if (row >= 0 && row < int(m_tracks.size()))
        setVolume(row, m_tracks[size_t(row)].volume + delta);
}

void Mixer::setCents(int cents) {
    cents = qBound(-PitchShifter::MaxCents, cents, PitchShifter::MaxCents);
    if (m_tracks.empty() || cents == m_cents)
        return;
    remember();
    m_cents = cents;
    edited();
}

void Mixer::resetMix() {
    MixState fresh{std::vector<TrackState>(m_tracks.size(), TrackState{false, false, 1.0}), 0};
    if (fresh == state())
        return;
    remember();
    apply(fresh);
}

void Mixer::undo() {
    if (m_undo.empty())
        return;
    m_redo.push_back(state());
    const MixState previous = m_undo.back();
    m_undo.pop_back();
    apply(previous);
    emit historyChanged();
}

void Mixer::redo() {
    if (m_redo.empty())
        return;
    m_undo.push_back(state());
    const MixState next = m_redo.back();
    m_redo.pop_back();
    apply(next);
    emit historyChanged();
}

QString Mixer::settingsPath() const {
    return m_folder.isEmpty() ? QString() : QDir(m_folder).filePath(SettingsFile);
}

// Stems are matched by name, so a saved mix still fits if the folder gains
// or loses a stem.
bool Mixer::restoreSettings() {
    QFile file(settingsPath());
    if (m_tracks.empty() || !file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.isEmpty())
        return false;
    m_cents = qBound(-PitchShifter::MaxCents, root.value("cents").toInt(0), PitchShifter::MaxCents);
    const QJsonObject stems = root.value("stems").toObject();
    for (auto &track : m_tracks) {
        const QJsonObject saved = stems.value(track.name).toObject();
        track.muted = saved.value("muted").toBool(false);
        track.soloed = saved.value("soloed").toBool(false);
        track.volume = qBound(0.0, saved.value("volume").toDouble(1.0), 1.0);
    }
    mixUpdated();
    return true;
}

QString Mixer::saveSettings() const {
    if (m_folder.isEmpty())
        return {};
    QJsonObject stems;
    for (const auto &track : m_tracks)
        stems.insert(track.name, QJsonObject{{"muted", track.muted}, {"soloed", track.soloed}, {"volume", track.volume}});
    const QJsonObject root{{"version", 1}, {"cents", m_cents}, {"stems", stems}};
    QSaveFile file(settingsPath());
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit())
        return tr("Could not save this song's mix to %1: %2").arg(settingsPath(), file.errorString());
    return {};
}

QString Mixer::exportMix(const QString &path) const {
    if (m_tracks.empty())
        return tr("There is nothing to export.");
    const std::vector<float> gains = targetGains();
    if (std::all_of(gains.begin(), gains.end(), [](float g) { return g == 0.f; }))
        return tr("Every stem is muted, so the mix would be silent.");

    // QSaveFile writes beside the target and renames it into place.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return tr("Could not write %1: %2").arg(path, file.errorString());

    const quint32 dataBytes = quint32(m_frames * BytesPerFrame);
    QByteArray header(44, 0);
    auto put32 = [&](int at, quint32 v) { qToLittleEndian(v, header.data() + at); };
    auto put16 = [&](int at, quint16 v) { qToLittleEndian(v, header.data() + at); };
    memcpy(header.data(), "RIFF", 4);
    put32(4, 36 + dataBytes);
    memcpy(header.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, Channels);
    put32(24, SampleRate);
    put32(28, SampleRate * BytesPerFrame);
    put16(32, BytesPerFrame);
    put16(34, 16);
    memcpy(header.data() + 36, "data", 4);
    put32(40, dataBytes);
    file.write(header);

    constexpr qint64 chunk = 65536;
    std::vector<qint16> buffer(size_t(chunk * Channels));
    // The export is shifted the same way as playback, and comes out the
    // same length as the song.
    std::unique_ptr<PitchShifter> shifter;
    if (m_cents != 0) {
        shifter = std::make_unique<PitchShifter>(SampleRate, Channels);
        shifter->restart(m_cents);
    }
    qint64 cursor = 0;
    auto source = [&](qint16 *into, qint64 wanted) {
        const qint64 count = qBound<qint64>(0, m_frames - cursor, wanted);
        mixInto(into, cursor, count, gains, gains);
        cursor += count;
        return count;
    };
    for (qint64 from = 0; from < m_frames; from += chunk) {
        const qint64 count = qMin(chunk, m_frames - from);
        const qint64 made = shifter ? shifter->render(buffer.data(), count, source) : source(buffer.data(), count);
        std::fill(buffer.begin() + made * Channels, buffer.begin() + count * Channels, qint16(0));
        // WAV is little-endian, like every machine Omarchy runs on.
        file.write(reinterpret_cast<const char *>(buffer.data()), count * BytesPerFrame);
    }
    if (!file.commit())
        return tr("Could not write %1: %2").arg(path, file.errorString());
    return {};
}
