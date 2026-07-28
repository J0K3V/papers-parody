// When everything happens, in frames.
//
// The editor, the live player and the mp4 exporter all walk this same object,
// so what you watch in the window is what ends up in the file.

#pragma once

#include <string>
#include <vector>

#include "layout.h"

namespace parody {

// When a scene's one-off sound fires.
enum class SoundAt {
    Start,      // the moment the scene begins
    Typed,      // as the line finishes being written
    End,        // as the scene gives way to the next
    Delayed,    // sound_delay seconds after the scene begins
};

struct Scene {
    std::string image;   // path, relative to the app folder where it can be
    std::string text;
    std::string sound;   // one-off effect somewhere in this scene, may be empty
    std::string music;   // track that starts here and plays on, may be empty
    layout::Align align = layout::Align::Left;

    SoundAt sound_at = SoundAt::Start;
    float sound_delay = 0.0f;   // seconds, only used when sound_at is Delayed
};

struct Project {
    std::vector<Scene> scenes;
    std::string soundtrack = "bad ending";
    std::string fit = "contain";
    std::string typing = "normal";
    std::string next_button = "on";
    std::string output = "my-parody.mp4";
    bool movie_mode = true;
};

// A cue is one sound at one moment.
struct Cue {
    enum class Kind { Letter, Next, SceneSound };
    Kind kind;
    int frame;
    std::string file;   // only for SceneSound
};

// One scene's slot in the running order.
struct Entry {
    int index = 0;
    std::string text;
    layout::Align align = layout::Align::Left;
    std::string music;

    int start = 0;      // first frame of the scene
    int wipe_end = 0;   // the picture has finished sliding in
    int pause_end = 0;  // the beat of stillness is over, typing begins
    int type_end = 0;   // the line is fully written
    int hold_end = 0;   // it has been held long enough
    int end = 0;        // length of the whole scene, from start
    bool is_last = false;
};

// A stretch of the report that shares one piece of music.
struct MusicRun {
    std::vector<std::string> tracks;
    int start = 0;
    int end = 0;
};

// What a single frame needs drawn on it.
struct FrameState {
    const Entry* entry = nullptr;
    float hidden = 0.0f;      // 1 hides the picture, 0 shows all of it
    std::string text;         // as much of the line as has been typed
    layout::Align align = layout::Align::Left;
    const char* button = nullptr;   // nullptr, "idle" or "press"
    float cursor = -1.0f;           // how far the hand has travelled, -1 for none
};

class Timeline {
  public:
    Timeline() = default;
    Timeline(const std::vector<Scene>& scenes, const std::string& typing_speed,
             bool show_button, bool interactive);

    const std::vector<Entry>& entries() const { return entries_; }
    const std::vector<Cue>& cues() const { return cues_; }
    int total_frames() const { return total_frames_; }
    double duration() const { return double(total_frames_) / layout::FPS; }
    bool show_button() const { return show_button_; }

    const Entry* entry_at(int frame) const;
    FrameState state_at(int frame) const;

    // Scenes in a row asking for the same track are merged, so the score plays
    // straight through instead of restarting at every scene change.
    std::vector<MusicRun> music_runs(const std::vector<std::string>& fallback) const;

  private:
    std::vector<Entry> entries_;
    std::vector<Cue> cues_;
    int total_frames_ = 0;
    int step_ = 2;
    int click_step_ = 3;
    bool show_button_ = true;
    bool interactive_ = false;
};

}  // namespace parody
