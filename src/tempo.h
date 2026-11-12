#pragma once

#include <QtGlobal>
#include <utility>
#include <vector>

// Finds a song's tempo and beats from its audio, and plans a count-in. The
// drum stem is the best input: with nothing else in it, every hit stands out.
namespace Tempo {

struct Grid {
    double bpm = 0;
    // Where each beat falls, in seconds from the start of the song.
    std::vector<double> beats;
    bool valid() const { return bpm > 0 && beats.size() >= 2; }
};

// Interleaved 16-bit stereo, as the mixer holds it; several sources are
// summed (a song without a drum stem is analysed as the whole mix).
using Source = std::pair<const qint16 *, qint64>;

// Estimates the tempo, then tracks the beats with dynamic programming
// (Ellis, "Beat Tracking by Dynamic Programming", 2007). An empty Grid
// means there was no steady pulse to find.
Grid detect(const std::vector<Source> &sources, qint64 frames, int sampleRate);

// The tempo of a run of taps, in seconds on any steady clock; 0 with fewer
// than two.
double tappedBpm(const std::vector<double> &taps);

struct CountIn {
    double start = 0;     // where the song starts playing, in seconds
    double preroll = 0;   // how long before that the first click sounds
    double interval = 0;  // seconds between clicks
    bool valid() const { return interval > 0; }
};

// Where the song's first note begins, in seconds, across every stem; -1
// for a silent song.
double firstSound(const std::vector<Source> &sources, int sampleRate);

// Counts `count` beats in to the beat at (or just before) the playhead, so
// the song starts on a beat. With no beats but a tempo (tapped while
// stopped), it counts in to the playhead itself. Before the first beat or
// after the last, the grid carries on at the tempo there. Playing from
// before `firstSound` counts in to that first note instead.
CountIn plan(const std::vector<double> &beats, double bpm, double playhead, int count, double firstSound = -1);

}  // namespace Tempo
