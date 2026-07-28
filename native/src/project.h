// Reading and writing a report.
//
// The same json the Python editor and the browser use, so a project moves
// between all three without conversion.

#pragma once

#include <string>

#include "timeline.h"

namespace parody {

// Where the assets and the writable folders live. Resolved once at startup so
// the app works the same run from a build folder or unpacked next to the exe.
struct Paths {
    std::string root;      // the folder holding assets/ and projects/
    std::string assets;
    std::string images;
    std::string sounds;
    std::string sprites;
    std::string projects;
    std::string ffmpeg;

    static Paths discover(const char* argv0);
};

// Turn a stored path into something that can be opened, relative to the root.
std::string resolve(const Paths& paths, const std::string& stored);

// Store a path relative to the root when it sits inside it, so projects stay
// portable between machines.
std::string relative_to(const Paths& paths, const std::string& absolute);

bool load_project(const Paths& paths, const std::string& file, Project& into, std::string& error);
bool save_project(const Paths& paths, const std::string& file, const Project& project);

// The report shown on launch, so the editor never opens empty.
std::string sample_project(const Paths& paths);

}  // namespace parody
