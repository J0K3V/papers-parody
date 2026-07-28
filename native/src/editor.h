// The editor window.
//
// Scene list down the left, the stage and its transport on the right, the
// inspector for whichever scene is selected underneath. The whole state is the
// project plus which scene is picked, so nothing has to be kept in step.

#pragma once

#include <string>
#include <vector>

#include "booth.h"
#include "exporter.h"
#include "mixer.h"
#include "player.h"
#include "preview_sound.h"
#include "project.h"
#include "raylib.h"
#include "stage.h"
#include "timeline.h"

namespace parody {

// Which modal is up, if any. Only one at a time.
enum class Modal { None, Picture, Sound, Music, Open };

class Editor {
  public:
    bool start(const std::string& project_file);
    void run();
    void shutdown();

  private:
    void rebuild_timeline();

    // Keep the scene list on whichever scene the report is showing.
    void follow_playing_scene(int index);

    // Rebuild after an edit to the report itself and let a running preview pick
    // it up, without restarting music that has not changed.
    void apply_edit();

    // Rebuild after a change to how the report plays, stopping anything already
    // running - it was started against a timeline that no longer exists.
    void stop_playback(const std::string& what);

    // Copy chosen files into the project's own folders.
    void import_into(const std::string& folder, const std::string& title,
                     const std::string& filter);
    void import_pictures();
    void import_sounds();

    // Reports opened before, newest first, remembered in projects/recent.txt.
    std::string recents_file() const;
    void load_recents();
    void save_recents() const;
    void remember_recent(const std::string& file);
    void forget_recent(size_t at);

    void save_project_as();

    // The three volume sliders beside the stage: everything, the score, the
    // effects. Kept between sessions in projects/settings.json, which stays on
    // this machine like everything else in projects/.
    std::string settings_file() const;
    void load_settings();
    void save_settings() const;
    void apply_volumes();
    void draw_volume_panel(Rectangle area);

    // Back to the game's own ending, with no filename, so Save asks where to
    // put it instead of writing over the sample.
    void new_project();
    void draw_scene_list(Rectangle area);
    // The area everything is laid out against this frame, in the scaled
    // coordinates. Not the window size while fullscreen.
    Vector2 layout_size_{};

    // Where and how big the window was before it filled the screen, so it can
    // be put back exactly - maximised, if that is how it was.
    Vector2 windowed_size_{1280.0f, 800.0f};
    Vector2 windowed_at_{60.0f, 60.0f};
    bool windowed_maximised_ = false;

    // Set by whatever asks to change mode. Acted on between frames, never in
    // the middle of one: resizing the window with the canvas transform already
    // applied draws that frame against a size the window no longer has.
    bool fullscreen_wanted_ = false;

    // When the pointer last moved, and how far the close button has slid down
    // from above the top edge: 0 out of sight, 1 fully showing.
    double pointer_moved_at_ = 0.0;
    float close_slide_ = 0.0f;

    // Set while the stage is drawing the game's pointer, so the real one can be
    // hidden and there is never a second arrow on screen.
    bool game_pointer_showing_ = false;

    // Renders the video frame to its own texture. Must run before BeginDrawing:
    // the render target cannot be switched once the frame is under way.
    void render_stage_texture();
    void draw_stage(Rectangle area);
    void draw_inspector(Rectangle area);
    void draw_footer(Rectangle area);
    void draw_modal();
    void draw_recent_list(Rectangle box);

    void add_scene(const std::string& image);
    void delete_scene();
    void move_scene(int offset);
    void duplicate_scene();

    void open_project(const std::string& file);
    void save_project();
    void begin_render();
    void poll_render();

    std::vector<std::string> pictures_in(const std::string& folder) const;
    std::vector<std::string> sounds_of(bool music) const;

    Paths paths_;
    Stage stage_;
    Mixer mixer_;
    Player player_;
    Booth booth_;
    Exporter exporter_;
    SoundPreview preview_;
    Timeline timeline_;
    Project project_;

    // The report as it was when RENDER was pressed. Encoding spans many frames,
    // and it must not follow edits made while it runs.
    Timeline render_timeline_;
    Project render_project_;

    std::string file_;
    std::string status_;
    int selected_ = -1;
    float list_scroll_ = 0.0f;
    float list_view_height_ = 0.0f;   // how tall the scene list was drawn

    std::vector<std::string> recent_;

    Modal modal_ = Modal::None;
    std::string modal_tab_ = "default";  // the game's own frames come first
    float modal_scroll_ = 0.0f;

    float volume_master_ = 1.0f;
    float volume_music_ = 1.0f;
    float volume_effects_ = 1.0f;
    bool settings_dirty_ = false;   // saved when the drag lets go, not per frame

    bool text_focused_ = false;
    bool output_focused_ = false;
    bool soundtrack_open_ = false;
    bool fit_open_ = false;
    bool typing_open_ = false;

    Rectangle stage_box_{};
    bool rendering_ = false;
    double render_started_ = 0.0;
    float render_progress_ = 0.0f;
    std::string render_target_;
    bool picking_for_new_ = false;  // the picker was opened by ADD SCENE

    // Thumbnails for the scene list and the picker, kept as textures so the
    // list scrolls without touching the disk.
    std::vector<Texture2D> thumbs_;
    std::vector<std::string> thumb_keys_;
    Texture2D thumbnail(const std::string& path);
    void forget_thumbnails();
};

}  // namespace parody
