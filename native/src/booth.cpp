#include "booth.h"

#include <algorithm>
#include <cmath>

#include "gui.h"
#include "layout.h"

namespace parody {
namespace {

constexpr float TRAVEL_PER_SECOND = 2.4f;  // the whole drop takes under half a second

// How far the booth stands out past the video on each side. A frame, not a
// background: enough to read as the wall the screen is set into.
constexpr float MARGIN = 0.055f;

Texture2D load_pixels(const std::string& path) {
    Image image = LoadImage(path.c_str());
    if (image.data == nullptr) return Texture2D{};
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);  // pixel art stays hard
    SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    return texture;
}

// Draw a sprite once, stretched over `box` and clipped to `area`, so it reads
// as one piece of scenery whatever size the window is.
void draw_once(const Texture2D& texture, Rectangle area, Rectangle box, float shift_y) {
    if (texture.id == 0) return;

    // Scissor works in real pixels while everything here is drawn in the
    // editor's laid-out coordinates, so the rectangle is converted first.
    // Without this the clip lands in the top left and crops the wall.
    const float zoom = gui::scale();
    BeginScissorMode(int(area.x * zoom), int(area.y * zoom), int(area.width * zoom),
                     int(area.height * zoom));
    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                   {box.x, box.y + shift_y, box.width, box.height}, {0, 0}, 0.0f, WHITE);
    EndScissorMode();
}

}  // namespace

void Booth::load(const std::string& sprites_dir) {
    wall_ = load_pixels(sprites_dir + "/BoothWall.png");
    slats_ = load_pixels(sprites_dir + "/Shutter.png");
    lever_up_ = load_pixels(sprites_dir + "/ShutterSwitchUp.png");
    lever_down_ = load_pixels(sprites_dir + "/ShutterSwitchDown.png");
    // The sounds live one folder up from the sprites.
    sounds_ = sprites_dir + "/../../sounds/default";
}

void Booth::unload() {
    for (Texture2D* texture : {&wall_, &slats_, &lever_up_, &lever_down_}) {
        if (texture->id != 0) UnloadTexture(*texture);
        *texture = Texture2D{};
    }

    if (rattle_ready_) {
        StopSound(rattle_);
        UnloadSound(rattle_);
        rattle_ready_ = false;
    }
}

void Booth::play(const char* file) {
    const std::string path = sounds_ + "/" + file;
    if (!FileExists(path.c_str())) return;

    // One sound at a time: the drop and the lift never overlap, so the previous
    // one is released before the next is loaded.
    if (rattle_ready_) {
        StopSound(rattle_);
        UnloadSound(rattle_);
        rattle_ready_ = false;
    }

    rattle_ = LoadSound(path.c_str());
    rattle_ready_ = true;
    SetSoundVolume(rattle_, volume_);
    PlaySound(rattle_);
}

void Booth::set_volume(float gain) {
    volume_ = gain;
    // Mid-rattle the change lands at once, rather than on the next pull.
    if (rattle_ready_) SetSoundVolume(rattle_, gain);
}

void Booth::close() {
    if (target_ >= 1.0f) return;
    target_ = 1.0f;
    moving_ = true;
    play("ShutterDrop.wav");
}

void Booth::open() {
    if (target_ <= 0.0f) return;
    target_ = 0.0f;
    moving_ = true;
    play("ShutterRise.wav");
}

void Booth::close_instantly() {
    offset_ = 1.0f;
    target_ = 1.0f;
    moving_ = false;
}

void Booth::open_after(double seconds) { open_at_ = GetTime() + seconds; }

void Booth::reveal() {
    close();
    reveal_pending_ = true;
}

void Booth::update() {
    if (open_at_ > 0.0 && GetTime() >= open_at_) {
        open_at_ = 0.0;
        open();
    }

    if (!moving_) return;

    const float step = TRAVEL_PER_SECOND * GetFrameTime();
    if (offset_ < target_) {
        offset_ = std::min(target_, offset_ + step);
    } else {
        offset_ = std::max(target_, offset_ - step);
    }

    if (std::abs(offset_ - target_) < 0.001f) {
        offset_ = target_;
        moving_ = false;

        // A reveal is a drop followed by a lift, so the second half is queued
        // once the first has landed.
        if (reveal_pending_ && target_ >= 1.0f) {
            reveal_pending_ = false;
            open_at_ = GetTime() + 0.25;
        }
    }
}

Rectangle Booth::frame_for(Rectangle area, Rectangle stage) {
    const float margin = stage.width * MARGIN;
    Rectangle box = {stage.x - margin, stage.y - margin, stage.width + margin * 2.0f,
                     stage.height + margin * 2.0f};

    // Never wider or taller than the panel it lives in, or the margin is lost
    // off the edge and the wall looks cropped rather than framed.
    if (box.width > area.width) {
        box.x = area.x;
        box.width = area.width;
    }
    if (box.height > area.height) {
        box.y = area.y;
        box.height = area.height;
    }
    return box;
}

void Booth::draw_wall(Rectangle area, Rectangle stage) const {
    draw_once(wall_, area, frame_for(area, stage), 0.0f);
}

void Booth::draw_shutter(Rectangle area, Rectangle stage) const {
    if (offset_ <= 0.001f) return;
    // Falls faster as it goes, like real weight.
    const float eased = offset_ * offset_;
    const Rectangle box = frame_for(area, stage);
    draw_once(slats_, area, box, -box.height * (1.0f - eased));
}

Rectangle Booth::lever_box(Rectangle area, Rectangle stage) {
    const Rectangle booth = frame_for(area, stage);

    // Small, and flush with the top right of the booth: the handle belongs to
    // the wall, not to the picture. Sized against the wall it hangs on rather
    // than the window, so it keeps the shape the artwork was drawn at.
    // Scaled to the wall's own proportions it came out large enough to sit over
    // the video, which is not where it hangs in the game.
    constexpr float SHARE = 0.09f;         // of the booth's width
    constexpr float LEVER_WIDTH = 28.0f;   // ShutterSwitchUp.png
    constexpr float LEVER_HEIGHT = 23.0f;
    constexpr float DROP = 0.025f;         // of the booth's height

    const float width = booth.width * SHARE;
    const float height = width * LEVER_HEIGHT / LEVER_WIDTH;

    // Hard against the right edge, and hanging a little below the top rail
    // rather than jammed into the corner.
    return {booth.x + booth.width - width, booth.y + booth.height * DROP, width, height};
}

void Booth::draw_lever(Rectangle area, Rectangle stage, bool hovered) const {
    // Which way the handle points follows the shutter: down once it has
    // started falling, up while it is on its way back.
    const Texture2D& sprite = target_ >= 0.5f ? lever_down_ : lever_up_;
    if (sprite.id == 0) return;

    const Rectangle box = lever_box(area, stage);

    // Drawn last of everything in the booth, so it hangs in front of the bars
    // rather than being covered by them as they come down. Scissor works in
    // real pixels while this is drawn in the editor's scaled ones.
    const float zoom = gui::scale();
    BeginScissorMode(int(area.x * zoom), int(area.y * zoom), int(area.width * zoom),
                     int(area.height * zoom));
    DrawTexturePro(sprite, {0, 0, float(sprite.width), float(sprite.height)}, box, {0, 0}, 0.0f,
                   hovered ? Color{255, 255, 255, 255} : Color{215, 215, 215, 255});
    EndScissorMode();
}

void Booth::pull_lever() {
    // Whoever pulls it decides. Any lift the editor had queued for later - the
    // one that opens the booth on startup - is dropped, so it cannot come back
    // and undo this a second afterwards.
    open_at_ = 0.0;
    reveal_pending_ = false;

    if (target_ >= 0.5f) {
        open();
    } else {
        close();
    }
}

}  // namespace parody
