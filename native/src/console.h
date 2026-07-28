// Talking to a console that may or may not be there.
//
// The editor is built as a windowed program, so double-clicking it does not
// flash up a black box behind the window. That also means it starts with no
// console at all - and `ParodyPlease.exe --render report.json` run from a
// terminal would print its progress into nothing.
//
// Kept away from raylib: windows.h declares its own Rectangle, CloseWindow and
// ShowCursor, and the two do not compile in the same file.

#pragma once

namespace parody {

// Borrow the terminal this was launched from, if there is one, so printing
// works. Does nothing when there is no console to borrow.
void use_parent_console();

}  // namespace parody
