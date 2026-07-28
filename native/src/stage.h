// Drawing a frame: the picture, the report text, the NEXT button, the pointer.
//
// Everything is drawn into an off-screen 1280x720 target, which is then shown
// at whatever size the window happens to be. That keeps the picture identical
// whether it is being previewed in a small window or written to a file, and it
// is why scaling the window costs nothing: the GPU does it.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"
#include "timeline.h"

namespace parody {

// A picture already fitted into the 480x320 box the renderer draws.
struct Picture {
    Texture2D texture{};
    bool ready = false;
};

class Stage {
  public:
    ~Stage();

    void load(const std::string& assets_dir);
    void unload();

    // Fit a picture into the box, letterboxed or stretched, and keep it.
    const Picture* picture(const std::string& path, const std::string& fit_mode);
    void forget_pictures();

    // Split a line into the rows it needs inside the text column.
    std::vector<std::string> wrap(const std::string& text) const;
    float text_width(const std::string& text) const;

    // Draw one frame of a report into the off-screen target.
    void draw(const FrameState& state, const Picture* art, bool waiting, bool hover,
              Vector2 pointer, bool show_pointer);

    // Where the NEXT button sits on the 1280x720 frame.
    Rectangle button_box() const;

    const RenderTexture2D& target() const { return target_; }
    const Font& font() const { return font_; }
    bool ready() const { return ready_; }

  private:
    void draw_text_block(const std::string& text, layout::Align align) const;
    void draw_button(bool pressed, float cursor) const;

    RenderTexture2D target_{};
    Font font_{};
    Font button_font_{};
    Texture2D next_button_{};
    Texture2D cursor_hand_{};
    Texture2D cursor_arrow_{};
    std::unordered_map<std::string, Picture> pictures_;
    std::string assets_;
    bool ready_ = false;
};

}  // namespace parody
