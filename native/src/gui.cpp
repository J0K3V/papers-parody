#include "gui.h"

#include <algorithm>
#include <cmath>

#include "rlgl.h"  // the matrix stack the scrolling list rides on

namespace gui {
namespace {

Font ui_font{};
float clip_scroll = 0.0f;
bool clipping = false;
Rectangle clip_area{};

// Every widget is laid out against a 1280 px wide window. Rather than scaling
// each size and position at the call site - a couple of hundred numbers, each
// one a chance to forget - the whole canvas is scaled once and the mouse is
// converted back into those coordinates. See begin_scale().
float ui_scale = 1.0f;

// A field only takes typed characters while it has focus, and only one field
// can hold it, so the id of the focused box is tracked here.
const void* focused_field = nullptr;

// Set when something opens a panel mid-frame. A press stays true for the whole
// frame, so without this the very click that opens a picker is read again by
// whatever the pointer happens to be over inside it, and the panel closes on an
// arbitrary choice before it has been drawn once.
bool clicks_swallowed = false;

// An open dropdown list, held back until the rest of the frame has been drawn.
// Drawn in place it would be painted over by every widget laid out after it,
// which is how a list ended up half hidden behind the status line.
struct OpenList {
    Rectangle box;
    std::vector<std::string> options;
    int active;
};
std::vector<OpenList> deferred;

// Where last frame's open lists were drawn. A press inside one belongs to
// the list, but the list is painted above widgets that were laid out - and
// hit-tested - before its owner: without this, choosing from an open footer
// dropdown also landed in the scene text box behind it.
std::vector<Rectangle> shield;

// A list normally drops below its box, but one opened near the bottom of the
// window would run off the edge - which is what happened to every dropdown in
// the footer, where only the first option or two could be seen.
bool opens_upward(Rectangle box, size_t count) {
    const float below = box.y + box.height * float(count + 1);
    const float screen = float(GetScreenHeight()) / ui_scale;
    return below > screen && box.y - box.height * float(count) >= 0.0f;
}

// Where the i-th option of an open list sits.
Rectangle slot_of(Rectangle box, size_t i, size_t count) {
    if (opens_upward(box, count)) {
        // Stacked upwards, keeping the first option nearest its own box.
        return {box.x, box.y - box.height * float(i + 1), box.width, box.height};
    }
    return {box.x, box.y + box.height * float(i + 1), box.width, box.height};
}

}  // namespace

void set_font(Font font) { ui_font = font; }

float scale() { return ui_scale; }

void begin_scale(float scale) {
    ui_scale = std::clamp(scale, 1.0f, 4.0f);
    rlPushMatrix();
    rlScalef(ui_scale, ui_scale, 1.0f);
}

void end_scale() {
    rlPopMatrix();
    // ui_scale deliberately survives the pop. Work done between frames - the
    // stage texture, which cannot be rendered inside a scaled canvas - still
    // needs to convert the pointer, and it runs when no scale is in effect.
}

float text_width(const std::string& text, float size) {
    if (text.empty()) return 0.0f;
    return MeasureTextEx(ui_font, text.c_str(), size, 1.0f).x;
}

void label(Vector2 at, const std::string& text, float size, ::Color colour) {
    DrawTextEx(ui_font, text.c_str(), at, size, 1.0f, colour);
}

void heading(Vector2 at, const std::string& text) { label(at, text, 17.0f, UI_AMBER); }

void panel(Rectangle box, ::Color fill) { DrawRectangleRec(box, fill); }

bool clicked() { return !clicks_swallowed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

void swallow_clicks() { clicks_swallowed = true; }

void allow_clicks() { clicks_swallowed = false; }

void begin_frame() {
    clicks_swallowed = false;

    // A press on an open dropdown list is spoken for before any widget under
    // it gets a look. The dropdown itself tests its slots with the raw press,
    // so the choice still lands.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const Vector2 mouse = mouse_position();
        for (const Rectangle& box : shield) {
            if (CheckCollisionPointRec(mouse, box)) {
                clicks_swallowed = true;
                break;
            }
        }
    }
}

Vector2 mouse_position() {
    // The canvas is scaled, so screen pixels are divided back into the
    // coordinates the widgets were laid out in.
    const Vector2 mouse = GetMousePosition();
    return {mouse.x / ui_scale, mouse.y / ui_scale};
}

bool hovering(Rectangle box) {
    Vector2 mouse = mouse_position();

    if (clipping) {
        // Only what is actually on show can be hovered. Shifting the pointer by
        // the scroll without first checking it is inside the window meant a
        // click on a button below the list also landed on whatever row that
        // offset happened to reach - pressing a tab chose a picture as well.
        if (!CheckCollisionPointRec(mouse, clip_area)) return false;
        mouse.y += clip_scroll;
    }

    return CheckCollisionPointRec(mouse, box);
}

void begin_clip(Rectangle area, float scroll) {
    // Scissor takes real pixels, not the scaled ones the caller is drawing in.
    BeginScissorMode(int(area.x * ui_scale), int(area.y * ui_scale),
                     int(area.width * ui_scale), int(area.height * ui_scale));
    clip_scroll = scroll;
    clip_area = area;
    clipping = true;
    rlPushMatrix();
    rlTranslatef(0.0f, -scroll, 0.0f);
}

void end_clip() {
    rlPopMatrix();
    EndScissorMode();
    clipping = false;
    clip_scroll = 0.0f;
}

bool button(Rectangle box, const std::string& text, ::Color fill, bool enabled) {
    const bool over = enabled && hovering(box);
    DrawRectangleRec(box, !enabled ? ::Color{40, 46, 43, 255} : (over ? UI_RED_HOVER : fill));

    const float size = 17.0f;
    const float width = text_width(text, size);
    label({box.x + (box.width - width) / 2.0f, box.y + (box.height - size) / 2.0f - 1.0f}, text,
          size, enabled ? UI_INK : UI_INK_DIM);

    return over && clicked();
}

bool toggle(Rectangle box, const std::string& text, bool on) {
    constexpr float SIZE = 16.0f;
    constexpr float GAP = 8.0f;

    // The box and its label are both clickable. Callers only give a position,
    // so the hit area is worked out here from what is actually drawn - passing
    // a zero-width rectangle in would otherwise make the control impossible to
    // hit while still looking perfectly normal.
    const Rectangle hit = {box.x, box.y, SIZE + GAP + text_width(text, SIZE), SIZE + 8.0f};
    const bool over = hovering(hit);

    const Rectangle tick = {box.x, box.y + 4.0f, SIZE, SIZE};
    DrawRectangleRec(tick, on ? UI_AMBER : UI_CARD);
    DrawRectangleLinesEx(tick, 1.0f, over ? UI_INK : UI_EDGE);

    // A filled box alone reads as "there is a box here" more than as "this is
    // on", especially in amber on a dark panel. The mark settles it.
    if (on) {
        const ::Color mark = {26, 32, 30, 255};
        DrawLineEx({tick.x + 3.5f, tick.y + SIZE * 0.55f},
                   {tick.x + SIZE * 0.42f, tick.y + SIZE - 4.0f}, 2.5f, mark);
        DrawLineEx({tick.x + SIZE * 0.42f, tick.y + SIZE - 4.0f},
                   {tick.x + SIZE - 3.0f, tick.y + 3.5f}, 2.5f, mark);
    }

    label({box.x + SIZE + GAP, box.y + 4.0f}, text, SIZE, on ? UI_INK : UI_INK_DIM);

    return over && clicked();
}

bool slider(Rectangle box, float& value) {
    // The drag belongs to whichever slider it started on, tracked by the
    // address of its value - stable, since each slider edits its own float.
    static const void* dragged = nullptr;

    const void* id = static_cast<const void*>(&value);
    const bool over = hovering(box);

    if (over && clicked()) {
        dragged = id;
        swallow_clicks();   // this press is the slider's, not the panel's
    }
    if (dragged == id && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragged = nullptr;

    const float before = value;
    if (dragged == id) {
        value = std::clamp((mouse_position().x - box.x) / box.width, 0.0f, 1.0f);
    }

    const float mid = box.y + box.height / 2.0f;
    const Rectangle groove = {box.x, mid - 3.0f, box.width, 6.0f};
    DrawRectangleRec(groove, UI_CARD);
    DrawRectangleRec({groove.x, groove.y, groove.width * value, groove.height}, UI_AMBER);
    DrawRectangleLinesEx(groove, 1.0f, UI_EDGE);

    const Rectangle knob = {box.x + box.width * value - 5.0f, mid - 8.0f, 10.0f, 16.0f};
    DrawRectangleRec(knob, over || dragged == id ? UI_INK : UI_INK_DIM);
    DrawRectangleLinesEx(knob, 1.0f, UI_EDGE);

    return value != before;
}

int choice(Rectangle box, const std::vector<std::string>& options, int active) {
    if (options.empty()) return -1;

    const float each = box.width / options.size();
    int picked = -1;

    for (size_t i = 0; i < options.size(); ++i) {
        const Rectangle slot = {box.x + each * i, box.y, each - 2.0f, box.height};
        const bool over = hovering(slot);
        const bool is_active = int(i) == active;

        DrawRectangleRec(slot, is_active ? UI_CARD_ON : (over ? UI_RED_HOVER : UI_CARD));
        const float width = text_width(options[i], 16.0f);
        label({slot.x + (slot.width - width) / 2.0f, slot.y + (slot.height - 16.0f) / 2.0f - 1.0f},
              options[i], 16.0f, UI_INK);

        if (over && clicked()) picked = int(i);
    }

    return picked;
}

bool text_field(Rectangle box, std::string& text, bool& focused, int max_length) {
    const bool over = hovering(box);
    const void* id = static_cast<const void*>(&text);

    // A field claims focus only when the click is on it, and gives it up only
    // if it was the one holding it. Clearing on any click elsewhere looks right
    // until two fields are drawn in the same frame: the second one saw the same
    // press, found the pointer outside itself, and wiped the focus the first
    // had just taken - a box that lit up for one frame and then ignored the
    // keyboard.
    if (clicked()) {
        if (over) {
            focused = true;
            focused_field = id;
        } else if (focused_field == id) {
            focused = false;
            focused_field = nullptr;
        }
    }

    const bool mine = focused && focused_field == id;

    DrawRectangleRec(box, UI_CARD);
    if (mine) DrawRectangleLinesEx(box, 1, UI_AMBER);

    if (mine) {
        int codepoint = GetCharPressed();
        while (codepoint > 0) {
            if (int(text.size()) < max_length && codepoint >= 32) {
                // raylib hands back codepoints; store them as utf-8 so accents
                // survive a round trip through the project file.
                int bytes = 0;
                const char* piece = CodepointToUTF8(codepoint, &bytes);
                text.append(piece, size_t(bytes));
            }
            codepoint = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            // Step back over a whole character, not a stray continuation byte.
            while (!text.empty()) {
                const unsigned char last = (unsigned char)text.back();
                text.pop_back();
                if ((last & 0xC0) != 0x80) break;
            }
        }
    }

    // Show the tail of the line when it is longer than the box.
    std::string shown = text;
    while (text_width(shown, 17.0f) > box.width - 16.0f && shown.size() > 1) {
        shown.erase(0, 1);
    }

    label({box.x + 8, box.y + (box.height - 17.0f) / 2.0f - 1.0f}, shown, 17.0f, UI_INK);

    if (mine && (int(GetTime() * 2) % 2) == 0) {
        const float caret = box.x + 8 + text_width(shown, 17.0f) + 1;
        DrawRectangle(int(caret), int(box.y + 6), 2, int(box.height - 12), UI_AMBER);
    }

    return mine;
}

int dropdown(Rectangle box, const std::vector<std::string>& options, int active, bool& open) {
    if (options.empty()) return -1;

    const bool over = hovering(box);
    DrawRectangleRec(box, over ? UI_RED_HOVER : UI_CARD);
    label({box.x + 8, box.y + (box.height - 16.0f) / 2.0f - 1.0f},
          active >= 0 && active < int(options.size()) ? options[active] : "", 16.0f, UI_INK);
    label({box.x + box.width - 16, box.y + (box.height - 16.0f) / 2.0f - 1.0f}, "v", 16.0f, UI_INK_DIM);

    if (over && clicked()) {
        open = !open;
        swallow_clicks();  // do not let this same press hit a slot below
    }

    int picked = -1;
    if (open) {
        // The list is drawn at the end of the frame instead of here, so nothing
        // laid out afterwards paints over it. Hit testing still happens now,
        // with the raw press rather than clicked(): begin_frame has already
        // swallowed any press that landed on the list, precisely so nothing
        // else would read it - the list itself still must.
        const bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        for (size_t i = 0; i < options.size(); ++i) {
            const Rectangle slot = slot_of(box, i, options.size());
            if (hovering(slot) && pressed) {
                picked = int(i);
                open = false;
                swallow_clicks();
            }
        }

        if (open) {
            deferred.push_back({box, options, active});
            // A press anywhere off the list closes it, and is eaten so it does
            // not also press whatever it landed on.
            if (pressed && picked < 0 && !over) {
                open = false;
                swallow_clicks();
            }
        }
    }

    return picked;
}

void draw_deferred() {
    shield.clear();
    for (const OpenList& list : deferred) {
        for (size_t i = 0; i < list.options.size(); ++i) {
            const Rectangle slot = slot_of(list.box, i, list.options.size());
            shield.push_back(slot);
            DrawRectangleRec(slot, hovering(slot) ? UI_RED_HOVER : UI_PANEL);
            DrawRectangleLinesEx(slot, 1, UI_EDGE);
            label({slot.x + 8, slot.y + (slot.height - 16.0f) / 2.0f - 1.0f}, list.options[i],
                  16.0f, int(i) == list.active ? UI_AMBER : UI_INK);
        }
    }
    deferred.clear();
}

}  // namespace gui
