#include "stage.h"

#include <algorithm>
#include <cmath>

#include "font_metrics.h"

namespace parody {
namespace {

// Latin-1 plus the punctuation the reports actually use, so accents and the
// Spanish question marks all render instead of coming out as boxes.
std::vector<int> report_codepoints() {
    std::vector<int> points;
    for (int c = 32; c < 127; ++c) points.push_back(c);
    for (int c = 0xA1; c <= 0xFF; ++c) points.push_back(c);
    for (int c : {0x2018, 0x2019, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026}) points.push_back(c);
    return points;
}

Texture2D load_sprite(const std::string& path) {
    Image image = LoadImage(path.c_str());
    if (image.data == nullptr) return Texture2D{};
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);  // pixel art stays hard
    return texture;
}

}  // namespace

Stage::~Stage() { unload(); }

void Stage::load(const std::string& assets_dir) {
    assets_ = assets_dir;

    std::vector<int> points = report_codepoints();
    const std::string font_path = assets_ + "/fonts/pixelplay.ttf";
    font_ = LoadFontEx(font_path.c_str(), layout::DRAW_FONT_SIZE, points.data(), int(points.size()));
    button_font_ =
        LoadFontEx(font_path.c_str(), layout::BUTTON_LABEL_SIZE, points.data(), int(points.size()));

    SetTextureFilter(font_.texture, TEXTURE_FILTER_POINT);
    SetTextureFilter(button_font_.texture, TEXTURE_FILTER_POINT);

    next_button_ = load_sprite(assets_ + "/images/sprites/NextButton.png");
    cursor_hand_ = load_sprite(assets_ + "/images/sprites/CursorHand.png");
    cursor_arrow_ = load_sprite(assets_ + "/images/sprites/CursorArrow.png");

    target_ = LoadRenderTexture(layout::FRAME_WIDTH, layout::FRAME_HEIGHT);
    SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);  // smooth when scaled down
    ready_ = true;
}

void Stage::unload() {
    if (!ready_) return;
    forget_pictures();
    UnloadRenderTexture(target_);
    UnloadFont(font_);
    UnloadFont(button_font_);
    UnloadTexture(next_button_);
    UnloadTexture(cursor_hand_);
    UnloadTexture(cursor_arrow_);
    ready_ = false;
}

void Stage::forget_pictures() {
    for (auto& pair : pictures_) {
        if (pair.second.ready) UnloadTexture(pair.second.texture);
    }
    pictures_.clear();
}

const Picture* Stage::picture(const std::string& path, const std::string& fit_mode) {
    const std::string key = path + "|" + fit_mode;
    auto found = pictures_.find(key);
    if (found != pictures_.end()) return &found->second;

    Image source = LoadImage(path.c_str());
    Picture picture;
    if (source.data == nullptr) {
        pictures_[key] = picture;
        return &pictures_[key];
    }

    Image box = GenImageColor(layout::PICTURE_WIDTH, layout::PICTURE_HEIGHT, BLACK);

    if (fit_mode == "stretch") {
        // The game's own art is 240x160 and has to be doubled. A smooth filter
        // turns those pixels to mush, so only smooth when shrinking.
        if (source.width < layout::PICTURE_WIDTH || source.height < layout::PICTURE_HEIGHT) {
            ImageResizeNN(&source, layout::PICTURE_WIDTH, layout::PICTURE_HEIGHT);
        } else {
            ImageResize(&source, layout::PICTURE_WIDTH, layout::PICTURE_HEIGHT);
        }
        ImageDraw(&box, source, {0, 0, float(source.width), float(source.height)},
                  {0, 0, float(layout::PICTURE_WIDTH), float(layout::PICTURE_HEIGHT)}, WHITE);
    } else {
        const float scale = std::min(float(layout::PICTURE_WIDTH) / source.width,
                                     float(layout::PICTURE_HEIGHT) / source.height);
        const int width = std::max(1, int(std::lround(source.width * scale)));
        const int height = std::max(1, int(std::lround(source.height * scale)));

        if (scale > 1.0f) {
            ImageResizeNN(&source, width, height);
        } else {
            ImageResize(&source, width, height);
        }

        ImageDraw(&box, source, {0, 0, float(width), float(height)},
                  {float((layout::PICTURE_WIDTH - width) / 2),
                   float((layout::PICTURE_HEIGHT - height) / 2), float(width), float(height)},
                  WHITE);
    }

    picture.texture = LoadTextureFromImage(box);
    SetTextureFilter(picture.texture, TEXTURE_FILTER_POINT);
    picture.ready = true;

    UnloadImage(source);
    UnloadImage(box);

    pictures_[key] = picture;
    return &pictures_[key];
}

float Stage::text_width(const std::string& text) const {
    // Measured from the shared advance table, not from raylib, so a line breaks
    // in the same place here as it does in the rendered video and the browser.
    float width = 0.0f;
    for (size_t at = 0; at < text.size();) {
        int size = 0;
        const int codepoint = GetCodepointNext(text.c_str() + at, &size);
        width += font_metrics::advance(codepoint);
        at += size_t(size);
    }
    return width;
}

std::vector<std::string> Stage::wrap(const std::string& text) const {
    std::vector<std::string> lines;
    if (text.empty()) return lines;

    std::string current;
    size_t at = 0;

    while (at <= text.size()) {
        const size_t space = text.find(' ', at);
        const std::string word = text.substr(at, space == std::string::npos ? std::string::npos
                                                                            : space - at);
        const std::string candidate = current.empty() ? word : current + " " + word;

        if (!current.empty() && text_width(candidate) > layout::TEXT_BLOCK_WIDTH) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }

        if (space == std::string::npos) break;
        at = space + 1;
    }

    lines.push_back(current);
    return lines;
}

void Stage::draw_text_block(const std::string& text, layout::Align align) const {
    if (text.empty()) return;

    const std::vector<std::string> lines = wrap(text);
    for (size_t row = 0; row < lines.size(); ++row) {
        float x = float(layout::TEXT_LEFT);
        if (align != layout::Align::Left) {
            const float slack = layout::TEXT_BLOCK_WIDTH - text_width(lines[row]);
            x += align == layout::Align::Centre ? slack / 2.0f : slack;
        }
        DrawTextEx(font_, lines[row].c_str(),
                   {x, float(layout::TEXT_TOP + int(row) * layout::LINE_HEIGHT)},
                   float(layout::DRAW_FONT_SIZE), 0.0f, WHITE);
    }
}

Rectangle Stage::button_box() const {
    if (next_button_.id == 0) return {0, 0, 0, 0};

    const float width = float(next_button_.width * layout::ART_SCALE);
    const float height = float(next_button_.height / 2 * layout::ART_SCALE);
    return {float((layout::FRAME_WIDTH - int(width)) / 2), float(layout::BUTTON_TOP), width,
            height};
}

void Stage::draw_button(bool pressed, float cursor) const {
    if (next_button_.id == 0) return;

    // The sprite is two states stacked: the idle bar on top, the lit one below.
    // The word itself is not in the artwork, the game draws it on.
    const Rectangle box = button_box();
    const float half = float(next_button_.height) / 2.0f;
    const Rectangle source = {0.0f, pressed ? half : 0.0f, float(next_button_.width), half};

    DrawTexturePro(next_button_, source, box, {0, 0}, 0.0f, WHITE);

    const char* label = "NEXT";
    const Vector2 size = MeasureTextEx(button_font_, label, float(layout::BUTTON_LABEL_SIZE), 0.0f);
    const Color ink = pressed ? Color{34, 38, 34, 255} : Color{122, 128, 120, 255};
    DrawTextEx(button_font_, label,
               {box.x + (box.width - size.x) / 2.0f, box.y + (box.height - size.y) / 2.0f},
               float(layout::BUTTON_LABEL_SIZE), 0.0f, ink);

    // The pointer that comes up to press the button by itself. It travels as an
    // arrow and only turns into a hand once it arrives, which is what an actual
    // pointer does - and what it already did when you move it yourself.
    const Texture2D& sprite = pressed && cursor_hand_.id != 0 ? cursor_hand_ : cursor_arrow_;

    if (cursor >= 0.0f && sprite.id != 0) {
        const float width = float(sprite.width * layout::ART_SCALE);
        const float height = float(sprite.height * layout::ART_SCALE);
        const float rest_x = box.x + box.width / 2.0f - width / 4.0f;
        const float rest_y = box.y + box.height - height / 4.0f;

        // Comes up from below and settles just under the middle of the bar.
        const float eased = 1.0f - (1.0f - std::min(1.0f, cursor)) * (1.0f - std::min(1.0f, cursor));
        const float y = layout::FRAME_HEIGHT + (rest_y - layout::FRAME_HEIGHT) * eased;

        DrawTexturePro(sprite, {0, 0, float(sprite.width), float(sprite.height)},
                       {rest_x, y, width, height}, {0, 0}, 0.0f, WHITE);
    }
}

void Stage::draw(const FrameState& state, const Picture* art, bool waiting, bool hover,
                 Vector2 pointer, bool show_pointer) {
    BeginTextureMode(target_);
    ClearBackground(BLACK);

    if (state.entry != nullptr) {
        // The picture wipes in from the right.
        if (art != nullptr && art->ready) {
            const int hidden = std::min(layout::PICTURE_WIDTH,
                                        std::max(0, int(std::lround(state.hidden *
                                                                    layout::PICTURE_WIDTH))));
            const int visible = layout::PICTURE_WIDTH - hidden;
            if (visible > 0) {
                DrawTexturePro(art->texture,
                               {float(hidden), 0.0f, float(visible), float(layout::PICTURE_HEIGHT)},
                               {float(layout::PICTURE_LEFT + hidden), float(layout::PICTURE_TOP),
                                float(visible), float(layout::PICTURE_HEIGHT)},
                               {0, 0}, 0.0f, WHITE);
            }
        }

        draw_text_block(state.text, state.align);

        // While the viewer is the one clicking there is no automatic hand: the
        // bar simply lights up under the real pointer, as it does in the game.
        const char* button = state.button;
        float cursor = state.cursor;
        if (waiting) {
            button = hover ? "press" : "idle";
            cursor = -1.0f;
        }
        if (button != nullptr) draw_button(std::string(button) == "press", cursor);
    }

    if (show_pointer) {
        const Texture2D& sprite = (waiting && hover) ? cursor_hand_ : cursor_arrow_;
        if (sprite.id != 0) {
            const float width = float(sprite.width * layout::ART_SCALE);
            const float height = float(sprite.height * layout::ART_SCALE);
            const float offset = (waiting && hover) ? width / 3.0f : 0.0f;
            DrawTexturePro(sprite, {0, 0, float(sprite.width), float(sprite.height)},
                           {pointer.x - offset, pointer.y, width, height}, {0, 0}, 0.0f, WHITE);
        }
    }

    EndTextureMode();
}

}  // namespace parody
