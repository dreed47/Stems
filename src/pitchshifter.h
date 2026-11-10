#pragma once

#include <QtGlobal>
#include <functional>
#include <memory>
#include <vector>

namespace RubberBand { class RubberBandStretcher; }

// Shifts the pitch of interleaved 16-bit stereo without changing its tempo,
// using Rubber Band in real-time mode, so the playback stream and the
// exporter can both pull shifted audio a block at a time.
class PitchShifter {
public:
    // Fills the buffer with up to `frames` frames of input and returns how
    // many it wrote; fewer than asked means the input has ended.
    using Source = std::function<qint64(qint16 *buffer, qint64 frames)>;

    PitchShifter(int sampleRate, int channels);
    ~PitchShifter();

    static constexpr int MaxCents = 100;
    static double scaleFor(double cents);

    // Starts over at `cents`, so the next output lines up with the next input.
    void restart(double cents);
    // Changes the pitch mid-stream, without a gap.
    void setCents(double cents);
    double cents() const { return m_cents; }

    // Writes up to `frames` shifted frames to out and returns how many; fewer
    // only once the input has ended and everything has come out.
    qint64 render(qint16 *out, qint64 frames, const Source &source);

    // Frames taken from the source that haven't come out yet.
    qint64 lag() const { return m_fed - m_produced; }

private:
    int m_channels;
    std::unique_ptr<RubberBand::RubberBandStretcher> m_stretcher;
    double m_cents = 0;
    qint64 m_discard = 0;
    qint64 m_fed = 0;
    qint64 m_produced = 0;
    bool m_inputEnded = false;
    std::vector<qint16> m_interleaved;
    std::vector<std::vector<float>> m_planes;
    std::vector<float *> m_pointers;
};
