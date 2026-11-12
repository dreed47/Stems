#include "structure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>


namespace Structure {
namespace {

// Chords are read at half the sample rate, which still covers the notes
// that name them, in frames of about 190 ms every 93 ms.
constexpr int FftSize = 4096;
constexpr int FftHop = 2048;
constexpr double LowestNote = 65, HighestNote = 2100;  // C2 to C7, in Hz
// Boundaries are judged over this many beats either side: four bars of 4/4.
constexpr int Reach = 16;
// The shortest section, in beats: two bars.
constexpr int Shortest = 8;
// Passages compared for repetition, in beats: one bar.
constexpr int Passage = 4;
// How far, in beats, a boundary moves to meet the singer: two bars.
constexpr int Snap = 8;
// Shorter than this, in beats, a part is a fragment of its neighbour.
constexpr int Fragment = 12;
// How much a stem's loudness counts against the chords in telling beats apart.
constexpr double LoudnessWeight = 0.5;
// And the vocal melody.
constexpr double TuneWeight = 1.0;
// Sections at least this alike are the same part played again.
constexpr double SameGroup = 0.72;
// How much the compression spares the weaker notes of a chord.
constexpr double Contrast = 4;
// A boundary scores this many standard deviations above the song's mean.
constexpr double Sensitivity = 0.5;

using Chroma = std::array<double, 12>;

void fft(std::vector<std::complex<double>> &a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> step = std::polar(1.0, -2 * M_PI / double(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

double median(std::vector<double> v) {
    if (v.empty())
        return 0;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// Centred, then scaled to unit length: what's compared is which notes
// stand out, not how much is playing.
void normalise(Chroma &c) {
    double mean = 0;
    for (double x : c)
        mean += x / 12;
    for (double &x : c)
        x -= mean;
    double sum = 0;
    for (double x : c)
        sum += x * x;
    const double length = std::sqrt(sum);
    for (double &x : c)
        x = length > 1e-9 ? x / length : 0;
}

// The pitch classes sounding in each frame of the given stems.
std::vector<Chroma> chromagram(const std::vector<const Stem *> &stems, qint64 frames, int sampleRate, double *hopSeconds) {
    const int factor = std::max(1, sampleRate / 22050);
    const double rate = double(sampleRate) / factor;
    std::vector<float> mono(size_t(frames / factor), 0.f);
    for (const Stem *stem : stems) {
        for (size_t i = 0; i < mono.size(); ++i)
            for (int k = 0; k < factor; ++k) {
                const qint64 f = qint64(i) * factor + k;
                if (f < stem->frames)
                    mono[i] += float(stem->pcm[2 * f] + stem->pcm[2 * f + 1]) / (65536.f * factor);
            }
    }

    std::vector<int> pitchClass(FftSize / 2, -1);
    for (int bin = 1; bin < FftSize / 2; ++bin) {
        const double hz = bin * rate / FftSize;
        if (hz >= LowestNote && hz <= HighestNote)
            pitchClass[size_t(bin)] = int((std::lround(12 * std::log2(hz / 440.0)) % 12 + 12) % 12);
    }
    std::vector<double> window(FftSize);
    for (int i = 0; i < FftSize; ++i)
        window[size_t(i)] = 0.5 - 0.5 * std::cos(2 * M_PI * i / FftSize);

    std::vector<Chroma> out;
    std::vector<std::complex<double>> buffer(FftSize);
    for (size_t start = 0; start + FftSize <= mono.size(); start += FftHop) {
        for (int i = 0; i < FftSize; ++i)
            buffer[size_t(i)] = mono[start + size_t(i)] * window[size_t(i)];
        fft(buffer);
        Chroma c{};
        for (int bin = 1; bin < FftSize / 2; ++bin)
            if (pitchClass[size_t(bin)] >= 0)
                c[size_t(pitchClass[size_t(bin)])] += std::abs(buffer[size_t(bin)]);
        // Relative to the frame's strongest note, then compressed a little,
        // so the loudest note doesn't drown the rest of the chord.
        const double top = *std::max_element(c.begin(), c.end());
        for (double &x : c)
            x = top > 1e-9 ? std::log1p(Contrast * x / top) : 0;
        out.push_back(c);
    }
    *hopSeconds = FftHop / rate;
    return out;
}

// How loud each stem is between two times, as a level from about -4
// (silence) to 0 (full scale).
double level(const Stem &stem, double from, double to, int sampleRate) {
    const qint64 a = std::clamp<qint64>(qint64(from * sampleRate), 0, stem.frames);
    const qint64 b = std::clamp<qint64>(qint64(to * sampleRate), a, stem.frames);
    if (b <= a)
        return -4;
    double sum = 0;
    // Every fourth frame is plenty for a loudness.
    qint64 n = 0;
    for (qint64 f = a; f < b; f += 4, ++n) {
        const double x = (stem.pcm[2 * f] + stem.pcm[2 * f + 1]) / 65536.0;
        sum += x * x;
    }
    return std::log10(std::sqrt(sum / double(std::max<qint64>(1, n))) + 1e-4);
}

// The beats with the grid carried on to both ends of the song, so every
// moment belongs to one; half seconds when there's no grid.
std::vector<double> fullGrid(const std::vector<double> &beats, double duration) {
    std::vector<double> grid;
    if (beats.size() < 8) {
        for (double t = 0; t < duration; t += 0.5)
            grid.push_back(t);
    } else {
        std::vector<double> gaps;
        for (size_t i = 0; i + 1 < std::min<size_t>(beats.size(), 9); ++i)
            gaps.push_back(beats[i + 1] - beats[i]);
        const double head = median(gaps);
        gaps.clear();
        for (size_t i = beats.size() - std::min<size_t>(beats.size(), 9); i + 1 < beats.size(); ++i)
            gaps.push_back(beats[i + 1] - beats[i]);
        const double tail = median(gaps);
        for (double t = beats.front() - head; t > 0; t -= head)
            grid.insert(grid.begin(), t);
        grid.insert(grid.begin(), 0.0);
        for (double b : beats)
            if (b > grid.back() + 0.05 && b < duration)
                grid.push_back(b);
        for (double t = beats.back() + tail; t < duration - 0.05; t += tail)
            grid.push_back(t);
    }
    grid.push_back(duration);
    return grid;
}

}  // namespace

std::vector<Section> analyse(const std::vector<Stem> &stems, qint64 frames, int sampleRate,
                             const std::vector<double> &beats) {
    const double duration = double(frames) / sampleRate;
    if (stems.empty() || duration < 20)
        return {};

    // The chords come from the accompaniment and the tune from the vocals,
    // read apart: a verse and a chorus often share chords but not a melody.
    std::vector<const Stem *> harmony, melody;
    long vocals = -1;
    for (size_t s = 0; s < stems.size(); ++s) {
        if (stems[s].name == QLatin1String("vocals")) {
            melody.push_back(&stems[s]);
            vocals = long(s);
        } else if (stems[s].name != QLatin1String("drums")) {
            harmony.push_back(&stems[s]);
        }
    }
    if (harmony.empty())
        std::swap(harmony, melody);

    double hop = 0;
    const std::vector<Chroma> chords = chromagram(harmony, frames, sampleRate, &hop);
    const std::vector<Chroma> tune = melody.empty() ? std::vector<Chroma>{} : chromagram(melody, frames, sampleRate, &hop);
    const std::vector<double> grid = fullGrid(beats, duration);
    const size_t n = grid.size() - 1;
    if (n < size_t(Reach * 2) || chords.empty())
        return {};

    // Each beat: its chord, its tune, and the loudness of every stem.
    auto perBeat = [&](const std::vector<Chroma> &frameChroma, size_t i) {
        Chroma c{};
        if (frameChroma.empty())
            return c;
        const size_t first = size_t(std::max(0.0, grid[i] / hop - 1));
        size_t last = size_t(std::max(0.0, grid[i + 1] / hop - 1));
        last = std::min(std::max(last, first + 1), frameChroma.size());
        for (size_t f = std::min(first, frameChroma.size() - 1); f < last; ++f)
            for (size_t k = 0; k < 12; ++k)
                c[k] += frameChroma[f][k];
        normalise(c);
        return c;
    };
    std::vector<Chroma> beatChroma(n), beatTune(n);
    std::vector<std::vector<double>> loud(n, std::vector<double>(stems.size()));
    double vocalPeak = -4;
    for (size_t i = 0; i < n; ++i) {
        beatChroma[i] = perBeat(chords, i);
        beatTune[i] = perBeat(tune, i);
        for (size_t s = 0; s < stems.size(); ++s)
            loud[i][s] = level(stems[s], grid[i], grid[i + 1], sampleRate);
        if (vocals >= 0)
            vocalPeak = std::max(vocalPeak, loud[i][size_t(vocals)]);
    }
    // A beat is sung when the vocals are within 24 dB of their loudest; the
    // tune of an unsung beat is only bleed from the other stems.
    auto isSung = [&](size_t i) { return vocals >= 0 && loud[i][size_t(vocals)] > vocalPeak - 1.2; };
    for (size_t i = 0; i < n; ++i)
        if (!isSung(i))
            beatTune[i] = Chroma{};


    // Loudness in units of each stem's own spread, so a quiet stem's
    // changes count as much as a loud one's.
    std::vector<std::vector<double>> z = loud;
    for (size_t s = 0; s < stems.size(); ++s) {
        double mean = 0, squares = 0;
        for (size_t i = 0; i < n; ++i)
            mean += loud[i][s] / double(n);
        for (size_t i = 0; i < n; ++i)
            squares += (loud[i][s] - mean) * (loud[i][s] - mean) / double(n);
        const double spread = std::max(0.1, std::sqrt(squares));
        for (size_t i = 0; i < n; ++i)
            z[i][s] = std::clamp((loud[i][s] - mean) / spread, -3.0, 3.0);
    }

    // How alike every pair of beats is.
    auto distance = [&](size_t i, size_t j) {
        double chord = 0, melody = 0, level = 0;
        for (size_t k = 0; k < 12; ++k) {
            chord += (beatChroma[i][k] - beatChroma[j][k]) * (beatChroma[i][k] - beatChroma[j][k]);
            melody += (beatTune[i][k] - beatTune[j][k]) * (beatTune[i][k] - beatTune[j][k]);
        }
        for (size_t s = 0; s < stems.size(); ++s)
            level += (z[i][s] - z[j][s]) * (z[i][s] - z[j][s]);
        return std::sqrt(chord) + TuneWeight * std::sqrt(melody) + LoudnessWeight * std::sqrt(level / double(stems.size()));
    };
    std::vector<double> d(n * n);
    std::vector<double> sample;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i; j < n; ++j) {
            d[i * n + j] = d[j * n + i] = distance(i, j);
            if (j > i && (i + j) % 7 == 0)
                sample.push_back(d[i * n + j]);
        }
    const double scale = std::max(1e-6, median(sample));
    std::vector<double> raw(n * n);
    for (size_t k = 0; k < n * n; ++k)
        raw[k] = std::exp(-0.5 * (d[k] / scale) * (d[k] / scale));
    // Along the diagonals, so a passage played again lines up as a whole
    // rather than beat by beat.
    std::vector<double> sim(n * n);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j) {
            double sum = 0;
            int count = 0;
            for (int k = -2; k <= 2; ++k) {
                const long a = long(i) + k, b = long(j) + k;
                if (a >= 0 && b >= 0 && a < long(n) && b < long(n)) {
                    sum += raw[size_t(a) * n + size_t(b)];
                    ++count;
                }
            }
            sim[i * n + j] = sum / count;
        }

    // Novelty: a checkerboard kernel slid down the diagonal scores how much
    // the beats before each one differ from those after it.
    std::vector<double> novelty(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        double score = 0, weight = 0;
        for (int a = -Reach; a < Reach; ++a)
            for (int b = -Reach; b < Reach; ++b) {
                const long x = long(i) + a, y = long(i) + b;
                if (x < 0 || y < 0 || x >= long(n) || y >= long(n))
                    continue;
                const double taper = std::exp(-((a + 0.5) * (a + 0.5) + (b + 0.5) * (b + 0.5)) / (2.0 * (Reach / 2.0) * (Reach / 2.0)));
                const double sign = (a < 0) == (b < 0) ? 1 : -1;
                score += sign * taper * sim[size_t(x) * n + size_t(y)];
                weight += taper;
            }
        novelty[i] = weight > 0 ? std::max(0.0, score / weight) : 0;
    }

    // Structure features (Serra et al., 2014): which earlier and later
    // passages each beat repeats. A verse and a chorus played with the same
    // band at the same level still repeat different things, so where that
    // pattern changes is a boundary the novelty above can miss.
    std::vector<double> repetition(n, 0.0);
    {
        // Four beats at a time, so a beat matches a passage, not a chord.
        const int span = Passage;
        std::vector<double> e(n * n, 0.0);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j) {
                double sum = 0;
                for (int k = 0; k < span; ++k) {
                    const size_t a = std::min(n - 1, i + size_t(k)), b = std::min(n - 1, j + size_t(k));
                    sum += d[a * n + b] * d[a * n + b];
                }
                e[i * n + j] = sum;
            }
        // Each beat's nearest passages, kept only where the liking is mutual.
        const size_t neighbours = std::max<size_t>(3, size_t(std::lround(0.04 * double(n))));
        std::vector<std::vector<char>> near(n, std::vector<char>(n, 0));
        std::vector<size_t> order(n);
        for (size_t i = 0; i < n; ++i) {
            std::iota(order.begin(), order.end(), 0);
            std::partial_sort(order.begin(), order.begin() + long(std::min(n, neighbours + 2 * span)), order.end(),
                              [&](size_t a, size_t b) { return e[i * n + a] < e[i * n + b]; });
            size_t taken = 0;
            for (size_t k = 0; k < n && taken < neighbours; ++k) {
                const size_t j = order[k];
                if ((i > j ? i - j : j - i) < size_t(span))
                    continue;
                near[i][j] = 1;
                ++taken;
            }
        }
        // Lag rows: beat i repeats what's lag beats away, smoothed along time.
        std::vector<double> lag(n * n, 0.0);
        for (size_t i = 0; i < n; ++i)
            for (size_t l = 0; l < n; ++l) {
                const size_t j = (i + l) % n;
                lag[i * n + l] = near[i][j] && near[j][i] ? 1.0 : 0.0;
            }
        const int sigma = 4;
        std::vector<double> kernel;
        for (int k = -2 * sigma; k <= 2 * sigma; ++k)
            kernel.push_back(std::exp(-0.5 * k * k / double(sigma * sigma)));
        std::vector<double> smooth(n * n, 0.0);
        for (size_t i = 0; i < n; ++i)
            for (size_t l = 0; l < n; ++l) {
                double sum = 0, weight = 0;
                for (int k = -2 * sigma; k <= 2 * sigma; ++k) {
                    const long t = long(i) + k;
                    if (t < 0 || t >= long(n))
                        continue;
                    for (int m = -1; m <= 1; ++m) {
                        const size_t ll = (l + n + size_t(m + 1) - 1) % n;
                        const double w = kernel[size_t(k + 2 * sigma)] * (m == 0 ? 1.0 : 0.5);
                        sum += w * lag[size_t(t) * n + ll];
                        weight += w;
                    }
                }
                smooth[i * n + l] = sum / weight;
            }
        for (size_t i = 1; i < n; ++i) {
            double change = 0;
            for (size_t l = 0; l < n; ++l) {
                const double x = smooth[i * n + l] - smooth[(i - 1) * n + l];
                change += x * x;
            }
            repetition[i] = change;
        }
    }
    const double noveltyTop = std::max(1e-9, *std::max_element(novelty.begin() + 1, novelty.end()));
    const double repetitionTop = std::max(1e-9, *std::max_element(repetition.begin(), repetition.end()));

    // Both measures, each scaled to its own peak. A repetition change shows
    // two beats early, since each beat is matched as the start of a
    // four-beat passage.
    std::vector<double> score(n, 0.0);
    for (size_t i = 0; i < n; ++i)
        score[i] = novelty[i] / noveltyTop + (i >= size_t(Passage / 2) ? repetition[i - size_t(Passage / 2)] / repetitionTop : 0.0);

    // Boundaries are the strongest peaks, at least two bars apart.
    double mean = 0, squares = 0;
    for (double v : score)
        mean += v / double(n);
    for (double v : score)
        squares += (v - mean) * (v - mean) / double(n);
    const double threshold = mean + Sensitivity * std::sqrt(squares);
    std::vector<size_t> candidates;
    for (size_t i = Shortest; i + Shortest / 2 < n; ++i) {
        bool peak = score[i] > threshold;
        for (long k = long(i) - Shortest / 2; peak && k <= long(i) + Shortest / 2; ++k)
            if (k >= 0 && k < long(n) && score[size_t(k)] > score[i])
                peak = false;
        if (peak)
            candidates.push_back(i);
    }
    std::sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) { return score[a] > score[b]; });
    std::vector<size_t> bounds{0, n};
    for (size_t c : candidates) {
        bool clear = true;
        for (size_t b : bounds)
            if ((c > b ? c - b : b - c) < size_t(Shortest))
                clear = false;
        if (clear)
            bounds.push_back(c);
    }
    std::sort(bounds.begin(), bounds.end());

    // Where the singer comes in or stops is where a listener hears a section
    // start, to the beat, even when the band carries on as before; the
    // measures above only find the neighbourhood. A boundary within two
    // bars of a clear vocal entry or exit (a bar of silence on one side, a
    // bar of singing on the other, so a breath doesn't count) moves onto it.
    if (vocals >= 0) {
        std::vector<size_t> entries;
        for (size_t i = Passage; i + Passage <= n; ++i) {
            bool before = true, after = true;
            const bool now = isSung(i);
            for (size_t k = 1; k <= size_t(Passage); ++k)
                before = before && isSung(i - k) != now;
            for (size_t k = 0; k < size_t(Passage); ++k)
                after = after && isSung(i + k) == now;
            if (before && after)
                entries.push_back(i);
        }
        for (size_t &b : bounds) {
            if (b == 0 || b == n)
                continue;
            size_t best = b, distance = size_t(Snap) + 1;
            for (size_t e : entries) {
                const size_t d = e > b ? e - b : b - e;
                if (d < distance) {
                    distance = d;
                    best = e;
                }
            }
            b = best;
        }
        std::sort(bounds.begin(), bounds.end());
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    }

    // Group sections that sound alike: compare them beat for beat from their
    // starts (allowing a beat or two of slack), then join the closest pairs
    // until none are close enough.
    const size_t count = bounds.size() - 1;
    std::vector<std::vector<double>> alike(count, std::vector<double>(count, 1.0));
    for (size_t p = 0; p < count; ++p)
        for (size_t q = p + 1; q < count; ++q) {
            const size_t a = bounds[p], b = bounds[q];
            const size_t length = std::min(bounds[p + 1] - a, bounds[q + 1] - b);
            double best = 0;
            for (int shift = -2; shift <= 2; ++shift) {
                double sum = 0;
                int used = 0;
                for (size_t k = 0; k < length; ++k) {
                    const long y = long(b + k) + shift;
                    if (y < 0 || y >= long(n))
                        continue;
                    sum += sim[(a + k) * n + size_t(y)];
                    ++used;
                }
                if (used > 0)
                    best = std::max(best, sum / used);
            }
            // A short section can't stand for a much longer one.
            const double ratio = double(length) / double(std::max(bounds[p + 1] - a, bounds[q + 1] - b));
            alike[p][q] = alike[q][p] = best * std::min(1.0, 0.5 + ratio);
        }
    std::vector<int> cluster(count);
    std::iota(cluster.begin(), cluster.end(), 0);
    for (;;) {
        double best = 0;
        int from = -1, into = -1;
        for (size_t p = 0; p < count; ++p)
            for (size_t q = p + 1; q < count; ++q) {
                if (cluster[p] == cluster[q])
                    continue;
                // Average linkage: how alike the two groups are overall.
                double sum = 0;
                int pairs = 0;
                for (size_t x = 0; x < count; ++x)
                    for (size_t y = 0; y < count; ++y)
                        if (cluster[x] == cluster[p] && cluster[y] == cluster[q]) {
                            sum += alike[x][y];
                            ++pairs;
                        }
                const double link = sum / pairs;
                if (link > best) {
                    best = link;
                    from = cluster[q];
                    into = cluster[p];
                }
            }
        if (from < 0 || best < SameGroup)
            break;
        for (int &c : cluster)
            if (c == from)
                c = into;
    }

    // Neighbours in one group are one section, unless one is sung and the
    // other isn't: an intro over the verse's chords is still an intro.
    struct Part {
        size_t from, to;
        int cluster;
    };
    auto sungShare = [&](const Part &part) {
        int singing = 0;
        for (size_t i = part.from; i < part.to; ++i)
            singing += isSung(i);
        return double(singing) / double(part.to - part.from);
    };
    auto together = [&](const Part &a, const Part &b) {
        return a.cluster == b.cluster && (vocals < 0 || (sungShare(a) > 0.5) == (sungShare(b) > 0.5));
    };
    std::vector<Part> parts;
    for (size_t p = 0; p < count; ++p) {
        const Part next{bounds[p], bounds[p + 1], cluster[p]};
        if (!parts.empty() && together(parts.back(), next))
            parts.back().to = next.to;
        else
            parts.push_back(next);
    }

    // A part shorter than three bars is a fragment (a pickup, a fill, the
    // last line of a chorus), not a section: it joins the neighbour it's
    // more like. Except a short unsung break between two sung parts, which
    // is a real gap a player wants to see.
    auto likeness = [&](const Part &a, const Part &b) {
        double sum = 0;
        for (size_t i = a.from; i < a.to; ++i)
            for (size_t j = b.from; j < b.to; ++j)
                sum += sim[i * n + j];
        return sum / double((a.to - a.from) * (b.to - b.from));
    };
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t p = 0; p < parts.size() && parts.size() > 1; ++p) {
            if (parts[p].to - parts[p].from >= size_t(Fragment))
                continue;
            const bool before = p > 0, after = p + 1 < parts.size();
            const bool sung = vocals >= 0 && sungShare(parts[p]) > 0.5;
            if (!sung && vocals >= 0 && before && after && sungShare(parts[p - 1]) > 0.5 && sungShare(parts[p + 1]) > 0.5)
                continue;
            const bool intoPrevious = !after || (before && likeness(parts[p], parts[p - 1]) >= likeness(parts[p], parts[p + 1]));
            if (intoPrevious)
                parts[p - 1].to = parts[p].to;
            else
                parts[p + 1].from = parts[p].from;
            parts.erase(parts.begin() + long(p));
            // Neighbours that now share a group are one section.
            for (size_t q = 1; q < parts.size();) {
                if (together(parts[q - 1], parts[q])) {
                    parts[q - 1].to = parts[q].to;
                    parts.erase(parts.begin() + long(q));
                } else {
                    ++q;
                }
            }
            merged = true;
            break;
        }
    }

    // What each part holds: whether there's singing, and how loud it is.
    const size_t partCount = parts.size();
    std::vector<bool> sung(partCount, vocals < 0);
    std::vector<double> energy(partCount, 0);
    for (size_t p = 0; p < partCount; ++p) {
        int singing = 0;
        for (size_t i = parts[p].from; i < parts[p].to; ++i) {
            double total = 0;
            for (size_t s = 0; s < stems.size(); ++s)
                total += std::pow(10.0, 2 * loud[i][s]);
            energy[p] += std::log10(total + 1e-8) / double(parts[p].to - parts[p].from);
            if (isSung(i))
                ++singing;
        }
        if (vocals >= 0)
            sung[p] = singing > 0.5 * double(parts[p].to - parts[p].from);
    }

    std::vector<QString> kind(partCount);
    // Unsung at the ends: intro and outro; unsung in between: instrumental.
    size_t firstSung = partCount, lastSung = 0;
    for (size_t p = 0; p < partCount; ++p)
        if (sung[p]) {
            firstSung = std::min(firstSung, p);
            lastSung = p;
        }
    for (size_t p = 0; p < partCount; ++p)
        if (!sung[p])
            kind[p] = p < firstSung ? "intro" : p > lastSung ? "outro" : "instrumental";

    // The sung groups: the loudest of those that come back is the chorus;
    // a group that always leads straight into it is the pre-chorus; other
    // groups that come back are verses; a sung part heard once, after the
    // first chorus, is the bridge.
    struct Group {
        int cluster;
        int times = 0;
        double energy = 0;
    };
    std::vector<Group> groups;
    for (size_t p = 0; p < partCount; ++p) {
        if (!sung[p])
            continue;
        auto it = std::find_if(groups.begin(), groups.end(), [&](const Group &g) { return g.cluster == parts[p].cluster; });
        if (it == groups.end())
            it = groups.insert(groups.end(), Group{parts[p].cluster});
        it->energy += energy[p];
        ++it->times;
    }
    // Groups are in order of first appearance. The first sung group to
    // come back is the verse; the chorus is the loudest of the others that
    // come back. (Loudness alone can't tell them apart when the band plays
    // both full out.)
    int chorus = -1, verse = -1;
    double loudest = -1e9;
    for (auto &g : groups) {
        g.energy /= g.times;
        if (g.times < 2)
            continue;
        if (verse < 0)
            verse = g.cluster;
        else if (g.energy > loudest) {
            loudest = g.energy;
            chorus = g.cluster;
        }
    }
    if (chorus < 0 && verse >= 0) {
        // Only one group comes back: the chorus if it's louder than the rest
        // of the singing, otherwise the verse.
        loudest = groups[size_t(std::find_if(groups.begin(), groups.end(), [&](const Group &g) { return g.cluster == verse; }) - groups.begin())].energy;
        chorus = verse;
    }
    int repeating = 0;
    double others = 0;
    int otherCount = 0;
    for (const auto &g : groups) {
        if (g.times >= 2)
            ++repeating;
        else {
            others += g.energy;
            ++otherCount;
        }
    }
    if (repeating == 1 && (otherCount == 0 || loudest <= others / otherCount))
        chorus = -1;
    else if (repeating == 1)
        verse = -1;
    size_t firstChorus = partCount;
    for (size_t p = 0; p < partCount; ++p)
        if (sung[p] && parts[p].cluster == chorus) {
            firstChorus = p;
            break;
        }
    for (const auto &g : groups) {
        bool leadsIn = g.times >= 2 && g.cluster != chorus && g.cluster != verse && chorus >= 0;
        for (size_t p = 0; leadsIn && p < partCount; ++p)
            if (parts[p].cluster == g.cluster && sung[p] && (p + 1 >= partCount || parts[p + 1].cluster != chorus))
                leadsIn = false;
        for (size_t p = 0; p < partCount; ++p) {
            if (!sung[p] || parts[p].cluster != g.cluster)
                continue;
            if (g.cluster == chorus)
                kind[p] = "chorus";
            else if (leadsIn)
                kind[p] = "prechorus";
            else if (g.times == 1 && p > firstChorus && firstChorus < partCount)
                kind[p] = "bridge";
            else
                kind[p] = "verse";
        }
    }

    std::vector<Section> sections;
    std::vector<int> order;
    for (size_t p = 0; p < partCount; ++p) {
        auto at = std::find(order.begin(), order.end(), parts[p].cluster);
        if (at == order.end())
            at = order.insert(order.end(), parts[p].cluster);
        Section s;
        s.start = grid[parts[p].from];
        s.end = grid[parts[p].to];
        s.kind = kind[p];
        s.group = int(at - order.begin());
        sections.push_back(s);
    }
    // Adjacent parts that got the same name read as one section; for the
    // unsung ones (two kinds of outro in a row) even when they sound unalike.
    std::vector<Section> merged;
    for (const auto &s : sections) {
        const bool unsung = s.kind == QLatin1String("intro") || s.kind == QLatin1String("outro")
            || s.kind == QLatin1String("instrumental");
        if (!merged.empty() && merged.back().kind == s.kind && (merged.back().group == s.group || unsung))
            merged.back().end = s.end;
        else
            merged.push_back(s);
    }
    return merged;
}

}  // namespace Structure
