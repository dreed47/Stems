#include "tempo.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace Tempo {
namespace {

constexpr int Hop = 512;              // about 11.6 ms at 44.1 kHz
constexpr double MinBpm = 50, MaxBpm = 220;
// Tempi near 120 BPM are the most likely; this is how quickly the
// likelihood falls away, in octaves (Ellis uses a similar prior).
constexpr double PriorCenter = 120, PriorOctaves = 1.0;
// How strongly the beat tracker holds to the tempo against the onsets.
constexpr double Tightness = 100;
// Snap to a beat this close after the playhead rather than the one before.
constexpr double SnapAhead = 0.08;
// The first sound is the first sample this loud (-36 dBFS) in any stem:
// above the faint bleed Demucs leaves in a stem, below any real note.
constexpr int SoundLevel = 500;
// A count-in to the first sound starts the song this much before it, so the
// note's attack isn't clipped.
constexpr double SoundLead = 0.03;

double median(std::vector<double> values) {
    if (values.empty())
        return 0;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    return values[mid];
}

// How much new sound arrives in each hop: the rise in loudness of the lows
// (kick) and of everything above them (snare, hats), on a log scale so quiet
// hits count too.
std::vector<double> onsetEnvelope(const std::vector<Source> &sources, qint64 frames, int sampleRate) {
    // Level first, so a quiet stem reads the same as a loud one.
    double peak = 0;
    for (qint64 f = 0; f < frames; ++f) {
        double x = 0;
        for (const auto &[pcm, count] : sources)
            if (f < count)
                x += pcm[2 * f] + pcm[2 * f + 1];
        peak = std::max(peak, std::abs(x));
    }
    if (peak < 64)
        return {};
    const double scale = 2 / peak;

    const qint64 hops = frames / Hop;
    std::vector<double> envelope(size_t(std::max<qint64>(0, hops)), 0.0);
    const double a = std::exp(-2 * M_PI * 200.0 / sampleRate);
    double lowpass = 0, previousLow = 0, previousHigh = 0;
    for (qint64 h = 0; h < hops; ++h) {
        double low = 0, high = 0;
        for (qint64 f = h * Hop; f < (h + 1) * Hop; ++f) {
            double x = 0;
            for (const auto &[pcm, count] : sources)
                if (f < count)
                    x += (pcm[2 * f] + pcm[2 * f + 1]) * scale / 2;
            lowpass = (1 - a) * x + a * lowpass;
            low += lowpass * lowpass;
            high += (x - lowpass) * (x - lowpass);
        }
        low = std::log1p(1000 * low / Hop);
        high = std::log1p(1000 * high / Hop);
        if (h > 0)
            envelope[size_t(h)] = std::max(0.0, low - previousLow) + std::max(0.0, high - previousHigh);
        previousLow = low;
        previousHigh = high;
    }

    // Take away the local average, so slow swells don't read as hits, and
    // scale to unit spread.
    const int half = int(0.5 * sampleRate / Hop);
    std::vector<double> sum(envelope.size() + 1, 0.0);
    std::partial_sum(envelope.begin(), envelope.end(), sum.begin() + 1);
    std::vector<double> out(envelope.size());
    for (size_t i = 0; i < envelope.size(); ++i) {
        const size_t from = i > size_t(half) ? i - size_t(half) : 0;
        const size_t to = std::min(envelope.size(), i + size_t(half) + 1);
        out[i] = std::max(0.0, envelope[i] - (sum[to] - sum[from]) / double(to - from));
    }
    double squares = 0;
    for (double v : out)
        squares += v * v;
    const double spread = std::sqrt(squares / std::max<size_t>(1, out.size()));
    // A sustained tone wobbles against the hop a little; hits move it a lot.
    if (spread < 0.2)
        return {};
    for (double &v : out)
        v /= spread;
    return out;
}

// The beat period in hops, from the envelope's autocorrelation weighted
// toward likely tempi; 0 when nothing repeats.
double estimatePeriod(const std::vector<double> &envelope, double hopsPerSecond) {
    const int minLag = int(std::floor(60 * hopsPerSecond / MaxBpm));
    const int maxLag = int(std::ceil(60 * hopsPerSecond / MinBpm));
    if (int(envelope.size()) < maxLag * 4)
        return 0;
    std::vector<double> correlation(size_t(maxLag + 2), 0.0);
    for (int lag = std::max(1, minLag - 1); lag <= maxLag + 1; ++lag) {
        double s = 0;
        for (size_t i = 0; i + size_t(lag) < envelope.size(); ++i)
            s += envelope[i] * envelope[i + size_t(lag)];
        correlation[size_t(lag)] = s / double(envelope.size() - size_t(lag));
    }
    double zero = 0;
    for (double v : envelope)
        zero += v * v;
    zero /= double(envelope.size());

    int best = 0;
    double bestScore = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double bpm = 60 * hopsPerSecond / lag;
        const double octaves = std::log2(bpm / PriorCenter) / PriorOctaves;
        const double score = correlation[size_t(lag)] * std::exp(-0.5 * octaves * octaves);
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    // A pulse has to stand out from the envelope's own energy.
    if (best == 0 || correlation[size_t(best)] < 0.15 * zero)
        return 0;
    // Between whole hops, by fitting a parabola through the peak.
    const double l = correlation[size_t(best - 1)], c = correlation[size_t(best)], r = correlation[size_t(best + 1)];
    const double bend = l - 2 * c + r;
    const double offset = bend < 0 ? std::clamp(0.5 * (l - r) / bend, -0.5, 0.5) : 0.0;
    return best + offset;
}

// Beat positions in hops, between whole hops where the onsets say so; empty
// when the beats don't land on real hits (a steady tone has no pulse).
std::vector<double> trackBeats(const std::vector<double> &envelope, double period) {
    // Smooth the onsets a little, so a hit a hop early or late still counts.
    const int reach = int(std::round(period));
    std::vector<double> window(size_t(2 * reach + 1));
    for (int i = -reach; i <= reach; ++i)
        window[size_t(i + reach)] = std::exp(-0.5 * std::pow(i * 32.0 / period, 2));
    const int n = int(envelope.size());
    std::vector<double> local(size_t(n), 0.0);
    for (int i = 0; i < n; ++i)
        for (int j = -reach; j <= reach; ++j)
            if (i + j >= 0 && i + j < n)
                local[size_t(i)] += envelope[size_t(i + j)] * window[size_t(j + reach)];

    // Each hop's best score as a beat: its own onset plus the best earlier
    // beat, penalised by how far the gap strays from the period.
    std::vector<double> score(size_t(n), 0.0);
    std::vector<int> previous(size_t(n), -1);
    const int nearest = int(std::round(period / 2)), farthest = int(std::round(period * 2));
    std::vector<double> cost(size_t(farthest + 1), 0.0);
    for (int gap = nearest; gap <= farthest; ++gap)
        cost[size_t(gap)] = -Tightness * std::pow(std::log(gap / period), 2);
    for (int i = 0; i < n; ++i) {
        double best = -std::numeric_limits<double>::infinity();
        int from = -1;
        for (int gap = nearest; gap <= farthest && i - gap >= 0; ++gap) {
            const double s = score[size_t(i - gap)] + cost[size_t(gap)];
            if (s > best) {
                best = s;
                from = i - gap;
            }
        }
        if (from >= 0 && best > 0) {
            score[size_t(i)] = local[size_t(i)] + best;
            previous[size_t(i)] = from;
        } else {
            score[size_t(i)] = local[size_t(i)];
        }
    }

    // The last beat is the last strong peak of the score, not merely the
    // last hop; then follow the chain back.
    std::vector<double> peaks;
    for (int i = 1; i + 1 < n; ++i)
        if (score[size_t(i)] > score[size_t(i - 1)] && score[size_t(i)] >= score[size_t(i + 1)])
            peaks.push_back(score[size_t(i)]);
    const double threshold = 0.5 * median(peaks);
    int last = -1;
    for (int i = n - 2; i > 0 && last < 0; --i)
        if (score[size_t(i)] > score[size_t(i - 1)] && score[size_t(i)] >= score[size_t(i + 1)] && score[size_t(i)] >= threshold)
            last = i;
    std::vector<int> beats;
    for (int i = last; i >= 0; i = previous[size_t(i)])
        beats.push_back(i);
    std::reverse(beats.begin(), beats.end());

    // Trim the quiet ends: beats the tracker coasted through before the
    // drums came in or after they stopped. The count-in extends the grid
    // there instead.
    double strength = 0;
    for (int b : beats)
        strength += local[size_t(b)] * local[size_t(b)];
    strength = 0.5 * std::sqrt(strength / std::max<size_t>(1, beats.size()));
    while (!beats.empty() && local[size_t(beats.front())] < strength)
        beats.erase(beats.begin());
    while (!beats.empty() && local[size_t(beats.back())] < strength)
        beats.pop_back();

    std::vector<double> onBeat;
    for (int b : beats)
        onBeat.push_back(local[size_t(b)]);
    if (beats.size() < 4 || median(onBeat) < 1.0)
        return {};

    std::vector<double> refined;
    for (int b : beats) {
        double offset = 0;
        if (b > 0 && b + 1 < n) {
            const double l = local[size_t(b - 1)], c = local[size_t(b)], r = local[size_t(b + 1)];
            const double bend = l - 2 * c + r;
            if (bend < 0)
                offset = std::clamp(0.5 * (l - r) / bend, -0.5, 0.5);
        }
        refined.push_back(b + offset);
    }
    return refined;
}

// The time per beat over beats [from, to), as the slope of a least-squares
// line, so no single beat's timing decides it.
double slope(const std::vector<double> &beats, size_t from, size_t to) {
    const double n = double(to - from);
    const double meanIndex = (n - 1) / 2;
    double meanTime = 0;
    for (size_t i = from; i < to; ++i)
        meanTime += beats[i] / n;
    double covariance = 0, variance = 0;
    for (size_t i = from; i < to; ++i) {
        const double d = double(i - from) - meanIndex;
        covariance += d * (beats[i] - meanTime);
        variance += d * d;
    }
    return variance > 0 ? covariance / variance : 0;
}

// The tempo around beat i: eight beats either side, which follows a
// drummer who speeds up without hanging on one beat.
double localInterval(const std::vector<double> &beats, size_t i) {
    const size_t from = i >= 8 ? i - 8 : 0;
    const size_t to = std::min(beats.size(), i + 9);
    return slope(beats, from, to);
}

}  // namespace

Grid detect(const std::vector<Source> &sources, qint64 frames, int sampleRate) {
    const std::vector<double> envelope = onsetEnvelope(sources, frames, sampleRate);
    if (envelope.empty())
        return {};
    const double hopsPerSecond = double(sampleRate) / Hop;
    const double period = estimatePeriod(envelope, hopsPerSecond);
    if (period <= 0)
        return {};

    Grid grid;
    for (double hop : trackBeats(envelope, period))
        grid.beats.push_back(hop * Hop / sampleRate);
    if (grid.beats.size() < 4)
        return {};
    // The tempo shown is the song's typical one: the median of its local
    // tempi, so a slow intro or a ritardando doesn't pull it.
    std::vector<double> intervals;
    for (size_t i = 0; i < grid.beats.size(); i += 4)
        intervals.push_back(localInterval(grid.beats, i));
    grid.bpm = 60 / median(intervals);
    return grid;
}

double tappedBpm(const std::vector<double> &taps) {
    if (taps.size() < 2 || taps.back() <= taps.front())
        return 0;
    // A least-squares line through the taps, so one early or late tap only
    // nudges the tempo.
    const double n = double(taps.size());
    double meanIndex = (n - 1) / 2, meanTime = 0;
    for (double t : taps)
        meanTime += t / n;
    double covariance = 0, variance = 0;
    for (size_t i = 0; i < taps.size(); ++i) {
        covariance += (double(i) - meanIndex) * (taps[i] - meanTime);
        variance += (double(i) - meanIndex) * (double(i) - meanIndex);
    }
    const double interval = covariance / variance;
    return interval > 0 ? std::clamp(60 / interval, 20.0, 400.0) : 0;
}

double firstSound(const std::vector<Source> &sources, int sampleRate) {
    qint64 first = -1;
    for (const auto &[pcm, count] : sources) {
        const qint64 end = first >= 0 ? std::min(first, count) : count;
        for (qint64 f = 0; f < end; ++f)
            if (std::abs(int(pcm[2 * f])) > SoundLevel || std::abs(int(pcm[2 * f + 1])) > SoundLevel) {
                first = f;
                break;
            }
    }
    if (first < 0)
        return -1;
    // Back from where it got loud to where the note began.
    return std::max(0.0, double(first) / sampleRate - 0.005);
}

CountIn plan(const std::vector<double> &beats, double bpm, double playhead, int count, double firstSound) {
    if (bpm <= 0 || count <= 0)
        return {};
    playhead = std::max(0.0, playhead);
    double beat = playhead, interval = 60 / bpm;

    // From the top, or anywhere in the silence before it, the song's first
    // note is the downbeat. A grid carried back from where the drums come in
    // may put a beat in the silence instead, which reads as a missing click.
    if (firstSound >= 0 && playhead <= firstSound + SnapAhead) {
        if (beats.size() >= 2) {
            const auto after = std::upper_bound(beats.begin(), beats.end(), firstSound);
            interval = localInterval(beats, after == beats.begin() ? 0 : size_t(after - beats.begin()) - 1);
        }
        CountIn plan;
        plan.interval = interval;
        plan.start = std::clamp(playhead, std::max(0.0, firstSound - SoundLead), firstSound);
        plan.preroll = plan.start - (firstSound - count * interval);
        return plan;
    }

    if (beats.size() >= 2) {
        const double at = playhead + SnapAhead;
        const auto after = std::upper_bound(beats.begin(), beats.end(), at);
        if (after == beats.begin()) {
            interval = localInterval(beats, 0);
            beat = beats.front() - std::ceil((beats.front() - at) / interval) * interval;
        } else if (after == beats.end()) {
            interval = localInterval(beats, beats.size() - 1);
            beat = beats.back() + std::floor((at - beats.back()) / interval) * interval;
        } else {
            const size_t i = size_t(after - beats.begin()) - 1;
            interval = localInterval(beats, i);
            beat = beats[i];
        }
    }
    CountIn plan;
    plan.interval = interval;
    plan.start = std::max(0.0, beat);
    // The clicks fall on the beats before `beat`; when that beat is before
    // the song starts, the song comes in after a short silence.
    plan.preroll = plan.start - (beat - count * interval);
    return plan;
}

}  // namespace Tempo
