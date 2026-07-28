#include "project.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "json.hpp"
#include "mixer.h"
#include "raylib.h"

using nlohmann::json;

namespace parody {
namespace {

bool exists(const std::string& path) { return FileExists(path.c_str()) || DirectoryExists(path.c_str()); }

layout::Align align_from(const std::string& name) {
    if (name == "center" || name == "centre") return layout::Align::Centre;
    if (name == "right") return layout::Align::Right;
    return layout::Align::Left;
}

const char* align_name(layout::Align align) {
    switch (align) {
        case layout::Align::Centre: return "center";
        case layout::Align::Right: return "right";
        default: return "left";
    }
}

SoundAt sound_at_from(const std::string& name) {
    if (name == "typed") return SoundAt::Typed;
    if (name == "end") return SoundAt::End;
    if (name == "delayed") return SoundAt::Delayed;
    return SoundAt::Start;
}

const char* sound_at_name(SoundAt when) {
    switch (when) {
        case SoundAt::Typed: return "typed";
        case SoundAt::End: return "end";
        case SoundAt::Delayed: return "delayed";
        default: return "start";
    }
}

// A json value that may be null, which is how the editors write "no sound".
std::string text_or_empty(const json& node, const char* key) {
    if (!node.contains(key) || node[key].is_null()) return "";
    return node[key].get<std::string>();
}

}  // namespace

Paths Paths::discover(const char* argv0) {
    Paths paths;

    // Walk up from the executable looking for the assets folder, so it works
    // from native/build during development and from beside the exe once
    // packaged.
    std::string base = GetApplicationDirectory();
    for (int up = 0; up < 4; ++up) {
        if (exists(base + "/assets/fonts/pixelplay.ttf")) {
            paths.root = base;
            break;
        }
        base += "/..";
    }
    if (paths.root.empty()) paths.root = GetApplicationDirectory();
    (void)argv0;

    paths.assets = paths.root + "/assets";
    paths.images = paths.assets + "/images";
    paths.sounds = paths.assets + "/sounds/default";
    paths.sprites = paths.images + "/sprites";
    paths.projects = paths.root + "/projects";

    // ffmpeg is fetched separately because it is far too big for the repository.
    for (const std::string& candidate : {paths.root + "/ffmpeg.exe",
                                         paths.root + "/tools/ffmpeg.exe"}) {
        if (FileExists(candidate.c_str())) {
            paths.ffmpeg = candidate;
            break;
        }
    }
    if (paths.ffmpeg.empty()) paths.ffmpeg = "ffmpeg";

    return paths;
}

std::string resolve(const Paths& paths, const std::string& stored) {
    if (stored.empty()) return "";
    if (stored.size() > 1 && (stored[1] == ':' || stored[0] == '/' || stored[0] == '\\')) {
        return stored;  // already absolute
    }
    return paths.root + "/" + stored;
}

std::string relative_to(const Paths& paths, const std::string& absolute) {
    if (absolute.rfind(paths.root, 0) == 0 && absolute.size() > paths.root.size() + 1) {
        std::string cut = absolute.substr(paths.root.size() + 1);
        std::replace(cut.begin(), cut.end(), '\\', '/');
        return cut;
    }
    return absolute;
}

bool load_project(const Paths& paths, const std::string& file, Project& into, std::string& error) {
    std::ifstream stream(file);
    if (!stream) {
        error = "could not open " + file;
        return false;
    }

    // Reading the values throws just as readily as parsing does - a number
    // where a string belongs is enough - so both are inside the same try.
    // Outside it, a file someone had hand-edited took the whole editor down.
    Project project;
    try {
        json payload;
        stream >> payload;

        for (const json& node : payload.value("scenes", json::array())) {
            Scene scene;
            scene.image = resolve(paths, text_or_empty(node, "image"));
            scene.text = text_or_empty(node, "text");

            const std::string sound = text_or_empty(node, "sound");
            if (!sound.empty()) scene.sound = resolve(paths, sound);

                // A music value with a folder in it is a path - the browser
            // editor's "assets/sounds/default/..." or an import of your own -
            // and is anchored to the root like the sound above. A bare name
            // from an older report is left alone; the mixer looks those up in
            // the sounds folder itself.
            const std::string music = text_or_empty(node, "music");
            scene.music = music.find_first_of("/\\") != std::string::npos ? resolve(paths, music)
                                                                          : music;
            scene.align = align_from(node.value("align", std::string("left")));
            scene.sound_at = sound_at_from(node.value("sound_at", std::string("start")));
            scene.sound_delay = node.value("sound_delay", 0.0f);
            project.scenes.push_back(scene);
        }

        project.soundtrack = payload.value("soundtrack", std::string(default_pack()));
        project.fit = payload.value("fit", std::string("contain"));
        project.typing = payload.value("typing", std::string("normal"));
        project.next_button = payload.value("next_button", std::string("on"));
        project.output = payload.value("output", std::string("my-parody.mp4"));
        project.movie_mode = payload.value("movie_mode", true);
    } catch (const std::exception& problem) {
        error = problem.what();
        return false;
    }

    into = project;
    return true;
}

bool save_project(const Paths& paths, const std::string& file, const Project& project) {
    json payload;
    payload["version"] = 2;

    json scenes = json::array();
    for (const Scene& scene : project.scenes) {
        json node;
        node["image"] = relative_to(paths, scene.image);
        node["text"] = scene.text;
        if (scene.sound.empty()) {
            node["sound"] = nullptr;
        } else {
            node["sound"] = relative_to(paths, scene.sound);
        }
        if (scene.music.empty()) {
            node["music"] = nullptr;
        } else {
            // Stored the way the browser editor stores it - relative to the
            // root - so the same file names the same track in both.
            node["music"] = relative_to(paths, scene.music);
        }
        node["align"] = align_name(scene.align);

        // Only written when it is not the default, so projects made in the
        // browser stay byte for byte what they were.
        if (scene.sound_at != SoundAt::Start) {
            node["sound_at"] = sound_at_name(scene.sound_at);
        }
        if (scene.sound_at == SoundAt::Delayed) {
            node["sound_delay"] = scene.sound_delay;
        }
        scenes.push_back(node);
    }

    payload["scenes"] = scenes;
    payload["soundtrack"] = project.soundtrack;
    payload["fit"] = project.fit;
    payload["typing"] = project.typing;
    payload["next_button"] = project.next_button;
    payload["output"] = project.output;
    payload["movie_mode"] = project.movie_mode;

    std::ofstream stream(file);
    if (!stream) return false;
    stream << payload.dump(2) << "\n";
    return true;
}

std::string sample_project(const Paths& paths) {
    const std::string preferred = paths.projects + "/default.json";
    if (FileExists(preferred.c_str())) return preferred;

    FilePathList found = LoadDirectoryFilesEx(paths.projects.c_str(), ".json", false);
    std::string first;
    if (found.count > 0) first = found.paths[0];
    UnloadDirectoryFiles(found);
    return first;
}

}  // namespace parody
