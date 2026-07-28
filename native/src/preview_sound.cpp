#include "preview_sound.h"

#include <algorithm>

namespace parody {
namespace {

// The same block the player uses, so both match the buffers raylib was told to
// allocate at startup.
constexpr int BLOCK_FRAMES = STREAM_FRAMES;

}  // namespace

SoundPreview::~SoundPreview() { stop(); }

void SoundPreview::play(const Clip& clip) {
    stop();
    if (clip.empty() || !IsAudioDeviceReady()) return;

    stream_ = LoadAudioStream(SAMPLE_RATE, 32, CHANNELS);
    ready_ = true;
    paused_ = false;
    clip_ = clip;
    cursor_ = 0;
    PlayAudioStream(stream_);
}

void SoundPreview::stop() {
    if (!ready_) return;
    StopAudioStream(stream_);
    UnloadAudioStream(stream_);
    ready_ = false;
    paused_ = false;
    clip_ = Clip{};
    cursor_ = 0;
}

void SoundPreview::toggle_pause() {
    if (!ready_) return;
    paused_ = !paused_;
    if (paused_) {
        PauseAudioStream(stream_);
    } else {
        ResumeAudioStream(stream_);
    }
}

float SoundPreview::progress() const {
    if (!ready_ || clip_.empty()) return 0.0f;
    return std::min(1.0f, float(cursor_) / float(clip_.frames()));
}

void SoundPreview::update() {
    if (!ready_ || clip_.empty() || paused_) return;

    if (cursor_ >= clip_.frames()) {
        stop();
        return;
    }
    if (!IsAudioStreamProcessed(stream_)) return;

    static std::vector<float> block;
    block.assign(size_t(BLOCK_FRAMES) * CHANNELS, 0.0f);

    const int total = clip_.frames();
    const int count = std::min(BLOCK_FRAMES, total - cursor_);
    for (int frame = 0; frame < count; ++frame) {
        for (int channel = 0; channel < CHANNELS; ++channel) {
            block[size_t(frame) * CHANNELS + channel] =
                clip_.samples[size_t(cursor_ + frame) * CHANNELS + channel];
        }
    }

    UpdateAudioStream(stream_, block.data(), BLOCK_FRAMES);
    cursor_ += BLOCK_FRAMES;
}

}  // namespace parody
