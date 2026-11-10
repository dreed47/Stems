#pragma once

#include <QAbstractListModel>
#include <QIODevice>
#include <QMutex>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <vector>

class QAudioSink;
class QProcess;

// Holds every stem of a song in memory as 44.1 kHz 16-bit stereo and mixes
// them on the fly. One audio stream plays the mix, so the stems can never
// drift apart, and mute, solo and volume change instantly (with a short
// ramp, so they don't click).
class Mixer : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY loadedChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY loadedChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double duration READ duration NOTIFY loadedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool anySolo READ anySolo NOTIFY mixChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, MutedRole, SoloedRole, VolumeRole, AudibleRole, PeaksRole };

    static constexpr int SampleRate = 44100;
    static constexpr int Channels = 2;
    static constexpr int PeakBins = 1500;

    explicit Mixer(QObject *parent = nullptr);
    ~Mixer() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString title() const { return m_title; }
    QString folder() const { return m_folder; }
    bool loaded() const { return !m_tracks.empty() && !m_loading; }
    bool loading() const { return m_loading; }
    bool playing() const { return m_playing; }
    double duration() const { return double(m_frames) / SampleRate; }
    double position() const { return m_position; }
    int selected() const { return m_selected; }
    void setSelected(int row);
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    bool anySolo() const;

    // Decodes every stem file with ffmpeg; emits loadFailed or loadedChanged.
    void load(const QStringList &files, const QString &folder, const QString &title);
    Q_INVOKABLE void clear();

    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void skip(double seconds) { seek(position() + seconds); }

    Q_INVOKABLE void toggleMute(int row);
    Q_INVOKABLE void toggleSolo(int row);
    Q_INVOKABLE void setVolume(int row, double volume);
    Q_INVOKABLE void nudgeVolume(int row, double delta);
    Q_INVOKABLE void resetMix();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // The names of the stems, in order, for the CLI.
    QStringList names() const;
    // Writes what's audible right now as a WAV file, atomically. Returns an
    // error message, or an empty string when it worked.
    QString exportMix(const QString &path) const;

    // Mixes frames [from, from + count) with the given gains into out.
    void mixInto(qint16 *out, qint64 from, qint64 count, const std::vector<float> &startGains,
                 const std::vector<float> &endGains) const;
    std::vector<float> targetGains() const;

signals:
    void loadedChanged();
    void loadingChanged();
    void loadFailed(const QString &message);
    void playingChanged();
    void positionChanged();
    void selectedChanged();
    void historyChanged();
    void mixChanged();
    void audioFailed(const QString &message);

private:
    struct Track {
        QString name;
        QByteArray pcm;
        QVariantList peaks;
        bool muted = false;
        bool soloed = false;
        double volume = 1.0;
    };
    struct TrackState {
        bool muted, soloed;
        double volume;
        bool operator==(const TrackState &o) const { return muted == o.muted && soloed == o.soloed && volume == o.volume; }
    };
    using MixState = std::vector<TrackState>;

    class Stream;
    friend class Stream;

    MixState state() const;
    void apply(const MixState &state);
    void remember();
    void mixEdited();
    void finishLoad();
    void updatePosition();
    bool ensureSink();

    std::vector<Track> m_tracks;
    std::vector<Track> m_pending;
    std::vector<QProcess *> m_decoders;
    int m_decodersLeft = 0;
    QString m_loadError;
    QString m_folder, m_title, m_pendingFolder, m_pendingTitle;
    qint64 m_frames = 0;
    bool m_loading = false;
    bool m_playing = false;
    double m_position = 0;
    int m_selected = 0;
    std::vector<MixState> m_undo, m_redo;

    // Shared with the audio thread.
    mutable QMutex m_gainLock;
    std::vector<float> m_targetGains;
    std::atomic<qint64> m_frame{0};
    std::atomic<bool> m_ended{false};

    std::unique_ptr<Stream> m_stream;
    QAudioSink *m_sink = nullptr;
    QTimer m_ticker;
};
