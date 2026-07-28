#include "console.h"

#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace parody {

void use_parent_console() {
    // Output already going somewhere - a file, or a pipe into another program -
    // is left exactly as it is. Pointing the streams at the console here would
    // take it away from whatever was collecting it.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) return;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;

    // Point the standard streams at the borrowed console. Without this the
    // handles are still the empty ones a windowed program starts with, and
    // printing goes nowhere.
    FILE* unused = nullptr;
    freopen_s(&unused, "CONOUT$", "w", stdout);
    freopen_s(&unused, "CONOUT$", "w", stderr);
    freopen_s(&unused, "CONIN$", "r", stdin);

    // The shell has already printed its prompt and moved on, so start on a
    // fresh line rather than halfway along it.
    std::printf("\n");
}

}  // namespace parody
