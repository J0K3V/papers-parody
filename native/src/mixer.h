// Soundtrack mixing.
//
// Everything is summed into one float buffer rather than layered clip by clip.
// A forty second report carries a few hundred typing clicks, and copying the
// whole track once per click is far too slow to sit through before a preview
// starts.
//
// Music is laid down per run of scenes, so a track that carries across several
// of them plays straight through and only loops if the scenes outlast it.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "timeline.h"

namespace parody {

constexpr int SAMPLE_RATE = 44100;
constexpr int CHANNELS = 2;

// How much audio is handed to the sound card at a time, and the size raylib's
// own buffers are set to at startup. Its default of 2048 suits a wired card but
// is too small for Bluetooth: every buffer reads as free, everything written is
// thrown away, and the music never starts - while the shutter, which is a plain
// Sound rather than a stream, plays perfectly.
constexpr int STREAM_FRAMES = 4096;

// Interleaved stereo, -1 to 1.
struct Clip {
    std::vector<float> samples;
    int frames() const { return int(samples.size()) / CHANNELS; }
    double seconds() const { return double(frames()) / SAMPLE_RATE; }
    bool empty() const { return samples.empty(); }
};

// Which file plays in each of the three roles, plus the music.
struct SoundPack {
    std::vector<std::string> music;
    std::vector<std::string> next;
    std::vector<std::string> letter;
};

const std::unordered_map<std::string, SoundPack>& sound_packs();
const char* default_pack();

class Mixer {
  public:
    void set_sounds_dir(const std::string& dir) { sounds_ = dir; }
    const std::string& sounds_dir() const { return sounds_; }

    // Decodes on first use and keeps the samples, so a second report that uses
    // the same music does not pay for it again.
    const Clip* load(const std::string& name);
    void forget();

    // Anything this long counts as a score rather than an effect.
    bool is_music(const std::string& name);

    // Build the whole soundtrack for a report.
    Clip mix(const Timeline& timeline, const std::string& pack_name, bool music_only);

    static bool write_wav(const Clip& clip, const std::string& path);

  private:
    void add(Clip& track, const Clip& clip, int at_sample, float gain) const;
    void loop_range(Clip& track, const Clip& clip, int start, int end, int fade) const;

    std::string sounds_;
    std::unordered_map<std::string, Clip> cache_;
};

}  // namespace parody
