#include "pitchshifter.h"

#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>

using RubberBand::RubberBandStretcher;

namespace {
constexpr size_t BlockFrames = 1024;
}

PitchShifter::PitchShifter(int sampleRate, int channels)
    : m_channels(channels),
      // The R3 engine sounds best on music. Keeping the channels together
      // holds the stereo image still while the pitch moves.
      m_stretcher(std::make_unique<RubberBandStretcher>(
          size_t(sampleRate), size_t(channels),
          RubberBandStretcher::OptionProcessRealTime | RubberBandStretcher::OptionEngineFiner |
              RubberBandStretcher::OptionChannelsTogether)),
      m_interleaved(BlockFrames * size_t(channels)),
      m_planes(size_t(channels), std::vector<float>(BlockFrames)),
      m_pointers(size_t(channels)) {
    m_stretcher->setMaxProcessSize(BlockFrames);
    for (int c = 0; c < channels; ++c)
        m_pointers[size_t(c)] = m_planes[size_t(c)].data();
}

PitchShifter::~PitchShifter() = default;

double PitchShifter::scaleFor(double cents) {
    return std::pow(2.0, cents / 1200.0);
}

void PitchShifter::restart(double cents) {
    m_cents = cents;
    m_stretcher->reset();
    m_stretcher->setPitchScale(scaleFor(cents));
    m_fed = m_produced = 0;
    m_inputEnded = false;

    // Rubber Band wants some silence ahead of the audio, and its output
    // starts that many frames late; trimming the delay keeps the shifted
    // audio lined up with where playback is.
    for (auto &plane : m_planes)
        std::fill(plane.begin(), plane.end(), 0.f);
    size_t pad = m_stretcher->getPreferredStartPad();
    while (pad > 0) {
        const size_t n = std::min(pad, BlockFrames);
        m_stretcher->process(m_pointers.data(), n, false);
        pad -= n;
    }
    m_discard = qint64(m_stretcher->getStartDelay());
}

void PitchShifter::setCents(double cents) {
    if (cents == m_cents)
        return;
    m_cents = cents;
    m_stretcher->setPitchScale(scaleFor(cents));
}

qint64 PitchShifter::render(qint16 *out, qint64 frames, const Source &source) {
    qint64 written = 0;
    while (written < frames) {
        const int available = m_stretcher->available();
        if (available < 0)
            break;  // Finished: the input ended and all of it has come out.
        if (available > 0) {
            qint64 take = std::min<qint64>({available, qint64(BlockFrames),
                                            m_discard > 0 ? m_discard : frames - written});
            m_stretcher->retrieve(m_pointers.data(), size_t(take));
            if (m_discard > 0) {
                m_discard -= take;
                continue;
            }
            for (qint64 i = 0; i < take; ++i)
                for (int c = 0; c < m_channels; ++c)
                    out[(written + i) * m_channels + c] =
                        qint16(std::clamp(m_planes[size_t(c)][size_t(i)] * 32768.f, -32768.f, 32767.f));
            written += take;
            m_produced += take;
            continue;
        }
        if (m_inputEnded)
            break;

        size_t wanted = std::clamp<size_t>(m_stretcher->getSamplesRequired(), 1, BlockFrames);
        const qint64 got = source(m_interleaved.data(), qint64(wanted));
        for (qint64 i = 0; i < got; ++i)
            for (int c = 0; c < m_channels; ++c)
                m_planes[size_t(c)][size_t(i)] = m_interleaved[size_t(i * m_channels + c)] / 32768.f;
        m_inputEnded = got < qint64(wanted);
        m_stretcher->process(m_pointers.data(), size_t(got), m_inputEnded);
        m_fed += got;
    }
    return written;
}
