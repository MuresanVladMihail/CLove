# microui

Vendored copy of [rxi/microui](https://github.com/rxi/microui) — the tiny
immediate-mode UI library behind CLove's `ui` module (`src/ui/`,
`src/fhapi/ui.c`).

Pinned at upstream commit `0850aba860959c3e75fb3e97120ca92957f9d057`.
Only `src/microui.c` and `src/microui.h` are needed; CMake globs
`src/3rdparty/microui/src/*.c` (see `CMakeLists.txt`).

This used to be a git submodule with no `.gitmodules` entry, so a fresh clone
got an empty directory and the build failed on the missing `microui.h`. It is
now tracked as plain files, like the other vendored dependencies (see
CLAUDE.md).

Licence: MIT, see `licenses/microui` at the repository root.
