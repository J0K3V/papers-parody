#include "file_dialog.h"

#include <cstring>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <commdlg.h>

namespace parody {
namespace {

// A filter for the common dialogs is a run of name/pattern pairs, each ending
// in a null, with a second null closing the list. This turns the readable
// "Pictures|*.png;*.jpg" form into that.
std::string to_win32_filter(const std::string& readable) {
    std::string out;
    const size_t bar = readable.find('|');
    if (bar == std::string::npos) {
        out.append("All files");
        out.push_back('\0');
        out.append("*.*");
        out.push_back('\0');
    } else {
        out.append(readable.substr(0, bar));
        out.push_back('\0');
        out.append(readable.substr(bar + 1));
        out.push_back('\0');
        out.append("All files");
        out.push_back('\0');
        out.append("*.*");
        out.push_back('\0');
    }
    out.push_back('\0');
    return out;
}

// Room for a long multiple selection: the folder, then a name per file.
constexpr size_t BUFFER = 64 * 1024;

}  // namespace

std::vector<std::string> ask_for_files(const std::string& title, const std::string& filter) {
    std::vector<char> buffer(BUFFER, '\0');
    const std::string patterns = to_win32_filter(filter);

    OPENFILENAMEA panel{};
    panel.lStructSize = sizeof(panel);
    panel.hwndOwner = GetActiveWindow();
    panel.lpstrFilter = patterns.c_str();
    panel.lpstrFile = buffer.data();
    panel.nMaxFile = DWORD(buffer.size());
    panel.lpstrTitle = title.c_str();
    panel.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT |
                  OFN_NOCHANGEDIR;

    std::vector<std::string> chosen;
    if (!GetOpenFileNameA(&panel)) return chosen;

    // One file comes back as a whole path. Several come back as the folder,
    // a null, then each name with its own null.
    const char* at = buffer.data();
    const std::string first = at;
    at += first.size() + 1;

    if (*at == '\0') {
        chosen.push_back(first);
        return chosen;
    }

    while (*at != '\0') {
        const std::string name = at;
        chosen.push_back(first + "\\" + name);
        at += name.size() + 1;
    }
    return chosen;
}

std::string ask_where_to_save(const std::string& title, const std::string& filter,
                              const std::string& suggested) {
    std::vector<char> buffer(BUFFER, '\0');
    std::strncpy(buffer.data(), suggested.c_str(), buffer.size() - 1);
    const std::string patterns = to_win32_filter(filter);

    OPENFILENAMEA panel{};
    panel.lStructSize = sizeof(panel);
    panel.hwndOwner = GetActiveWindow();
    panel.lpstrFilter = patterns.c_str();
    panel.lpstrFile = buffer.data();
    panel.nMaxFile = DWORD(buffer.size());
    panel.lpstrTitle = title.c_str();
    panel.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    panel.lpstrDefExt = "json";

    if (!GetSaveFileNameA(&panel)) return "";
    return buffer.data();
}

}  // namespace parody
