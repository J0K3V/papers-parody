// The handful of controls the editor needs.
//
// Immediate mode: every widget is drawn and tested in the same call, so there
// is no retained tree to keep in step with the project. That suits an editor
// whose whole state is a list of scenes.

#pragma once

#include <string>
#include <vector>

#include "raylib.h"

namespace gui {

// The same washed-out greens the rest of the project uses. raylib already
// defines UI_RED, UI_MAROON and friends at global scope, so these carry a prefix
// rather than shadowing them.
constexpr ::Color UI_BACKGROUND = {26, 32, 30, 255};
constexpr ::Color UI_PANEL = {35, 43, 40, 255};
constexpr ::Color UI_CARD = {44, 55, 51, 255};
constexpr ::Color UI_CARD_ON = {61, 81, 71, 255};
constexpr ::Color UI_EDGE = {17, 22, 20, 255};
constexpr ::Color UI_INK = {216, 211, 194, 255};
constexpr ::Color UI_INK_DIM = {140, 148, 144, 255};
constexpr ::Color UI_AMBER = {201, 162, 39, 255};
constexpr ::Color UI_RED = {163, 53, 47, 255};
constexpr ::Color UI_RED_HOVER = {192, 64, 57, 255};
constexpr ::Color UI_MAROON = {107, 43, 40, 255};

void set_font(Font font);

// The whole editor is laid out for a 1280 px window and then scaled to fit
// whatever the real one is, so a maximised window shows the same layout larger
// rather than the same pixels stranded in the corner. Everything between these
// draws in those 1280-wide coordinates.
void begin_scale(float scale);
void end_scale();
float scale();

// The pointer in laid-out coordinates. Use this instead of GetMousePosition
// anywhere inside begin_scale.
Vector2 mouse_position();

// Was the left button pressed this frame, and has nothing claimed it yet? Use
// this rather than IsMouseButtonPressed so swallow_clicks is respected.
bool clicked();

// Ignore the rest of this frame's click. Call it after opening a panel, so the
// press that opened it is not read again by whatever is now underneath the
// pointer. begin_frame clears it.
void swallow_clicks();

// Hand clicks back, for a panel drawn on top of everything else.
void allow_clicks();

void begin_frame();

// Anything drawn between these is clipped to the rectangle and shifted by the
// scroll offset, which is how the scene list scrolls.
void begin_clip(Rectangle area, float scroll);
void end_clip();

void label(Vector2 at, const std::string& text, float size, ::Color colour);
void heading(Vector2 at, const std::string& text);
float text_width(const std::string& text, float size);

bool button(Rectangle box, const std::string& text, ::Color fill = UI_CARD, bool enabled = true);
bool toggle(Rectangle box, const std::string& text, bool on);

// A horizontal slider over a 0..1 value. Returns true whenever the value
// moves. Once a drag has hold of it the pointer can wander out of the box and
// the knob still follows, the way every slider works.
bool slider(Rectangle box, float& value);

// A row of choices, one of which is active. Returns the index picked, or -1.
int choice(Rectangle box, const std::vector<std::string>& options, int active);

// One line of editable text. Returns true while it has focus.
bool text_field(Rectangle box, std::string& text, bool& focused, int max_length = 400);

// A dropdown. `open` is kept by the caller so only one can be open at a time.
// An open list is drawn by draw_deferred rather than here, so later widgets
// cannot paint over it.
int dropdown(Rectangle box, const std::vector<std::string>& options, int active, bool& open);

// Draws any open dropdown list. Call once, last, after everything else.
void draw_deferred();

void panel(Rectangle box, ::Color fill = UI_PANEL);
bool hovering(Rectangle box);

}  // namespace gui
