#include "mixer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "raylib.h"

namespace parody {
namespace {

// Anything this long is treated as a score rather than an effect, so the two
// pickers stay tidy. Nothing is forbidden either way.
constexpr double MUSIC_MIN_SECONDS = 20.0;

std::mt19937& die() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

// raylib decodes wav, ogg, mp3 and flac, and hands back whatever the file had.
// Everything here is normalised to interleaved stereo floats at 44100, because
// the game's own files are not consistent: the page-turn click is mono and the
// detention theme runs at 48 kHz.
Clip to_stereo_44100(const Wave& wave) {
    Clip clip;
    if (wave.frameCount == 0 || wave.channels == 0) return clip;

    float* source = LoadWaveSamples(wave);  // always float, still at the wave's rate
    if (source == nullptr) return clip;

    const long long in_frames = wave.frameCount;
    const unsigned int in_channels = wave.channels;

    // The soundtrack files decode at 48 kHz and the effects at 44.1, so this
    // is a real resample, not a formality. Truncating the source index instead
    // of interpolating drifts the music away from the reference mix.
    const double ratio = double(wave.sampleRate) / double(SAMPLE_RATE);
    const long long out_frames = (long long)std::floor(in_frames / ratio);

    clip.samples.assign(size_t(out_frames) * CHANNELS, 0.0f);

    for (long long frame = 0; frame < out_frames; ++frame) {
        const double at = frame * ratio;
        const long long left_index = (long long)at;
        const long long right_index = std::min(in_frames - 1, left_index + 1);
        const float blend = float(at - double(left_index));

        for (int channel = 0; channel < CHANNELS; ++channel) {
            const unsigned int pick = in_channels == 1 ? 0u : (unsigned int)channel;
            const float a = source[size_t(left_index) * in_channels + pick];
            const float b = source[size_t(right_index) * in_channels + pick];
            clip.samples[size_t(frame) * CHANNELS + channel] = a + (b - a) * blend;
        }
    }

    UnloadWaveSamples(source);
    return clip;
}

// The lead-in silence on these mp3s is deliberately left alone.
//
// It is tempting to trim it: raylib's decoder keeps the encoder padding that
// the format says to discard. But the reference renderer keeps it too, and the
// scene timings were built against that. Cutting it here moves the whole score
// three tenths of a second early against every video already made.
//
// Both decoders produce the same recording - the loudness curves match to
// within a thousandth once aligned - so the only thing that matters is that
// they agree on where it starts. They do, as long as nothing is trimmed.

}  // namespace

const std::unordered_map<std::string, SoundPack>& sound_packs() {
    static const std::vector<std::string> clicks = {"TextReveal0.wav", "TextReveal1.wav",
                                                    "TextReveal2.wav", "TextReveal3.wav"};
    static const std::string page_turn = "ButtonUp.wav";

    // The track the original generator shipped as Death.wav turned out to be
    // the bad ending itself, sample for sample, so there is one copy of it.
    static const std::unordered_map<std::string, SoundPack> packs = {
        {"bad ending", {{"BadEnding.mp3"}, {page_turn}, clicks}},
        {"good ending", {{"GoodEnding.mp3"}, {page_turn}, clicks}},
        {"main theme", {{"MainTheme.mp3"}, {page_turn}, clicks}},
        // There were four. "detention" was the main theme with two effects laid
        // over it, which is something a scene can do for itself.
    };
    return packs;
}

const char* default_pack() { return "bad ending"; }

const Clip* Mixer::load(const std::string& name) {
    auto found = cache_.find(name);
    if (found != cache_.end()) return &found->second;

    // A pack's own effects are bare filenames living in the sounds folder, but
    // a scene stores its sound as a whole path - including one pointing at an
    // imported file outside that folder. Gluing the folder onto a path that is
    // already complete produced a name nothing could open, and the sound went
    // missing from the video without a word.
    const std::string path = FileExists(name.c_str()) ? name : sounds_ + "/" + name;
    Wave wave = LoadWave(path.c_str());
    Clip clip = to_stereo_44100(wave);
    UnloadWave(wave);

    // A file that was not there is not remembered as silent. Importing the
    // missing sound and trying again would otherwise keep giving back the empty
    // clip from the first attempt, for as long as the editor stayed open.
    if (clip.empty()) {
        static const Clip nothing;
        return &nothing;
    }

    return &(cache_[name] = std::move(clip));
}

void Mixer::forget() { cache_.clear(); }

bool Mixer::is_music(const std::string& name) {
    // By file size, not by decoding. Asking the old way meant decoding every
    // sound in the folder just to open a picker - four megabytes of mp3 apiece
    // for the scores, and a wait long enough to look like the app had hung.
    // Anything of this size is a piece of music: the longest effect in the game
    // is a third of a megabyte, and the shortest score is three.
    constexpr long long MUSIC_MIN_BYTES = 1024 * 1024;

    for (const auto& pair : sound_packs()) {
        for (const std::string& track : pair.second.music) {
            if (track == name) return true;
        }
    }

    const std::string path = FileExists(name.c_str()) ? name : sounds_ + "/" + name;
    return FileExists(path.c_str()) && GetFileLength(path.c_str()) >= MUSIC_MIN_BYTES;
}

void Mixer::add(Clip& track, const Clip& clip, int at_sample, float gain) const {
    if (clip.empty()) return;

    const int track_frames = track.frames();
    if (at_sample >= track_frames) return;

    const int start = std::max(0, at_sample);
    const int offset = start - at_sample;
    const int length = std::min(clip.frames() - offset, track_frames - start);
    if (length <= 0) return;

    for (int frame = 0; frame < length; ++frame) {
        for (int channel = 0; channel < CHANNELS; ++channel) {
            track.samples[size_t(start + frame) * CHANNELS + channel] +=
                clip.samples[size_t(offset + frame) * CHANNELS + channel] * gain;
        }
    }
}

void Mixer::loop_range(Clip& track, const Clip& clip, int start, int end, int fade) const {
    if (clip.empty()) return;

    start = std::max(0, start);
    end = std::min(track.frames(), end);
    if (end <= start) return;

    const int span = end - start;
    const int clip_frames = clip.frames();

    for (int frame = 0; frame < span; ++frame) {
        // Only restarted when the track genuinely runs out, never at a scene
        // boundary, so nothing stutters halfway through the report.
        const int from = frame % clip_frames;

        float level = 1.0f;
        if (fade > 0) {
            const int edge = std::min(fade, span / 2);
            if (edge > 0) {
                if (frame < edge) level = float(frame) / edge;
                else if (frame >= span - edge) level = float(span - frame) / edge;
            }
        }

        for (int channel = 0; channel < CHANNELS; ++channel) {
            track.samples[size_t(start + frame) * CHANNELS + channel] +=
                clip.samples[size_t(from) * CHANNELS + channel] * level;
        }
    }
}

Clip Mixer::mix(const Timeline& timeline, const std::string& pack_name, bool music_only) {
    const auto& packs = sound_packs();
    auto found = packs.find(pack_name);
    const SoundPack& pack = found != packs.end() ? found->second : packs.at(default_pack());

    Clip track;
    track.samples.assign(size_t(timeline.duration() * SAMPLE_RATE) * CHANNELS, 0.0f);

    const std::vector<MusicRun> runs = timeline.music_runs(pack.music);
    const int fade = runs.size() > 1 ? int(layout::MUSIC_CROSSFADE * SAMPLE_RATE) : 0;

    for (const MusicRun& run : runs) {
        // Some packs point at soundtrack files that have to be imported first.
        std::vector<std::string> playable;
        for (const std::string& name : run.tracks) {
            if (!load(name)->empty()) playable.push_back(name);
        }
        if (playable.empty()) playable = packs.at(default_pack()).music;

        const int start = int(double(SAMPLE_RATE) * run.start / layout::FPS);
        const int end = int(double(SAMPLE_RATE) * run.end / layout::FPS);
        for (const std::string& name : playable) {
            loop_range(track, *load(name), start, end, fade);
        }
    }

    if (!music_only) {
        std::uniform_int_distribution<size_t> pick(0, pack.letter.size() - 1);

        for (const Cue& cue : timeline.cues()) {
            const int at = int(double(SAMPLE_RATE) * cue.frame / layout::FPS);

            switch (cue.kind) {
                case Cue::Kind::SceneSound:
                    add(track, *load(cue.file), at, 1.0f);
                    break;
                case Cue::Kind::Letter:
                    add(track, *load(pack.letter[pick(die())]), at, layout::LETTER_GAIN);
                    break;
                case Cue::Kind::Next:
                    if (!pack.next.empty()) add(track, *load(pack.next[0]), at, 1.0f);
                    break;
            }
        }
    }

    // Taper the last second to silence.
    const int tail = std::min(SAMPLE_RATE, track.frames());
    for (int frame = 0; frame < tail; ++frame) {
        const float level = float(tail - frame) / tail;
        const int at = track.frames() - tail + frame;
        for (int channel = 0; channel < CHANNELS; ++channel) {
            track.samples[size_t(at) * CHANNELS + channel] *= level;
        }
    }

    return track;
}

bool Mixer::write_wav(const Clip& clip, const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;

    const uint32_t data_bytes = uint32_t(clip.samples.size() * sizeof(int16_t));
    const uint32_t byte_rate = SAMPLE_RATE * CHANNELS * 2;

    auto put32 = [&](uint32_t value) { std::fwrite(&value, 4, 1, file); };
    auto put16 = [&](uint16_t value) { std::fwrite(&value, 2, 1, file); };

    std::fwrite("RIFF", 1, 4, file);
    put32(36 + data_bytes);
    std::fwrite("WAVEfmt ", 1, 8, file);
    put32(16);
    put16(1);                 // PCM
    put16(CHANNELS);
    put32(SAMPLE_RATE);
    put32(byte_rate);
    put16(CHANNELS * 2);      // block align
    put16(16);                // bits per sample
    std::fwrite("data", 1, 4, file);
    put32(data_bytes);

    std::vector<int16_t> pcm(clip.samples.size());
    for (size_t i = 0; i < clip.samples.size(); ++i) {
        const float value = std::max(-1.0f, std::min(1.0f, clip.samples[i]));
        pcm[i] = int16_t(std::lround(value * 32767.0f));
    }
    std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), file);

    std::fclose(file);
    return true;
}

}  // namespace parody
