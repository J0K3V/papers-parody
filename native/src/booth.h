// The booth wall and its shutter.
//
// The stage sits inside the booth: the wall is always there behind the
// picture, and the metal shutter rattles down over it when the window is busy
// - rendering a video, or opening something new - then lifts again.
//
// Both are drawn once, as a frame a little larger than the video, rather than
// repeated across the panel. One booth reads as a booth; nine of them tiled
// side by side read as wallpaper, and the bars stop lining up with the picture.

#pragma once

#include <string>

#include "raylib.h"

namespace parody {

class Booth {
  public:
    void load(const std::string& sprites_dir);
    void unload();

    void update();

    // The rattle counts as an effect, so the effects slider reaches it too.
    void set_volume(float gain);

    void close();               // rattle it down
    void open();                // lift it back up
    void close_instantly();     // already down when the window appears
    void open_after(double seconds);
    void reveal();              // drop it and lift it, to show something new
    void pull_lever();          // whichever way it is, send it the other way

    // `stage` is where the video itself lands. The booth is drawn around it,
    // slightly larger, and clipped to `area` so it never escapes the panel.
    void draw_wall(Rectangle area, Rectangle stage) const;
    void draw_shutter(Rectangle area, Rectangle stage) const;

    // The handle in the corner, drawn over the shutter the way it hangs in
    // front of the bars in the game. Give it whether the pointer is on it.
    void draw_lever(Rectangle area, Rectangle stage, bool hovered) const;

    // Where that handle is, so the editor can tell when it has been clicked.
    static Rectangle lever_box(Rectangle area, Rectangle stage);

    // The booth around a given stage: a margin on every side, kept inside the
    // panel. Public so the editor can leave room for it.
    static Rectangle frame_for(Rectangle area, Rectangle stage);

    bool closed() const { return offset_ >= 0.999f; }
    bool moving() const { return moving_; }

  private:
    void play(const char* file);

    Texture2D wall_{};
    Texture2D slats_{};
    Texture2D lever_up_{};
    Texture2D lever_down_{};
    std::string sounds_;

    // The rattle is held rather than left to raylib. Loading a Sound and
    // walking away leaks a voice on every drop, and the audio device only has
    // so many before it stops handing them out.
    Sound rattle_{};
    bool rattle_ready_ = false;
    float volume_ = 1.0f;

    // 0 is fully up and out of sight, 1 is fully down.
    float offset_ = 0.0f;
    float target_ = 0.0f;
    bool moving_ = false;
    double open_at_ = 0.0;
    bool reveal_pending_ = false;
};

}  // namespace parody
