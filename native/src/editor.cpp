#include "editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "file_dialog.h"
#include "gui.h"
#include "json.hpp"

namespace parody {
namespace {

// The window the layout is written against, then drawn scaled up to whatever
// the real one is. Deliberately smaller than the smallest window allowed, so
// even at minimum size everything is enlarged a little: the type is a pixel
// font at 13 to 17 px, honest sizes on paper but too small to read on a modern
// display. Wide enough that the footer's controls fit on one row, and tall
// enough that the stage keeps a useful share once the inspector and footer have
// taken their fixed heights - too short and the preview shrinks to a stamp.
constexpr float DESIGN_WIDTH = 900.0f;
constexpr float DESIGN_HEIGHT = 700.0f;

constexpr float LIST_WIDTH = 300.0f;
constexpr float CARD_HEIGHT = 62.0f;

const std::vector<std::string> FIT_MODES = {"contain", "stretch"};
const std::vector<std::string> TYPING_SPEEDS = {"slow", "normal", "fast"};
const std::vector<std::string> ALIGN_NAMES = {"left", "centre", "right"};

// In the order of the SoundAt enum, since the picker returns an index.
const std::vector<std::string> SOUND_WHEN = {"on entry", "when typed", "at end", "after"};

std::vector<std::string> pack_names() {
    std::vector<std::string> names;
    for (const auto& pair : sound_packs()) names.push_back(pair.first);
    std::sort(names.begin(), names.end());
    return names;
}

int index_of(const std::vector<std::string>& options, const std::string& value) {
    for (size_t i = 0; i < options.size(); ++i) {
        if (options[i] == value) return int(i);
    }
    return 0;
}

std::string shorten(const std::string& text, size_t limit) {
    if (text.size() <= limit) return text;
    return text.substr(0, limit - 3) + "...";
}

std::string stem_of(const std::string& path) {
    if (path.empty()) return "";
    return GetFileNameWithoutExt(path.c_str());
}

// The largest 16:9 box that fits an area, so the picture scales with the
// window instead of being cropped.
Rectangle fit_stage(Rectangle area) {
    float width = area.width;
    float height = width * 9.0f / 16.0f;
    if (height > area.height) {
        height = area.height;
        width = height * 16.0f / 9.0f;
    }
    return {area.x + (area.width - width) / 2.0f, area.y + (area.height - height) / 2.0f, width,
            height};
}

// Borderless rather than true fullscreen: an undecorated window the size of the
// monitor. ToggleFullscreen changes the actual video mode, which blanks the
// display going in and out, does it again on every alt-tab, and leaves the
// framebuffer at the old window size so a 1280x800 editor is stretched across a
// 2560x1440 screen. This just takes the whole screen and gives it back.
void toggle_fullscreen(Vector2& windowed_size, Vector2& windowed_at, bool& was_maximised) {
    if (IsWindowState(FLAG_WINDOW_UNDECORATED)) {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);

        // Put it back exactly where it was, maximised included. Restoring only
        // the size and dropping it at a fixed corner left the window hanging
        // off the bottom right of the screen every time.
        if (was_maximised) {
            MaximizeWindow();
        } else {
            SetWindowSize(int(windowed_size.x), int(windowed_size.y));
            SetWindowPosition(int(windowed_at.x), int(windowed_at.y));
        }
        return;
    }

    was_maximised = IsWindowMaximized();
    windowed_size = {float(GetScreenWidth()), float(GetScreenHeight())};
    windowed_at = GetWindowPosition();

    const int monitor = GetCurrentMonitor();
    if (was_maximised) RestoreWindow();   // a maximised window will not resize
    SetWindowState(FLAG_WINDOW_UNDECORATED);
    SetWindowPosition(0, 0);
    SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
}

// Whether the stage has the screen to itself.
bool showing_bare() { return IsWindowState(FLAG_WINDOW_UNDECORATED); }

// Whether the sound-timing picker fits beside Choose on the picture/sound
// row. At most window shapes it does not and drops to a row of its own -
// the height the frame loop allots follows this same rule. Hiding it
// instead, as it used to, made "after" a trap with no way back.
bool sound_when_fits(float width, bool delayed) {
    return width > 952.0f + (delayed ? 98.0f : 0.0f);
}

}  // namespace

// ------------------------------------------------------------------- lifetime

bool Editor::start(const std::string& project_file) {
    paths_ = Paths::discover(nullptr);
    stage_.load(paths_.assets);
    gui::set_font(stage_.font());
    mixer_.set_sounds_dir(paths_.sounds);
    booth_.load(paths_.sprites);

    load_recents();
    load_settings();
    apply_volumes();

    // Carry on from the last report rather than always opening the sample.
    // Imported pictures and sounds live in the project's own folders, so those
    // are still there whatever is opened; this is about not having to go and
    // find your own work every time.
    file_ = project_file;
    if (file_.empty()) {
        for (const std::string& recent : recent_) {
            if (FileExists(recent.c_str())) {
                file_ = recent;
                break;
            }
        }
    }
    if (file_.empty()) file_ = sample_project(paths_);
    if (!file_.empty()) {
        std::string error;
        if (load_project(paths_, file_, project_, error)) {
            // The button is how a report is read, so it is always there. The
            // setting is left in the file for the browser editor, but nothing
            // here can turn it off any more - and a report that arrived with it
            // off would have been stuck that way with the control gone.
            project_.next_button = "on";
            status_ = "Loaded " + std::string(GetFileName(file_.c_str()));
            selected_ = project_.scenes.empty() ? -1 : 0;
        } else {
            status_ = "Could not read the project: " + error;
        }
    } else {
        status_ = "No project found - add a scene to start.";
    }

    rebuild_timeline();

    // The booth opens once everything has loaded, rather than during the
    // slowest part of startup where nobody would see it.
    booth_.close_instantly();
    booth_.open_after(2.0);
    return true;
}

void Editor::shutdown() {
    // A drag still in flight when the window closes would otherwise be lost.
    if (settings_dirty_) save_settings();

    // Audio first, and all of it: the device is closed straight after this
    // returns, and a Sound or a stream freed afterwards is freed through a
    // device that has already gone.
    preview_.stop();
    player_.release();

    forget_thumbnails();
    booth_.unload();
    stage_.unload();
}

void Editor::rebuild_timeline() {
    timeline_ = Timeline(project_.scenes, project_.typing, project_.next_button == "on",
                         !project_.movie_mode);
}

void Editor::apply_edit() {
    rebuild_timeline();

    // An edit to what the report says shows up straight away in the preview.
    // Only a genuine change of music restarts the score - retyping a line does
    // not, so writing while it plays is not a stutter every keystroke.
    if (player_.playing()) player_.refresh(mixer_, timeline_, project_);
}

void Editor::stop_playback(const std::string& what) {
    rebuild_timeline();

    // Changing how a report plays cannot be done to one already running: the
    // timeline it was started against no longer exists. Everything stops, music
    // included, and PLAY starts it again under the new rules.
    const bool was_running = player_.playing();
    player_.stop();
    selected_ = project_.scenes.empty() ? -1 : std::max(0, selected_);

    status_ = was_running ? what + "changed - stopped. Press PLAY to watch it again."
                          : what + "changed.";
}

void Editor::follow_playing_scene(int index) {
    if (index < 0 || index >= int(project_.scenes.size())) return;
    if (index == selected_) return;

    selected_ = index;

    // Bring the card into view if the list has scrolled past it, so a long
    // report does not leave the highlight somewhere off the top or bottom.
    const float row = float(index) * (CARD_HEIGHT + 6.0f);
    if (row < list_scroll_) {
        list_scroll_ = row;
    } else if (row + CARD_HEIGHT > list_scroll_ + list_view_height_) {
        list_scroll_ = row + CARD_HEIGHT - list_view_height_;
    }
    list_scroll_ = std::max(0.0f, list_scroll_);
}

void Editor::forget_thumbnails() {
    for (Texture2D& texture : thumbs_) {
        if (texture.id != 0) UnloadTexture(texture);
    }
    thumbs_.clear();
    thumb_keys_.clear();
}

Texture2D Editor::thumbnail(const std::string& path) {
    for (size_t i = 0; i < thumb_keys_.size(); ++i) {
        if (thumb_keys_[i] == path) return thumbs_[i];
    }

    Image image = LoadImage(path.c_str());
    Texture2D texture{};
    if (image.data != nullptr) {
        // Kept at its own shape and no smaller than it is shown. Squashing
        // everything into a fixed 96x64 first threw away the proportions and
        // then had to be stretched back out to fill a cell, which is what made
        // the pictures look both distorted and blocky.
        constexpr int LONGEST = 320;
        if (image.width > LONGEST || image.height > LONGEST) {
            const float scale = float(LONGEST) / float(std::max(image.width, image.height));
            ImageResize(&image, std::max(1, int(image.width * scale)),
                        std::max(1, int(image.height * scale)));
        }
        texture = LoadTextureFromImage(image);
        // Smooth: a thumbnail is always shown smaller than this, and nearest
        // neighbour on the way down is what turns fine detail into gravel.
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        UnloadImage(image);
    }

    thumb_keys_.push_back(path);
    thumbs_.push_back(texture);
    return texture;
}

// Draw a picture inside a box at its own proportions, centred, never stretched.
void draw_fitted(const Texture2D& texture, Rectangle box) {
    if (texture.id == 0) return;

    const float scale = std::min(box.width / float(texture.width),
                                 box.height / float(texture.height));
    const float width = float(texture.width) * scale;
    const float height = float(texture.height) * scale;

    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                   {box.x + (box.width - width) / 2.0f, box.y + (box.height - height) / 2.0f,
                    width, height},
                   {0, 0}, 0.0f, WHITE);
}

// --------------------------------------------------------------------- assets

std::vector<std::string> Editor::pictures_in(const std::string& folder) const {
    std::vector<std::string> found;
    FilePathList list = LoadDirectoryFiles((paths_.images + "/" + folder).c_str());
    for (unsigned int i = 0; i < list.count; ++i) {
        const std::string path = list.paths[i];
        if (IsFileExtension(path.c_str(), ".png") || IsFileExtension(path.c_str(), ".jpg") ||
            IsFileExtension(path.c_str(), ".jpeg")) {
            found.push_back(path);
        }
    }
    UnloadDirectoryFiles(list);
    std::sort(found.begin(), found.end());
    return found;
}

std::vector<std::string> Editor::sounds_of(bool music) const {
    std::vector<std::string> found;

    // The game's own, then anything imported alongside them.
    for (const std::string& folder : {paths_.sounds, paths_.assets + "/sounds/custom"}) {
        FilePathList list = LoadDirectoryFiles(folder.c_str());
        for (unsigned int i = 0; i < list.count; ++i) {
            const std::string path = list.paths[i];
            if (!IsFileExtension(path.c_str(), ".wav") && !IsFileExtension(path.c_str(), ".mp3") &&
                !IsFileExtension(path.c_str(), ".ogg")) {
                continue;
            }
            // A picker for effects shows effects and nothing else, and the same
            // for music: mixing the two made both lists twice as long as they
            // needed to be.
            const std::string key = folder == paths_.sounds ? std::string(GetFileName(path.c_str()))
                                                            : path;
            if (const_cast<Mixer&>(mixer_).is_music(key) == music) found.push_back(key);
        }
        UnloadDirectoryFiles(list);
    }

    std::sort(found.begin(), found.end());
    return found;
}

// ------------------------------------------------------------------ importing

void Editor::import_into(const std::string& folder, const std::string& title,
                         const std::string& filter) {
    const std::vector<std::string> chosen = ask_for_files(title, filter);
    if (chosen.empty()) return;

    if (!DirectoryExists(folder.c_str())) MakeDirectory(folder.c_str());

    int copied = 0;
    for (const std::string& source : chosen) {
        const std::string target = folder + "/" + GetFileName(source.c_str());
        int size = 0;
        unsigned char* data = LoadFileData(source.c_str(), &size);
        if (data == nullptr) continue;
        if (SaveFileData(target.c_str(), data, size)) ++copied;
        UnloadFileData(data);
    }

    status_ = copied == 0 ? "Nothing could be copied in."
                          : "Brought in " + std::to_string(copied) +
                                (copied == 1 ? " file." : " files.");
    forget_thumbnails();
}

void Editor::import_pictures() {
    import_into(paths_.images + "/custom", "Pictures to bring in",
                "Pictures|*.png;*.jpg;*.jpeg");
    modal_tab_ = "custom";
    modal_scroll_ = 0.0f;
}

void Editor::import_sounds() {
    import_into(paths_.assets + "/sounds/custom", "Sounds to bring in",
                "Sounds|*.wav;*.mp3;*.ogg");
    modal_scroll_ = 0.0f;
}

// ---------------------------------------------------------------------- edits

void Editor::add_scene(const std::string& image) {
    Scene scene;
    scene.image = image;
    const size_t at = selected_ >= 0 ? size_t(selected_) + 1 : project_.scenes.size();
    project_.scenes.insert(project_.scenes.begin() + long(at), scene);
    selected_ = int(at);
    apply_edit();
    status_ = "Scene added.";
}

void Editor::delete_scene() {
    if (selected_ < 0 || selected_ >= int(project_.scenes.size())) return;
    project_.scenes.erase(project_.scenes.begin() + selected_);
    selected_ = project_.scenes.empty() ? -1
                                        : std::min(selected_, int(project_.scenes.size()) - 1);
    apply_edit();
    status_ = "Scene deleted.";
}

void Editor::move_scene(int offset) {
    if (selected_ < 0) return;
    const int target = selected_ + offset;
    if (target < 0 || target >= int(project_.scenes.size())) return;
    std::swap(project_.scenes[selected_], project_.scenes[target]);
    selected_ = target;
    apply_edit();
}

void Editor::duplicate_scene() {
    if (selected_ < 0) return;
    project_.scenes.insert(project_.scenes.begin() + selected_ + 1, project_.scenes[selected_]);
    selected_ += 1;
    apply_edit();
    status_ = "Scene duplicated.";
}

void Editor::open_project(const std::string& file) {
    std::string error;
    Project loaded;
    if (!load_project(paths_, file, loaded, error)) {
        status_ = "Could not read that project: " + error;
        return;
    }
    player_.stop();
    project_ = loaded;
    project_.next_button = "on";   // see start(): the control for this is gone
    file_ = file;
    selected_ = project_.scenes.empty() ? -1 : 0;
    rebuild_timeline();
    remember_recent(file);
    booth_.reveal();  // drop the shutter and lift it, to show what is behind
    status_ = "Loaded " + std::string(GetFileName(file.c_str()));
}

void Editor::new_project() {
    const std::string sample = sample_project(paths_);
    if (sample.empty()) {
        status_ = "The sample report is missing from projects/.";
        return;
    }

    std::string error;
    Project loaded;
    if (!load_project(paths_, sample, loaded, error)) {
        status_ = "Could not read the sample report: " + error;
        return;
    }

    player_.stop();
    project_ = loaded;
    project_.next_button = "on";

    // Deliberately nameless. Save will ask where it should go rather than
    // writing over the sample everyone starts from.
    file_.clear();
    selected_ = project_.scenes.empty() ? -1 : 0;
    list_scroll_ = 0.0f;
    rebuild_timeline();
    booth_.reveal();
    status_ = "Started again from the game's own ending.";
}

void Editor::save_project() {
    // The editor opens on the bundled sample, so plain Save would quietly write
    // over it the first time it was pressed. Nothing has been named yet, so ask
    // where it should go - after that, Save just saves.
    const bool is_sample =
        file_.empty() || file_ == paths_.projects + "/default.json" ||
        std::string(GetFileName(file_.c_str())) == "default.json";

    if (is_sample) {
        save_project_as();
        return;
    }
    if (parody::save_project(paths_, file_, project_)) {
        remember_recent(file_);
        status_ = "Saved to " + file_;
    } else {
        status_ = "Could not save to " + file_;
    }
}

void Editor::save_project_as() {
    const std::string suggested =
        file_.empty() ? "my-report.json" : std::string(GetFileName(file_.c_str()));
    const std::string chosen =
        ask_where_to_save("Save this report", "Reports|*.json", suggested);
    if (chosen.empty()) return;

    if (parody::save_project(paths_, chosen, project_)) {
        file_ = chosen;
        remember_recent(chosen);
        status_ = "Saved to " + chosen;
    } else {
        status_ = "Could not save to " + chosen;
    }
}

// ------------------------------------------------------------------ settings

std::string Editor::settings_file() const { return paths_.projects + "/settings.json"; }

void Editor::load_settings() {
    if (!FileExists(settings_file().c_str())) return;

    try {
        std::ifstream in(settings_file());
        nlohmann::json payload;
        in >> payload;

        const nlohmann::json volume = payload.value("volume", nlohmann::json::object());
        volume_master_ = std::clamp(volume.value("master", 1.0f), 0.0f, 1.0f);
        volume_music_ = std::clamp(volume.value("music", 1.0f), 0.0f, 1.0f);
        volume_effects_ = std::clamp(volume.value("effects", 1.0f), 0.0f, 1.0f);
    } catch (const std::exception&) {
        // A settings file that will not read is no worse than none at all.
        volume_master_ = volume_music_ = volume_effects_ = 1.0f;
    }
}

void Editor::save_settings() const {
    nlohmann::json payload;
    payload["volume"] = {{"master", volume_master_},
                         {"music", volume_music_},
                         {"effects", volume_effects_}};
    std::ofstream out(settings_file());
    out << payload.dump(2) << "\n";
}

void Editor::apply_volumes() {
    // The master sits on the device, so it reaches every stream and every
    // sound - previews included. The other two land where each kind is played.
    // None of it touches RENDER: an exported video is mixed at full level.
    SetMasterVolume(volume_master_);
    player_.set_music_volume(volume_music_);
    player_.set_effects_volume(volume_effects_);
    booth_.set_volume(volume_effects_);
}

// ------------------------------------------------------------------- recents

std::string Editor::recents_file() const { return paths_.projects + "/recent.txt"; }

void Editor::load_recents() {
    recent_.clear();
    const std::string path = recents_file();
    if (!FileExists(path.c_str())) return;

    char* text = LoadFileText(path.c_str());
    if (text == nullptr) return;

    std::string line;
    for (const char* at = text;; ++at) {
        if (*at == '\n' || *at == '\0') {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) recent_.push_back(line);
            line.clear();
            if (*at == '\0') break;
        } else {
            line.push_back(*at);
        }
    }
    UnloadFileText(text);
}

void Editor::save_recents() const {
    std::string text;
    for (const std::string& path : recent_) text += path + "\n";
    SaveFileText(recents_file().c_str(), text.empty() ? const_cast<char*>("")
                                                      : const_cast<char*>(text.c_str()));
}

void Editor::remember_recent(const std::string& file) {
    // Most recent first, and only once each.
    recent_.erase(std::remove(recent_.begin(), recent_.end(), file), recent_.end());
    recent_.insert(recent_.begin(), file);
    if (recent_.size() > 16) recent_.resize(16);
    save_recents();
}

void Editor::forget_recent(size_t at) {
    if (at >= recent_.size()) return;
    // Only the entry in this list goes. Whether the report itself should be
    // deleted is not the editor's call to make.
    recent_.erase(recent_.begin() + long(at));
    save_recents();
}

// --------------------------------------------------------------------- render

void Editor::begin_render() {
    if (rendering_ || project_.scenes.empty()) return;

    render_target_ = project_.output.empty() ? "my-parody.mp4" : project_.output;
    if (render_target_.find(':') == std::string::npos && render_target_[0] != '/') {
        render_target_ = paths_.root + "/" + render_target_;
    }

    // The report as it stands, kept aside for the whole render. Encoding takes
    // many frames, and pointing it at the live one meant an edit halfway
    // through wrote the second half of the video against a different report -
    // and deleting every scene divided by a zero-length timeline.
    render_project_ = project_;
    render_timeline_ = timeline_;

    rendering_ = true;
    render_started_ = GetTime();
    render_progress_ = 0.0f;
    status_ = "Rendering...";
    booth_.close();  // the booth shuts while the work is done
}

void Editor::poll_render() {
    if (!rendering_) return;

    // Encoding runs on this thread a slice at a time, so the window stays
    // responsive and the shutter keeps moving while it works.
    const bool done = exporter_.step(stage_, mixer_, render_timeline_, render_project_, paths_,
                                     render_target_, render_progress_);
    if (!done) return;

    rendering_ = false;
    const double took = GetTime() - render_started_;

    if (exporter_.failed()) {
        status_ = "Render failed: " + exporter_.error();
    } else {
        char line[256];
        std::snprintf(line, sizeof(line), "Rendered %s in %.0fs",
                      GetFileName(render_target_.c_str()), took);
        status_ = line;
    }
    booth_.open();
}

// ----------------------------------------------------------------- scene list

void Editor::draw_scene_list(Rectangle area) {
    gui::panel(area);
    gui::heading({area.x + 14, area.y + 12}, "SCENES");
    gui::label({area.x + area.width - 40, area.y + 12}, std::to_string(project_.scenes.size()),
               16.0f, gui::UI_INK_DIM);

    const Rectangle add = {area.x + 14, area.y + area.height - 46, area.width - 28, 34};
    if (gui::button(add, "+  ADD SCENE", gui::UI_RED)) {
        modal_ = Modal::Picture;
        modal_tab_ = "default";
        modal_scroll_ = 0.0f;
        picking_for_new_ = true;
        gui::swallow_clicks();
    }

    const Rectangle list = {area.x + 8, area.y + 38, area.width - 16,
                            area.height - 38 - 56};

    // The wheel belongs to whatever panel is on top: with a picker open over
    // the left of the window, scrolling it also crept through to this list.
    if (modal_ == Modal::None && gui::hovering(list)) {
        list_scroll_ -= GetMouseWheelMove() * 40.0f;
    }
    // Remembered so following the report as it plays can scroll the right card
    // into view without having to work the panel's size out a second time.
    list_view_height_ = list.height;

    const float content = project_.scenes.size() * (CARD_HEIGHT + 6.0f);
    list_scroll_ = std::max(0.0f, std::min(list_scroll_, std::max(0.0f, content - list.height)));

    if (project_.scenes.empty()) {
        gui::label({list.x + 20, list.y + 40}, "No scenes yet.", 16.0f, gui::UI_INK_DIM);
        gui::label({list.x + 20, list.y + 62}, "Press ADD SCENE.", 16.0f, gui::UI_INK_DIM);
        return;
    }

    gui::begin_clip(list, list_scroll_);
    for (size_t i = 0; i < project_.scenes.size(); ++i) {
        const Scene& scene = project_.scenes[i];
        const Rectangle card = {list.x, list.y + i * (CARD_HEIGHT + 6.0f), list.width, CARD_HEIGHT};

        const bool over = gui::hovering(card);
        const bool chosen = int(i) == selected_;
        DrawRectangleRec(card, chosen ? gui::UI_CARD_ON : gui::UI_CARD);
        DrawRectangleLinesEx(card, 1, gui::UI_EDGE);

        const Texture2D thumb = thumbnail(scene.image);
        if (thumb.id != 0) {
            draw_fitted(thumb, {card.x + 5, card.y + 5, 76, 52});
        }

        gui::label({card.x + 90, card.y + 8}, "SCENE " + std::to_string(i + 1), 14.0f,
                   gui::UI_AMBER);
        const std::string caption = scene.text.empty() ? "(no text)" : shorten(scene.text, 24);
        gui::label({card.x + 90, card.y + 26}, caption, 15.0f, gui::UI_INK);

        if (!scene.music.empty()) {
            gui::label({card.x + 90, card.y + 44}, "* " + stem_of(scene.music), 13.0f,
                       gui::UI_AMBER);
        } else if (!scene.sound.empty()) {
            gui::label({card.x + 90, card.y + 44}, "- " + stem_of(scene.sound), 13.0f,
                       gui::UI_AMBER);
        }

        if (over && gui::clicked() && modal_ == Modal::None) {
            selected_ = int(i);

            // Picking a scene while the report is running jumps to it rather
            // than only changing what the inspector edits. The music carries on
            // through, so scrubbing about does not chop it up.
            if (player_.playing() && i < timeline_.entries().size()) {
                player_.seek(timeline_.entries()[i].start);
                status_ = "Playing from scene " + std::to_string(i + 1) + ".";
            }
        }
    }
    gui::end_clip();
}

// ---------------------------------------------------------------------- stage

void Editor::render_stage_texture() {
    // Drawing the frame means switching render target, and rlgl cannot do that
    // partway through a frame once the canvas transform is applied - it leaves
    // the batch pointing at a framebuffer that is no longer bound. So the frame
    // is rendered to its texture first, before any of the editor is drawn, and
    // the panel simply blits the result.
    FrameState state;
    const Picture* art = nullptr;

    if (player_.playing()) {
        state = timeline_.state_at(player_.frame());
    } else if (selected_ >= 0 && selected_ < int(timeline_.entries().size())) {
        const Entry& entry = timeline_.entries()[selected_];
        state = timeline_.state_at(entry.start + entry.type_end);
    }
    if (state.entry != nullptr && state.entry->index < int(project_.scenes.size())) {
        art = stage_.picture(project_.scenes[state.entry->index].image, project_.fit);
    }

    // Where the pointer sits on the 1280x720 frame. stage_box_ is last frame's,
    // which is exact unless the window is being resized this very instant.
    const Vector2 mouse = gui::mouse_position();
    Vector2 on_frame = {0, 0};
    bool on_stage = false;
    if (stage_box_.width > 0.0f && stage_box_.height > 0.0f) {
        on_frame = {(mouse.x - stage_box_.x) / stage_box_.width * layout::FRAME_WIDTH,
                    (mouse.y - stage_box_.y) / stage_box_.height * layout::FRAME_HEIGHT};
        on_stage = CheckCollisionPointRec(mouse, stage_box_) && modal_ == Modal::None;
    }

    if (player_.waiting()) {
        const bool over = on_stage && CheckCollisionPointRec(on_frame, stage_.button_box());
        player_.set_hover(over);
        if (over && gui::clicked()) {
            if (player_.press_next()) {
                selected_ = 0;
                if (showing_bare()) fullscreen_wanted_ = true;
                status_ = "Back to the start.";
            }
        }
    }

    // The scene list follows the report as it plays, so the card highlighted
    // and the inspector below are always the scene on screen.
    if (player_.playing() && state.entry != nullptr) {
        follow_playing_scene(state.entry->index);
    }

    // While the report is playing and the pointer is over it, the stage draws
    // the game's own pointer at that spot. The real one is hidden so there is
    // only ever the one arrow on screen.
    game_pointer_showing_ = on_stage && player_.playing();

    stage_.draw(state, art, player_.waiting(), player_.hover(), on_frame,
                game_pointer_showing_);
}

void Editor::draw_stage(Rectangle area) {
    const bool bare = showing_bare();

    gui::panel(area);
    if (!bare) gui::heading({area.x + 14, area.y + 10}, "STAGE");

    // Filling the screen means filling it: no heading, no transport, nothing
    // held back for them. Reserving even a strip makes the 16:9 fit smaller
    // than the display and leaves a border of panel colour all the way round.
    // Windowed, room is left for the title above, the transport below and the
    // volume sliders down the right-hand side.
    constexpr float VOLUME_WIDTH = 128.0f;
    constexpr float VOLUME_GAP = 14.0f;
    const Rectangle inner =
        bare ? area
             : Rectangle{area.x + 14, area.y + 34, area.width - 28 - VOLUME_WIDTH - VOLUME_GAP,
                         area.height - 90};

    // Windowed, the stage gives up a little room so the booth has somewhere to
    // sit around it. Fullscreen has no booth, so it takes everything.
    const float inset = bare ? 0.0f : std::min(inner.width, inner.height) * 0.05f;
    stage_box_ = fit_stage({inner.x + inset, inner.y + inset, inner.width - inset * 2.0f,
                            inner.height - inset * 2.0f});

    // The booth wall sits behind the picture, the way the screen sits in the
    // booth in the game. Fullscreen leaves it out: there the report is meant to
    // be watched, not edited, and a wall around it is just decoration.
    if (!bare) booth_.draw_wall(inner, stage_box_);

    // The frame itself was rendered to its texture before drawing began - see
    // render_stage_texture - because switching render target partway through a
    // frame cannot be done with the canvas transform applied.
    DrawTexturePro(stage_.target().texture,
                   {0, 0, float(layout::FRAME_WIDTH), -float(layout::FRAME_HEIGHT)}, stage_box_,
                   {0, 0}, 0.0f, WHITE);

    // The shutter comes down over all of it, and the handle hangs in front of
    // the bars. Pull it whenever you like - the booth is yours.
    if (!bare) {
        booth_.draw_shutter(inner, stage_box_);

        const Rectangle lever = Booth::lever_box(inner, stage_box_);
        const bool on_lever = gui::hovering(lever) && modal_ == Modal::None;
        booth_.draw_lever(inner, stage_box_, on_lever);
        if (on_lever && gui::clicked()) booth_.pull_lever();

        draw_volume_panel(
            {inner.x + inner.width + VOLUME_GAP, inner.y, VOLUME_WIDTH, inner.height});
    }

    // Filling the screen leaves nothing but the report: no transport, no
    // status. Just a close button in the corner, which fades in when the mouse
    // moves and back out when it stops, the way a video player does it. Escape
    // works whether it is showing or not.
    const float row = area.y + area.height - 44;
    const bool can_play = !project_.scenes.empty() && !player_.playing() && !rendering_;

    if (bare) {
        // The close button lives above the top edge and slides down when the
        // pointer comes near it, the way a video player brings its controls in.
        // Anywhere else on screen and it stays hidden, so a report being watched
        // is never sat under a button.
        constexpr float SIZE = 44.0f;
        constexpr float RESTING = 28.0f;   // how far down it sits when out
        const float reach = area.height * 0.16f;

        const Vector2 mouse = gui::mouse_position();
        const bool near_top = mouse.y <= area.y + reach &&
                              mouse.x >= area.x + area.width - reach * 2.0f;

        const float step = GetFrameTime() * 6.0f;
        close_slide_ = std::clamp(close_slide_ + (near_top ? step : -step), 0.0f, 1.0f);
        if (close_slide_ <= 0.001f) return;

        // Eased so it settles rather than snapping to a stop.
        const float eased = close_slide_ * close_slide_ * (3.0f - 2.0f * close_slide_);
        const Rectangle close = {area.x + area.width - SIZE - 32.0f,
                                 area.y - SIZE + eased * (SIZE + RESTING), SIZE, SIZE};
        const bool over = gui::hovering(close);

        const float mid = SIZE / 2.0f;
        const float arm = 12.0f;
        const Vector2 centre = {close.x + mid, close.y + mid};
        const unsigned char ink = (unsigned char)(eased * (over ? 255 : 190));

        DrawCircleV(centre, mid, {0, 0, 0, (unsigned char)(eased * (over ? 170 : 110))});
        DrawLineEx({centre.x - arm, centre.y - arm}, {centre.x + arm, centre.y + arm}, 3.0f,
                   {255, 255, 255, ink});
        DrawLineEx({centre.x + arm, centre.y - arm}, {centre.x - arm, centre.y + arm}, 3.0f,
                   {255, 255, 255, ink});

        if (over && gui::clicked()) fullscreen_wanted_ = true;
        return;
    }

    if (gui::button({area.x + 14, row, 96, 32}, "PLAY", gui::UI_RED, can_play)) {
        const int from = (!project_.movie_mode && selected_ > 0)
                             ? timeline_.entries()[selected_].start
                             : 0;
        player_.prepare(mixer_, timeline_, project_);
        player_.play(from);
        status_ = "Playing.";
    }
    // A game does not pause, so the button is only live while watching a film.
    if (gui::button({area.x + 118, row, 96, 32}, player_.paused() ? "RESUME" : "PAUSE",
                    gui::UI_CARD, player_.playing() && project_.movie_mode)) {
        player_.toggle_pause();
    }
    if (gui::button({area.x + 222, row, 96, 32}, "STOP", gui::UI_CARD, player_.playing())) {
        player_.stop();
        status_ = "Stopped.";
    }
    if (gui::button({area.x + 326, row, 130, 32},
                    showing_bare() ? "WINDOWED" : "FULLSCREEN")) {
        fullscreen_wanted_ = true;
    }

    char clock[128];
    const double at = player_.playing() ? player_.frame() / double(layout::FPS) : 0.0;
    std::snprintf(clock, sizeof(clock), "%d:%02d / %d:%02d", int(at) / 60, int(at) % 60,
                  int(timeline_.duration()) / 60, int(timeline_.duration()) % 60);

    // Clock hard against the right edge, the notice in whatever is left between
    // it and the buttons, rather than at a fixed x that they grow into.
    const float clock_width = gui::text_width(clock, 16.0f);
    const float clock_x = area.x + area.width - 14 - clock_width;
    gui::label({clock_x, row + 8}, clock, 16.0f, gui::UI_INK_DIM);

    if (player_.waiting()) {
        const Entry* entry = timeline_.entry_at(player_.frame());
        const std::string notice = entry != nullptr && entry->is_last
                                       ? "the end - press NEXT to start over"
                                       : "waiting on NEXT";
        const float notice_x = area.x + 470;
        if (clock_x - notice_x > gui::text_width(notice, 15.0f) + 12.0f) {
            gui::label({notice_x, row + 8}, notice, 15.0f, gui::UI_AMBER);
        }
    }
}

// ------------------------------------------------------------- volume sliders

void Editor::draw_volume_panel(Rectangle area) {
    struct Row {
        const char* name;
        float* value;
    };
    const Row rows[] = {{"master", &volume_master_},
                        {"music", &volume_music_},
                        {"effects", &volume_effects_}};

    // The stack sits level with the middle of the booth beside it.
    constexpr float GROUP = 48.0f;
    constexpr float HEAD = 26.0f;
    float y = area.y + std::max(0.0f, (area.height - (HEAD + GROUP * 3.0f)) / 2.0f);

    gui::label({area.x, y}, "VOLUME", 14.0f, gui::UI_AMBER);
    y += HEAD;

    bool moved = false;
    for (const Row& row : rows) {
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%d%%", int(std::lround(*row.value * 100.0f)));

        gui::label({area.x, y}, row.name, 13.0f, gui::UI_INK_DIM);
        gui::label({area.x + area.width - gui::text_width(percent, 13.0f), y}, percent, 13.0f,
                   gui::UI_INK_DIM);
        if (gui::slider({area.x, y + 15.0f, area.width, 20.0f}, *row.value)) moved = true;
        y += GROUP;
    }

    if (moved) {
        apply_volumes();
        settings_dirty_ = true;
    }
    // Written once the drag lets go rather than at every pixel of it.
    if (settings_dirty_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        save_settings();
        settings_dirty_ = false;
    }
}

// ------------------------------------------------------------------ inspector

void Editor::draw_inspector(Rectangle area) {
    gui::panel(area);

    if (selected_ < 0 || selected_ >= int(project_.scenes.size())) {
        gui::label({area.x + 14, area.y + 16}, "Select a scene to edit it.", 16.0f,
                   gui::UI_INK_DIM);
        return;
    }

    Scene& scene = project_.scenes[selected_];
    const float left = area.x + 14;
    float y = area.y + 10;

    // Picture and sound share a row: the stage needs the height.
    gui::heading({left, y + 6}, "PICTURE");
    gui::label({left + 80, y + 6}, shorten(stem_of(scene.image), 18), 16.0f, gui::UI_INK);
    if (gui::button({left + 240, y, 80, 26}, "Change")) {
        modal_ = Modal::Picture;
        modal_tab_ = "default";
        modal_scroll_ = 0.0f;
        picking_for_new_ = false;
        gui::swallow_clicks();
    }

    gui::heading({left + 350, y + 6}, "SOUND");
    gui::label({left + 420, y + 6},
               scene.sound.empty() ? "none" : shorten(stem_of(scene.sound), 16), 16.0f,
               scene.sound.empty() ? gui::UI_INK_DIM : gui::UI_INK);
    if (gui::button({left + 580, y, 90, 26}, "Choose")) {
        modal_ = Modal::Sound;
        modal_scroll_ = 0.0f;
        gui::swallow_clicks();
    }

    // When that sound fires. Only worth showing once there is one to place.
    // Beside Choose when the row is wide enough; on its own row otherwise.
    if (!scene.sound.empty()) {
        const bool delayed = scene.sound_at == SoundAt::Delayed;

        float when_x;
        if (sound_when_fits(area.width, delayed)) {
            when_x = area.x + area.width - 14.0f - (delayed ? 98.0f : 0.0f) - 244.0f;
        } else {
            y += 34;
            gui::label({left, y + 6}, "plays", 16.0f, gui::UI_INK_DIM);
            when_x = left + 80.0f;
        }

        const int when_pick = gui::choice({when_x, y, 232, 26}, SOUND_WHEN,
                                          int(scene.sound_at));
        if (when_pick >= 0) {
            scene.sound_at = SoundAt(when_pick);
            apply_edit();
        }

        if (delayed) {
            float at = when_x + 232.0f + 12.0f;
            if (gui::button({at, y, 26, 26}, "-")) {
                scene.sound_delay = std::max(0.0f, scene.sound_delay - 0.5f);
                apply_edit();
            }
            char seconds[16];
            std::snprintf(seconds, sizeof(seconds), "%.1fs", double(scene.sound_delay));
            gui::label({at + 32, y + 6}, seconds, 16.0f, gui::UI_INK);
            if (gui::button({at + 72, y, 26, 26}, "+")) {
                scene.sound_delay = std::min(30.0f, scene.sound_delay + 0.5f);
                apply_edit();
            }
        }
    }

    y += 34;
    gui::heading({left, y + 6}, "MUSIC");

    // Nothing precedes the opening scene, so "carries on" would be a lie there:
    // it is the soundtrack picked at the bottom of the window that starts.
    const std::string music_now = !scene.music.empty() ? shorten(stem_of(scene.music), 18)
                                  : selected_ == 0     ? project_.soundtrack
                                                       : "carries on";
    gui::label({left + 80, y + 6}, music_now, 16.0f,
               scene.music.empty() ? gui::UI_INK_DIM : gui::UI_INK);
    if (gui::button({left + 240, y, 80, 26}, "Change")) {
        modal_ = Modal::Music;
        modal_scroll_ = 0.0f;
        gui::swallow_clicks();
    }
    gui::label({left + 330, y + 6}, "a track set here plays on into the next scenes", 14.0f,
               gui::UI_INK_DIM);

    y += 34;
    gui::heading({left, y + 6}, "SCENE TEXT");

    // The row buttons are placed from the right, so the alignment picker takes
    // whatever is left rather than running underneath them.
    const float buttons_left = area.x + area.width - 368;

    if (gui::button({area.x + area.width - 120, y, 106, 26}, "Delete", gui::UI_MAROON)) {
        delete_scene();
        return;
    }
    // Each of these rearranges project_.scenes, which leaves `scene` above
    // pointing at the wrong entry - or at freed memory, if inserting made the
    // vector grow. Nothing more is drawn from it this frame.
    if (gui::button({area.x + area.width - 232, y, 106, 26}, "Duplicate")) {
        duplicate_scene();
        return;
    }
    if (gui::button({area.x + area.width - 300, y, 62, 26}, "Down")) {
        move_scene(1);
        return;
    }
    if (gui::button({buttons_left, y, 62, 26}, "Up")) {
        move_scene(-1);
        return;
    }

    const float align_x = left + 110.0f;
    const float align_width = std::min(180.0f, buttons_left - align_x - 16.0f);
    if (align_width > 90.0f) {
        const int align_pick = gui::choice({align_x, y, align_width, 26}, ALIGN_NAMES,
                                           int(scene.align));
        if (align_pick >= 0) {
            scene.align = layout::Align(align_pick);
            apply_edit();   // the timeline carries the alignment, so it must be rebuilt
        }
    }

    y += 34;
    const std::string before = scene.text;
    gui::text_field({left, y, area.width - 28, 30}, scene.text, text_focused_);
    if (scene.text != before) apply_edit();
}

// --------------------------------------------------------------------- footer

void Editor::draw_footer(Rectangle area) {
    gui::panel(area);
    const float left = area.x + 14;
    const float y = area.y + 10;

    static const std::vector<std::string> packs = pack_names();

    // The right hand side is placed from the right edge inwards and gets first
    // claim on the room, since a render button that cannot be reached is worse
    // than a dropdown that has to be narrow.
    const Rectangle render = {area.x + area.width - 148, y + 12, 134, 26};
    if (gui::button(render, rendering_ ? "RENDERING" : "RENDER VIDEO", gui::UI_RED,
                    !rendering_ && !project_.scenes.empty())) {
        begin_render();
    }

    const float movie_width = 24.0f + gui::text_width("Movie mode", 16.0f);
    const float movie_x = render.x - movie_width - 16.0f;

    // This changes how the report is watched rather than what it says, so a
    // report already running is stopped instead of being switched under its own
    // feet - the music with it. Press PLAY again to see the difference.
    if (gui::toggle({movie_x, y + 12}, "Movie mode", project_.movie_mode)) {
        project_.movie_mode = !project_.movie_mode;
        stop_playback("Movie mode ");
    }

    // The three dropdowns then share whatever is left, shrinking together
    // rather than the last one sliding underneath the tick boxes.
    constexpr float WANTED = 368.0f;   // 130 + 14 + 110 + 14 + 100
    const float room = std::max(180.0f, movie_x - left - 16.0f);
    const float squeeze = std::min(1.0f, room / WANTED);

    const float pack_w = 130.0f * squeeze;
    const float fit_w = 110.0f * squeeze;
    const float typing_w = 100.0f * squeeze;
    const float fit_x = left + pack_w + 14.0f * squeeze;
    const float typing_x = fit_x + fit_w + 14.0f * squeeze;

    gui::label({left, y - 2}, "Soundtrack", 13.0f, gui::UI_INK_DIM);
    const int pack_pick = gui::dropdown({left, y + 12, pack_w, 26}, packs,
                                        index_of(packs, project_.soundtrack), soundtrack_open_);
    if (pack_pick >= 0) {
        project_.soundtrack = packs[pack_pick];
        apply_edit();   // a report already running picks the new score up
    }

    gui::label({fit_x, y - 2}, "Picture fit", 13.0f, gui::UI_INK_DIM);
    const int fit_pick = gui::dropdown({fit_x, y + 12, fit_w, 26}, FIT_MODES,
                                       index_of(FIT_MODES, project_.fit), fit_open_);
    if (fit_pick >= 0) {
        project_.fit = FIT_MODES[fit_pick];
        stage_.forget_pictures();
    }

    gui::label({typing_x, y - 2}, "Typing", 13.0f, gui::UI_INK_DIM);
    const int typing_pick = gui::dropdown({typing_x, y + 12, typing_w, 26}, TYPING_SPEEDS,
                                          index_of(TYPING_SPEEDS, project_.typing), typing_open_);
    if (typing_pick >= 0) {
        project_.typing = TYPING_SPEEDS[typing_pick];
        apply_edit();
    }

    // The filename takes whatever is left between the dropdowns and the ticks,
    // and simply disappears when there is not enough room to be useful.
    const float name_x = typing_x + typing_w + 18.0f;
    const float name_width = movie_x - name_x - 16.0f;
    if (name_width > 90.0f) {
        gui::label({name_x, y - 2}, "Save video as", 13.0f, gui::UI_INK_DIM);
        gui::text_field({name_x, y + 12, name_width, 26}, project_.output, output_focused_, 200);
    }

    if (rendering_) {
        const Rectangle bar = {left, area.y + area.height - 12, area.width - 28, 4};
        DrawRectangleRec(bar, gui::UI_EDGE);
        DrawRectangleRec({bar.x, bar.y, bar.width * render_progress_, bar.height}, gui::UI_AMBER);
    }

    gui::label({left, area.y + area.height - 30}, status_, 15.0f, gui::UI_INK_DIM);
}

// ---------------------------------------------------------------------- modal

void Editor::draw_modal() {
    if (modal_ == Modal::None) return;

    // The area the rest of the editor was laid out against this frame, which is
    // not what GetScreenWidth reports while fullscreen.
    const float width = layout_size_.x;
    const float height = layout_size_.y;
    DrawRectangle(0, 0, int(width), int(height), {0, 0, 0, 190});

    const Rectangle box = {width * 0.5f - 380, height * 0.5f - 260, 760, 520};
    gui::panel(box, gui::UI_BACKGROUND);
    DrawRectangleLinesEx(box, 1, gui::UI_EDGE);

    const char* title = modal_ == Modal::Picture ? "CHOOSE A PICTURE"
                        : modal_ == Modal::Sound ? "A SOUND FOR THIS SCENE"
                        : modal_ == Modal::Open  ? "OPEN A REPORT"
                                                 : "MUSIC FROM HERE";
    gui::heading({box.x + 16, box.y + 14}, title);

    if (gui::button({box.x + box.width - 100, box.y + 10, 86, 26}, "Cancel")) {
        modal_ = Modal::None;
        return;
    }

    if (modal_ == Modal::Open) {
        draw_recent_list(box);
        return;
    }

    // Whatever is being tried out gets a way to stop it. Without these the only
    // way to silence a two minute track was to sit through it.
    if (modal_ != Modal::Picture && preview_.playing()) {
        const float row = box.y + box.height - 40;
        if (gui::button({box.x + 16, row, 90, 26},
                        preview_.paused() ? "RESUME" : "PAUSE")) {
            preview_.toggle_pause();
        }
        if (gui::button({box.x + 114, row, 90, 26}, "STOP", gui::UI_MAROON)) preview_.stop();

        const Rectangle bar = {box.x + 216, row + 11, box.width - 232, 4};
        DrawRectangleRec(bar, gui::UI_EDGE);
        DrawRectangleRec({bar.x, bar.y, bar.width * preview_.progress(), bar.height},
                         gui::UI_AMBER);
    }

    // The list gives up its last row when there is a transport under it.
    const float taken = modal_ != Modal::Picture && preview_.playing() ? 48.0f : 0.0f;
    const Rectangle body = {box.x + 12, box.y + 48, box.width - 24, box.height - 100 - taken};

    if (modal_ == Modal::Picture) {
        const float tabs = box.y + box.height - 42;
        if (gui::button({box.x + 16, tabs, 150, 28}, "GAME FRAMES",
                        modal_tab_ == "default" ? gui::UI_CARD_ON : gui::UI_CARD)) {
            modal_tab_ = "default";
            modal_scroll_ = 0.0f;
        }
        if (gui::button({box.x + 174, tabs, 140, 28}, "MY ARTWORK",
                        modal_tab_ == "custom" ? gui::UI_CARD_ON : gui::UI_CARD)) {
            modal_tab_ = "custom";
            modal_scroll_ = 0.0f;
        }
        if (gui::button({box.x + box.width - 156, tabs, 140, 28}, "IMPORT PICTURES...")) {
            import_pictures();
        }

        const std::vector<std::string> found = pictures_in(modal_tab_);
        if (found.empty()) {
            gui::label({body.x + 8, body.y + 12},
                       modal_tab_ == "custom"
                           ? "Nothing here yet. IMPORT PICTURES to add your own."
                           : "The game's frames are missing from assets/images/default.",
                       16.0f, gui::UI_INK_DIM);
        }
        if (gui::hovering(body)) modal_scroll_ -= GetMouseWheelMove() * 50.0f;

        const int columns = 4;
        const float cell = body.width / columns;
        const float rows = std::ceil(found.size() / float(columns));
        modal_scroll_ = std::max(0.0f, std::min(modal_scroll_,
                                                std::max(0.0f, rows * 118.0f - body.height)));

        gui::begin_clip(body, modal_scroll_);
        for (size_t i = 0; i < found.size(); ++i) {
            const Rectangle cellbox = {body.x + cell * (i % columns),
                                       body.y + 118.0f * (i / columns), cell - 8, 110};
            const bool over = gui::hovering(cellbox);
            DrawRectangleRec(cellbox, over ? gui::UI_CARD_ON : gui::UI_CARD);

            draw_fitted(thumbnail(found[i]), {cellbox.x + 6, cellbox.y + 6, cellbox.width - 12, 74});
            gui::label({cellbox.x + 6, cellbox.y + 86}, shorten(stem_of(found[i]), 18), 13.0f,
                       gui::UI_INK);

            if (over && gui::clicked()) {
                if (picking_for_new_) {
                    add_scene(found[i]);
                } else if (selected_ >= 0) {
                    project_.scenes[selected_].image = found[i];
                    stage_.forget_pictures();
                }
                modal_ = Modal::None;
            }
        }
        gui::end_clip();
        return;
    }

    // A picker for effects lists effects; a picker for music lists music. The
    // two used to share one list with the other kind underneath, which made
    // both of them twice as long to read through.
    const bool music = modal_ == Modal::Music;
    const std::vector<std::string> found = sounds_of(music);

    if (gui::button({box.x + box.width - 172, box.y + box.height - 40, 156, 26},
                    music ? "IMPORT MUSIC..." : "IMPORT SOUNDS...")) {
        import_sounds();
    }

    if (gui::hovering(body)) modal_scroll_ -= GetMouseWheelMove() * 40.0f;
    const float content = (found.size() + 2) * 32.0f;
    modal_scroll_ = std::max(0.0f, std::min(modal_scroll_, std::max(0.0f, content - body.height)));

    gui::begin_clip(body, modal_scroll_);
    float y = body.y;

    // Clearing the choice, said plainly rather than as a name with a dash.
    const std::string none_label =
        !music           ? "Play no sound in this scene"
        : selected_ == 0 ? "Use the project soundtrack, chosen at the bottom of the window"
                         : "Keep playing whatever the scene before was playing";
    if (gui::button({body.x, y, body.width, 30}, none_label)) {
        if (selected_ >= 0) {
            if (music) project_.scenes[selected_].music.clear();
            else project_.scenes[selected_].sound.clear();
            apply_edit();
        }
        modal_ = Modal::None;
    }
    y += 40;

    if (found.empty()) {
        gui::label({body.x + 4, y}, music ? "No music here. Bring some in with IMPORT MUSIC."
                                          : "No effects here. Bring some in with IMPORT SOUNDS.",
                   16.0f, gui::UI_INK_DIM);
    }

    for (const std::string& name : found) {
        if (gui::button({body.x, y, body.width - 90, 28}, stem_of(name))) {
            if (selected_ >= 0) {
                // Both kinds keep the full path from the moment they are
                // picked. Music used to keep the bare name, which saved as a
                // name the browser editor could not find - the same report
                // played its music here and silence there.
                const std::string path =
                    FileExists(name.c_str()) ? name : paths_.sounds + "/" + name;
                if (music) {
                    project_.scenes[selected_].music = path;
                } else {
                    project_.scenes[selected_].sound = path;
                }
                apply_edit();
            }
            modal_ = Modal::None;
        }
        if (gui::button({body.x + body.width - 84, y, 80, 28}, "listen")) {
            const Clip* clip = mixer_.load(name);
            if (clip != nullptr && !clip->empty()) preview_.play(*clip);
        }
        y += 32;
    }

    gui::end_clip();
}

void Editor::draw_recent_list(Rectangle box) {
    const Rectangle body = {box.x + 12, box.y + 48, box.width - 24, box.height - 100};

    if (gui::button({box.x + 16, box.y + box.height - 40, 190, 26}, "OPEN ANOTHER FILE...")) {
        const std::vector<std::string> chosen =
            ask_for_files("Open a report", "Reports|*.json");
        if (!chosen.empty()) {
            open_project(chosen[0]);
            modal_ = Modal::None;
            return;
        }
    }

    if (!recent_.empty() &&
        gui::button({box.x + box.width - 156, box.y + box.height - 40, 140, 26},
                    "CLEAR THE LIST", gui::UI_MAROON)) {
        recent_.clear();
        save_recents();
    }

    if (recent_.empty()) {
        gui::label({body.x + 4, body.y + 10}, "Nothing opened yet.", 17.0f, gui::UI_INK);
        gui::label({body.x + 4, body.y + 36},
                   "Reports you open or save show up here, newest first.", 15.0f,
                   gui::UI_INK_DIM);
        return;
    }

    if (gui::hovering(body)) modal_scroll_ -= GetMouseWheelMove() * 45.0f;
    const float row_height = 46.0f;
    modal_scroll_ = std::max(0.0f, std::min(modal_scroll_,
                                            std::max(0.0f, recent_.size() * row_height -
                                                               body.height)));

    gui::begin_clip(body, modal_scroll_);
    int remove_at = -1;

    for (size_t i = 0; i < recent_.size(); ++i) {
        // By value. Opening a report moves it to the front of this very list,
        // so a reference into it would be dangling by the time it was read back.
        const std::string path = recent_[i];
        const float y = body.y + float(i) * row_height;
        const bool here = FileExists(path.c_str());

        // A report that has been moved or deleted stays on the list, greyed and
        // labelled. Quietly dropping it would leave you wondering where it went.
        if (gui::button({body.x, y, body.width - 46, 38},
                        std::string(GetFileName(path.c_str())), gui::UI_CARD, here)) {
            open_project(path);
            modal_ = Modal::None;
        }

        gui::label({body.x + 10, y + 22}, shorten(path, 92), 12.0f,
                   here ? gui::UI_INK_DIM : gui::UI_RED_HOVER);
        if (!here) {
            gui::label({body.x + body.width - 250, y + 6}, "moved or deleted", 13.0f,
                       gui::UI_RED_HOVER);
        }

        if (gui::button({body.x + body.width - 40, y, 38, 38}, "x", gui::UI_MAROON)) {
            remove_at = int(i);
        }
    }

    gui::end_clip();

    if (remove_at >= 0) forget_recent(size_t(remove_at));
}

// ----------------------------------------------------------------- main loop

void Editor::run() {
    while (!WindowShouldClose()) {
        gui::begin_frame();

        // Escape leaves fullscreen rather than closing the editor - the exit key
        // is off, so nothing else answers it.
        if (IsKeyPressed(KEY_ESCAPE) && showing_bare()) fullscreen_wanted_ = true;

        const Vector2 moved = GetMouseDelta();
        if (moved.x != 0.0f || moved.y != 0.0f) pointer_moved_at_ = GetTime();

        player_.update();
        preview_.update();
        booth_.update();
        poll_render();

        // A sound tried out in a picker stops when the picker does, however it
        // was closed - cancelled, chosen from, or clicked away.
        if (modal_ == Modal::None && preview_.playing()) preview_.stop();

        // The layout is written small and drawn scaled up, so a big window
        // enlarges the editor rather than stranding it in unreadable type at
        // the top left. Scale by whichever axis is tighter, or growing one
        // direction pushes the other off the edge - height is the one that
        // bites, since a wide short window would lose its footer entirely.
        const float screen_w = float(GetScreenWidth());
        const float screen_h = float(GetScreenHeight());
        const float zoom = std::max(1.0f, std::min(screen_w / DESIGN_WIDTH,
                                                   screen_h / DESIGN_HEIGHT));
        const float width = screen_w / zoom;
        const float height = screen_h / zoom;
        layout_size_ = {width, height};

        // Filling the screen shows nothing but the stage, the way the game does.
        const bool bare = showing_bare();

        // Two reasons to take the real pointer away: the stage is drawing the
        // game's own one in its place, or the screen has been left still long
        // enough for the close button to have faded out.
        if (game_pointer_showing_ || (bare && GetTime() - pointer_moved_at_ > 2.5)) {
            HideCursor();
        } else {
            ShowCursor();
        }

        render_stage_texture();

        BeginDrawing();
        ClearBackground(gui::UI_BACKGROUND);
        gui::begin_scale(zoom);

        // A panel on top takes the clicks. Everything under it is still drawn,
        // but it is behind glass: otherwise a press meant for a row in the
        // picker also lands on whatever inspector button happens to sit at the
        // same spot, and a scene gets duplicated or deleted on the way past.
        //
        // Whether one was already open is remembered here, before any button
        // has had a chance to open one. A panel opened during this frame keeps
        // the swallow its own button asked for, so the press that opened it is
        // not read again inside it.
        const bool panel_was_open = modal_ != Modal::None;
        if (panel_was_open) {
            gui::swallow_clicks();
            // A footer dropdown left open would keep drawing its list above
            // the panel and taking the clicks meant for it.
            soundtrack_open_ = fit_open_ = typing_open_ = false;
        }

        if (bare) {
            draw_stage({0, 0, width, height});
        } else {
            DrawRectangle(0, 0, int(width), 44, gui::UI_PANEL);
            gui::label({20, 12}, "PARODY, PLEASE", 24.0f, gui::UI_AMBER);
            gui::label({230, 20}, "scene editor", 15.0f, gui::UI_INK_DIM);

            if (gui::button({width - 100, 8, 86, 28}, "Save as")) save_project_as();
            if (gui::button({width - 194, 8, 86, 28}, "Save")) save_project();
            if (gui::button({width - 288, 8, 86, 28}, "Open")) {
                load_recents();
                modal_ = Modal::Open;
                modal_scroll_ = 0.0f;
                gui::swallow_clicks();
            }
            if (gui::button({width - 382, 8, 86, 28}, "New")) new_project();

            const float footer_height = 92.0f;

            // The scene list takes a share of the width rather than a fixed
            // 300 px. On a small window that fixed figure ate nearly half the
            // editor and left the stage squashed into what was over.
            const float list_width = std::clamp(width * 0.26f, 210.0f, LIST_WIDTH);
            const float right = list_width + 16.0f;
            const float right_width = width - list_width - 24.0f;

            // The inspector grows a row when the selected scene's sound-timing
            // picker will not fit beside Choose - same test the inspector runs.
            const bool timing_row =
                selected_ >= 0 && selected_ < int(project_.scenes.size()) &&
                !project_.scenes[selected_].sound.empty() &&
                !sound_when_fits(right_width,
                                 project_.scenes[selected_].sound_at == SoundAt::Delayed);
            const float inspector_height = 156.0f + (timing_row ? 34.0f : 0.0f);

            draw_scene_list({8, 52, list_width, height - 60});
            draw_stage({right, 52, right_width,
                        height - 60 - footer_height - inspector_height - 16});
            draw_inspector({right, height - footer_height - inspector_height - 8, right_width,
                            inspector_height});
            draw_footer({right, height - footer_height, right_width, footer_height - 8});
        }

        if (panel_was_open) gui::allow_clicks();   // the panel on top gets them
        draw_modal();
        gui::draw_deferred();  // open dropdown lists sit above everything
        gui::end_scale();
        EndDrawing();

        // Between frames, with nothing half drawn and no transform in effect.
        if (fullscreen_wanted_) {
            fullscreen_wanted_ = false;
            toggle_fullscreen(windowed_size_, windowed_at_, windowed_maximised_);
            ShowCursor();  // never leave it hidden over the editor
            pointer_moved_at_ = GetTime();
        }
    }
}

}  // namespace parody
