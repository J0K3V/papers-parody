// Where everything sits on a frame.
//
// These numbers are the whole look of the thing: the picture box, the text
// column and the pacing all come from the game's own ending screens. They are
// kept in one place so the editor, the live player and the exporter cannot
// drift apart.

#pragma once

namespace layout {

// The finished video.
constexpr int FRAME_WIDTH = 1280;
constexpr int FRAME_HEIGHT = 720;
constexpr int FPS = 30;

// The picture always occupies this box, centred across the frame.
constexpr int PICTURE_WIDTH = 480;
constexpr int PICTURE_HEIGHT = 320;
constexpr int PICTURE_LEFT = (FRAME_WIDTH - PICTURE_WIDTH) / 2;
constexpr int PICTURE_TOP = FRAME_HEIGHT / 3 - PICTURE_HEIGHT / 2;

// The report sits in a column centred on the frame. Pinning it to the left
// edge instead makes it drift away from everything else.
// Pillow sizes a font by its em square, raylib by the pixel height it
// rasterises to. For this font the same nominal size comes out about a third
// smaller in raylib, so it is asked for 55 to draw glyphs the size the video
// has. Widths are still measured from the shared table, never from raylib.
constexpr int FONT_SIZE = 40;       // the size everything is measured at
constexpr int DRAW_FONT_SIZE = 55;  // the size raylib is asked to rasterise
constexpr int TEXT_BLOCK_WIDTH = 800;
constexpr int TEXT_LEFT = (FRAME_WIDTH - TEXT_BLOCK_WIDTH) / 2;
constexpr int TEXT_TOP = FRAME_HEIGHT * 2 / 3;
constexpr int LINE_HEIGHT = 60;   // measured from the pixel font at 40px
constexpr int FONT_ASCENT = 43;

// The game does not roll on by itself: each screen waits on a NEXT button.
constexpr int ART_SCALE = 2;      // the game's art is drawn at twice size
constexpr int BUTTON_TOP = 636;
constexpr int BUTTON_LABEL_SIZE = 24;

// Pacing, in frames.
constexpr int WIPE_FRAMES = 20;
constexpr int PAUSE_FRAMES = 6;    // 0.2s
constexpr int HOLD_FRAMES = 60;    // 2.0s
constexpr int BLANK_FRAMES = 3;    // 0.1s
constexpr int FINAL_FRAMES = 150;  // 5.0s

// Where in the hold the hand arrives and presses.
constexpr float CURSOR_TRAVEL = 0.55f;
constexpr float CURSOR_PRESS_AT = 0.9f;

// How loud the typing clicks sit under everything else: -7 dB.
constexpr float LETTER_GAIN = 0.4467f;

// Seconds of fade where one piece of music gives way to another.
constexpr float MUSIC_CROSSFADE = 0.4f;

enum class Align { Left, Centre, Right };

inline int typing_step(const char* speed) {
    if (speed == nullptr) return 2;
    if (speed[0] == 's') return 3;  // slow
    if (speed[0] == 'f') return 1;  // fast
    return 2;                       // normal
}

}  // namespace layout
