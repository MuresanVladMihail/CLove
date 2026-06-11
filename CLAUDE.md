# CLAUDE.md

Guidance for Claude Code (and humans) working in the CLove repository.

## Project overview

CLove is a 2D game framework in C with OpenGL, in the spirit of LÖVE (Love2D).
Games are written as scripts — in **FH** (the default, a fast scripting language
vendored at `src/3rdparty/FH`) or in **Lua** — and the C engine provides
graphics, audio, input, filesystem, fonts, a UI toolkit and more. Tested on
macOS, Linux, Windows and Web (emscripten).

A game is a directory with a `main.fh` (and optional `config.fh`). The engine
calls lifecycle callbacks the script defines: `love_load`, `love_update(dt)`,
`love_draw`, plus optional input/window callbacks. See SKILLS.md for the full
scripting surface.

## Build

```sh
./build_osx.sh        # macOS  (cmake + make in build/)
./build_linux.sh      # Linux
./build_web.sh        # emscripten
# Windows: see README.md (MinGW + CMake)
```

Or directly:

```sh
mkdir -p build && cd build && cmake ../ && make -j4
```

The binary is `build/clove`. Run a game by launching `clove` from the game's
directory (it loads `main.fh` from the current working directory), or pass a
packaged `.love` archive: `clove game.love`.

`USE_FH` (default ON) and `USE_LUA` select the scripting backend(s) — see the
`option(...)` lines near the top of `CMakeLists.txt`.

## Testing

```sh
./tests/run_tests.sh            # auto-detects build/clove; exit 0 iff all pass
```

Tests live in `tests/fh/` and run through the real engine. `test_*.fh` assert
via FH's `error()` and print `CLOVE_TEST_OK`; `xfail_*.fh` are expected to fail
(they lock in the binding argument-validation guards). See `tests/README.md`.
The runner relies on the process exit code, which `main()` propagates from
`fh_main_activity_load` (0 = clean, 1 = script/engine error).

## Architecture

```
src/
  main.c               entry; dispatches to fh_ or lua_ main activity
  fh_mainactivity.c    FH game loop: init, config.fh, main.fh, love_* callbacks,
                       SDL event pump, shutdown (clove_finish)
  lua_mainactivity.c   the Lua equivalent
  fhapi/               FH <-> C bindings, one module per file (graphics*, audio,
                       image, filesystem, timer, math, keyboard, mouse,
                       joystick, ui, event, love, config). Lua mirror: luaapi/
  graphics/            OpenGL rendering: window/context, batch, font, canvas,
                       shader, mesh, quad, particlesystem, geometry, image
  audio/               OpenAL (via mojoAL/SDL) static + streaming sources
  image/               CPU-side image data (pixel get/set, load/save)
  math/                vectors, matrices, random, noise, triangulation
  filesystem/, timer/, net/, tools/, ui/   supporting modules
  3rdparty/            vendored deps: FH, SDL2, mojoAL, microtar, slre, microui,
                       physfs, glew, noise, CMath, stb
```

Engine functions are plain C (`graphics_*`, `audio_*`, ...). Each `fhapi/<m>.c`
wraps them as script-callable functions and registers them with
`fh_<m>_register(prog)` (called from `fh_mainactivity.c`).

## The vendored FH interpreter

`src/3rdparty/FH` is a full copy of the FH language
(https://github.com/MuresanVladMihail/FH), tracked here as plain files (no inner
`.git`). To upgrade it, sync `src/` and `tests/` from an FH checkout
(`rsync -a --exclude='*.o' --exclude='.DS_Store' .../FH/src/ src/3rdparty/FH/src/`)
and rebuild.

FH's public API is `src/3rdparty/FH/src/fh.h`. Notes for binding authors:
- Values carry separate integer and float types. `fh_get_number(v)` reads
  **either** (int or float) as a double; `fh_new_number(n)` makes a float.
  Prefer these in bindings so a script passing `10` vs `10.0` both work.
- `fh_optnumber` / `fh_optinteger` also convert across int/float.
- `fh_function_exists(prog, name)` — use before calling optional script
  callbacks so a missing function isn't a per-frame error.
- CMake globs `FH/src/*.c` + map/vec/regex/crypto subdirs but **not**
  `FH/src/tar`; FH's `mtar_*` calls link against CLove's own
  `src/3rdparty/microtar` (the two microtar headers must stay identical).

## Writing bindings (conventions)

A binding has the signature
`int fn(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args)`.

- **Validate `n_args` before indexing `args[]`.** Reading `args[k]` without
  checking the count is an out-of-bounds read (several such bugs have been
  fixed — the `xfail_*` tests guard against their return).
- Check types with `fh_is_number`, `fh_is_string`, `fh_is_c_obj_of_type(...)`.
- Return failures with `fh_set_error(prog, "...")` (returns -1), never by
  crashing.
- Wrap engine objects with `fh_new_c_obj(prog, ptr, free_cb, TYPE_ID)`; provide
  a free callback and return `null` (not a c_obj over `NULL`) when the object
  is absent.
- Register new functions in the module's `c_funcs[]` table and, if it's a new
  module, call its `fh_*_register` from `fh_mainactivity.c`.

## Platform gotchas

- **macOS shutdown:** the bundled SDL 2.0.8 CoreAudio backend blocks ~15s
  closing the audio device at exit (`mojoAL alcCloseDevice` → SDL, and via
  `SDL_Quit`). `clove_finish()` in `fh_mainactivity.c` therefore `_exit()`s on
  `__APPLE__` after the cheap teardown and lets the OS reclaim audio/SDL.
  Other platforms keep the full clean teardown. Upgrading the vendored SDL2 is
  the real fix.
- Build artifacts (`build/`, `cmake-build-debug/`) and the local `glew-old/`
  backup are gitignored; don't commit them.
```
