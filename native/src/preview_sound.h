// Playing a single sound so you can hear it before choosing it.
//
// Separate from the report's own stream: this one is short, interruptible and
// never part of the timeline.

#pragma once

#include "mixer.h"
#include "raylib.h"

namespace parody {

class SoundPreview {
  public:
    ~SoundPreview();

    void play(const Clip& clip);
    void update();
    void stop();
    void toggle_pause();

    bool playing() const { return ready_; }
    bool paused() const { return paused_; }

    // How far through, for a progress read-out. 0 when nothing is playing.
    float progress() const;

  private:
    AudioStream stream_{};
    bool ready_ = false;
    bool paused_ = false;

    // A copy, so this owns what it is playing. The mixer's cache outlives any
    // one preview today, but nothing says it has to: forget() empties it, and
    // a preview reading from a clip it does not own would be reading freed
    // memory the moment that happened.
    Clip clip_;
    int cursor_ = 0;
};

}  // namespace parody
