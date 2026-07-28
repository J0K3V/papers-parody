// The system's own open and save panels.
//
// Kept behind plain strings and away from raylib: windows.h declares its own
// Rectangle, CloseWindow and ShowCursor, and including both in one translation
// unit does not compile. Nothing here mentions a raylib type, so the two never
// have to meet.

#pragma once

#include <string>
#include <vector>

namespace parody {

// Files to bring in. Empty if the panel was closed without choosing.
// `filter` is a description and its patterns, e.g. "Pictures|*.png;*.jpg".
std::vector<std::string> ask_for_files(const std::string& title, const std::string& filter);

// Where to write something. Empty if cancelled. `suggested` fills the name box.
std::string ask_where_to_save(const std::string& title, const std::string& filter,
                              const std::string& suggested);

}  // namespace parody
