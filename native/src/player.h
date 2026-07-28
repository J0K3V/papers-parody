// Playing a report back, live.
//
// The score and the effects are kept apart, because they answer to different
// clocks. The music is put on its own stream at PLAY and never touched again:
// it runs on real time, loops when it reaches the end, and carries straight
// through however long a scene sits waiting on its NEXT button - the way it
// does in the game, where the music has nothing to do with the paperwork. The
// effects belong to the report instead, and are fired one at a time as the
// picture reaches each of them.
//
// Mixing the two into one buffer, as the exported video does, cannot work here:
// every press of NEXT would have to restart the stream, and the score would
// stutter at every scene.

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mixer.h"
#include "raylib.h"
#include "timeline.h"

namespace parody {

class Player {
  public:
    ~Player();

    // Mix the soundtrack and get ready. Returns false if there is nothing to play.
    bool prepare(Mixer& mixer, const Timeline& timeline, const Project& project);

    // Take up an edited report without interrupting anything that has not
    // actually changed. The picture follows the new timing at once; the score
    // only restarts if this part of the report now calls for a different piece.
    void refresh(Mixer& mixer, const Timeline& timeline, const Project& project);

    void play(int from_frame = 0);
    void stop();
    void toggle_pause();

    // Give back everything raylib owns. Must be called before the audio device
    // is closed: unloading a Sound or a stream afterwards goes through a device
    // that is no longer there.
    void release();

    // Press the button the report is waiting on. Returns true if that was the
    // closing one, which winds everything back to the start.
    bool press_next();

    // Jump the picture to a frame while it is playing. The score is left alone
    // unless the new place calls for a different piece, so picking through the
    // scenes does not keep chopping the music up.
    void seek(int frame);

    void update();          // call once a frame

    // Fills a block of the score. Called by raylib on the audio thread, not by
    // the main loop: dragging or resizing a window on Windows blocks the loop
    // inside the system's own message handling, and music pushed from there
    // breaks up every time the window is touched.
    void fill(void* buffer, unsigned int frames);

    bool playing() const { return playing_; }
    bool paused() const { return paused_; }
    bool waiting() const { return waiting_; }
    bool finished() const { return finished_; }
    int frame() const { return frame_; }

    // How much audio has been handed to the sound card since playback started.
    // If this falls behind the wall clock the score is stuttering.
    long long fed_frames() const { return fed_; }

    // Whether the sound card is actually playing the stream, as opposed to the
    // player merely believing it is.
    bool stream_playing() const { return stream_ready_ && IsAudioStreamPlaying(stream_); }

    // How long the piece playing now runs before it comes round again.
    double music_seconds() const { return music_ ? music_->seconds() : 0.0; }

    void set_hover(bool over) { hover_ = over; }
    bool hover() const { return hover_; }

    // The volume sliders beside the stage. The score's gain is read on the
    // audio thread, so it is atomic; effects fire from the main thread. Neither
    // touches an export - a render is mixed at full level regardless.
    void set_music_volume(float gain) { music_gain_.store(gain, std::memory_order_relaxed); }
    void set_effects_volume(float gain);

    const Timeline* timeline() const { return timeline_; }

  private:
    void start_stream(int from_frame);
    void stop_stream();

    // Fire every effect the picture has passed since the last frame.
    void fire_cues(int from_frame, int to_frame);
    void play_effect(const std::string& file, float volume);
    void forget_effects();

    // Pick the stretch of music the picture is currently inside.
    int run_at(int frame) const;
    void switch_music(int run, int from_frame);
    void build_runs(Mixer& mixer, const Timeline& timeline);

    const Timeline* timeline_ = nullptr;
    std::string sounds_dir_;
    SoundPack pack_;

    // One entry per stretch of the report that shares a piece of music, each
    // holding that music at its own full length rather than trimmed to fit the
    // report. Trimming is what made a ninety second track loop at thirty.
    // Shared rather than owned outright: the audio thread reads whichever one
    // is playing, and an edit that rebuilds this list must not pull the memory
    // out from under it.
    std::vector<std::shared_ptr<Clip>> run_music_;
    std::vector<int> run_start_;
    std::vector<std::vector<std::string>> run_tracks_;   // to tell an edit that changes the music
    int current_run_ = -1;

    // What the audio thread is reading. Only ever swapped while the stream is
    // stopped, so the callback never sees it change under itself.
    std::shared_ptr<Clip> music_;

    AudioStream stream_{};
    bool stream_ready_ = false;

    // Written by the audio thread, read by the editor for its read-outs.
    std::atomic<int> cursor_{0};        // where the score has been read up to
    std::atomic<long long> fed_{0};     // total handed over since it started

    // One-shot effects, kept loaded so a typing click costs nothing to repeat.
    // Each remembers the level it was fired at, so a slider moved while one is
    // still ringing can land on it rather than only on the next.
    struct Effect {
        Sound sound{};
        float level = 1.0f;
    };
    std::unordered_map<std::string, Effect> effects_;

    std::atomic<float> music_gain_{1.0f};
    float effects_gain_ = 1.0f;


    bool playing_ = false;
    bool paused_ = false;
    bool waiting_ = false;
    bool finished_ = false;
    bool hover_ = false;
    bool movie_mode_ = true;

    int frame_ = 0;
    int fired_to_ = 0;      // every cue up to here has already been played
    int started_at_frame_ = 0;
    double started_at_time_ = 0.0;
    std::unordered_set<int> cleared_;
};

}  // namespace parody
