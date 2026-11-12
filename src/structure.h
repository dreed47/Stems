#pragma once

#include <QString>
#include <vector>

// Finds a song's sections (intro, verse, chorus, bridge…) from its stems.
//
// Every beat is described by the chords the non-drum stems play and by how
// loud each stem is, so a section where the vocals drop out or the drums
// come in reads as different even over the same chords. Comparing every
// beat with every other gives a self-similarity matrix: boundaries are
// where the music before stops resembling the music after (Foote's
// novelty), and sections that resemble each other are grouped. The names
// come from the stems: which groups have vocals, which repeat, which is
// loudest.
namespace Structure {

struct Stem {
    QString name;  // as Demucs names them: vocals, drums, bass, other…
    const qint16 *pcm = nullptr;  // interleaved 16-bit stereo
    qint64 frames = 0;
};

struct Section {
    double start = 0, end = 0;  // seconds
    // intro, verse, prechorus, chorus, bridge, instrumental or outro
    QString kind;
    // Sections in one group sound alike; numbered from 0 in order of first
    // appearance.
    int group = 0;
};

// The beats come from Tempo::detect; with too few, the song is cut into
// half seconds instead. Empty for a song too short or quiet to divide.
std::vector<Section> analyse(const std::vector<Stem> &stems, qint64 frames, int sampleRate,
                             const std::vector<double> &beats);

}  // namespace Structure
