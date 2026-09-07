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
                       joystick, physics, ui, event, love, config).
                       Lua mirror: luaapi/
  graphics/            OpenGL rendering: window/context, batch, font, canvas,
                       shader, mesh, quad, particlesystem, geometry, image, svg
  asset/               the worker-thread image loader behind love.asset
  audio/               OpenAL (via mojoAL/SDL) static + streaming sources
  image/               CPU-side image data (pixel get/set, load/save)
  math/                vectors, matrices, random, noise, triangulation
  physics/             the love.physics object model over Box2D 3
  filesystem/, timer/, net/, tools/, ui/   supporting modules
  include/             CLove's own headers, plus vendored stb_image.c,
                       stb_image_write.h and stb_vorbis.c/.h
  3rdparty/            vendored deps: FH, SDL3, box2d, mojoAL, microtar, slre,
                       microui, physfs, glew, noise, CMath
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

## Physics (Box2D)

`src/3rdparty/box2d` is Box2D **3.1.1**, vendored as plain sources
(`include/box2d/*.h` + `src/*.c`) from https://github.com/erincatto/box2d. To
upgrade, replace those two directories from a release tarball; there is nothing
patched in it. Box2D 3 is pure C, so the bindings call `b2*` directly.

It is built as its **own CMake target** rather than folded into the engine
glob, because it needs C17 (`_Static_assert`, anonymous unions) and the Linux
build puts `-std=c99` in `CMAKE_C_FLAGS`. `include_directories()` adds its
headers globally so the static `love` library can compile `src/physics/`
without linking it, the same arrangement freetype has.

`src/physics/physics.c` (+ `src/include/physics.h`) is only the object model
LÖVE's API needs on top of Box2D:

- **Pixels in, pixels out.** Scripts never see metres. `physics_scaleDown` /
  `physics_scaleUp` convert with `love_physics_setMeter()` (30 by default).
  Lengths and forces scale once, torque and rotational inertia twice.
- **A child retains its parent** (fixture -> body -> world; joint -> world and
  both bodies), and a parent keeps a *weak* list of live children only so
  `world:getBodies()` can enumerate them. This is LÖVE's lifetime model: an
  unreferenced body is collected and leaves the world.
- **Every Box2D object carries its CLove wrapper in `b2*_GetUserData`**, which
  is how a contact event or `b2Body_GetShapes()` gets back to a script object.
- **A destroyed object stays addressable.** Box2D 3's ids are generation
  checked, so `physics_*_isValid()` can tell, and the bindings turn a call on a
  destroyed object into a script error rather than a crash — that is what
  `tests/fh/xfail_physics_destroyed_body.fh` locks in.
- A chain shape becomes a `b2Chain`, not a `b2Shape`; `physics_Fixture` carries
  both id kinds and `fixture_shape_id()` in the binding rejects the operations a
  chain cannot do.

The API's differences from LÖVE 11 (no gear/pulley joints, callbacks by name,
number/string user data) are listed in SKILLS.md.

## Local fixes in the vendored FH

Two bugs in `src/3rdparty/FH` had to be fixed here; both are latent in FH
itself, so re-apply them if you re-sync from an FH checkout (better: push them
upstream first).

- `program.c`, `fh_add_c_func()` / `fh_get_c_func_by_name()`: the name map used
  to hold a **pointer** into the `c_funcs` stack. That stack is one contiguous
  array that is realloc'd as it grows, so every pointer handed out before a
  growth dangled afterwards. It held together only while realloc happened to
  extend in place; registering the ~216 physics functions was enough to move it
  and make even `error()` resolve as "unknown variable or function". The map now
  stores a 1-based index.
- `vm.c`, `fh_call_vm_function()`: the new frame's register window was computed
  from `prev_frame->closure->func_def->n_regs`, and fell back to register **0**
  when the frame it nested inside had no closure — which is exactly the case
  when a C function calls back into the script (a collision callback fired from
  `world:update()`). It now starts at `prev_frame->stack_top`, which is the
  right bound for both frame kinds.

## SDL3, and the vendored mojoAL is no longer patched

`src/3rdparty/SDL3` is SDL **3.4.16**, vendored as the release tarball minus
its `test/` and `examples/` directories (and its own `CLAUDE.md`/`AGENTS.md`,
which would otherwise be picked up as instructions for this repository).
`src/3rdparty/mojoAL/mojoal.c` is upstream (https://github.com/icculus/mojoAL)
**unmodified** — CLove used to carry an implementation of `AL_SEC_OFFSET` /
`AL_SAMPLE_OFFSET` / `AL_BYTE_OFFSET` there because upstream left them as
`FIXME("offsets")`; upstream has since implemented them, and upstream mojoAL
is SDL3-only, which is what made the two upgrades one job.

Things to know before touching either:

- **CMake needs `OBJC` in `project()` on macOS**, because SDL3's macOS
  backends are Objective-C. Without it CMake fails at generate time with
  `Missing variable is: CMAKE_OBJC_COMPILE_OBJECT`.
- **SDL3's include path is `<SDL3/SDL.h>`**, and mojoAL includes it that way,
  so `src/3rdparty/SDL3/include` is on the global include path — the static
  `love` library compiles SDL-using sources without linking SDL, the same
  arrangement freetype and box2d have.
- **There is no `SDLmain` any more.** `SDL_main.h` is header-only; `src/main.c`
  defines a plain `main()` and links only `SDL3-static`.
- **The return convention flipped.** `SDL_Init`, `SDL_InitSubSystem`,
  `SDL_SetWindowFullscreen` and friends return `true` on success where SDL2
  returned `0`. These compile silently and behave backwards: the joystick
  subsystem's `!= 0` check took the failure branch on *success*, and
  `SDL_Init(...) < 0` could never be true, so a failed video init went
  unnoticed. Grep for `SDL_` with `!= 0` or `< 0` after any re-sync.
- **`SDL_GetBasePath()` returns a string SDL owns**; SDL2's was the caller's to
  free. `filesystem_getSource()` keeps its own copy because
  `filesystem_setSource()` `free()`s what is there.
- **Window events are their own event types now** (`SDL_EVENT_WINDOW_*`)
  rather than one `SDL_WINDOWEVENT` with a sub-field, and a key event carries
  `event.key.key` rather than `event.key.keysym.sym`.
- **`SDL_CreateWindow()` takes no position** — set it afterwards with
  `SDL_SetWindowPosition()`. Fullscreen split in two: a `bool` for whether,
  and `SDL_SetWindowFullscreenMode()` for which kind (`NULL` = the borderless
  desktop one).
- `SDL_oldnames.h` in SDL3's includes is the full rename table, and the
  compiler points at it by name (`SDL_AtomicGet_renamed_SDL_GetAtomicInt`),
  which is what makes a re-sync mostly mechanical.

**One upstream mojoAL bug CLove works around rather than patching.**
`source_set_offset()` reads `AL_SEC_OFFSET` as `((int) value) * freq *
framesize` — it truncates the seconds to a whole number *before* scaling, so
any seek inside the first second lands on zero and 1.75s lands on 1s.
`audio_StaticSource_seek()` therefore converts seconds to samples itself and
sets `AL_SAMPLE_OFFSET`, which has no such problem (samples are whole numbers)
and is the more precise thing to ask for anyway. `tests/fh/test_audio_wav.fh`
fails if that is undone. Do not patch the vendored mojoAL to fix it — report
it upstream; the whole point of this upgrade was to stop carrying a patch.

**The web build is not ported.** `build_web.sh` hand-lists its sources, and
that list had already drifted so far from the tree that it could not have
built (it compiles `src/main.cpp`, which does not exist, and knows nothing
about `src/fhapi` or FH). It also asks emscripten for SDL2; the SDL3 port
there is `--use-port=sdl3`, a different flag. The script carries a comment
saying all of this rather than a one-line flag change that would look ported
and would not be. Rebuilding it on top of `emcmake` and the real CMake build
is the fix.

## Asynchronous loading

`src/asset/asyncload.c` is an SDL_Thread pool that decodes images off the main
thread; `src/fhapi/asset.c` is `love.asset` on top of it. Two invariants:

- **The GL upload stays on the main thread.** A worker produces an
  `image_ImageData` — pixels in memory — and `love_asset_take()` turns that
  into a `graphics_Image`. A GL context belongs to one thread.
- **A `Job *` is only valid while the lock is held.** The job array is one
  contiguous block that `asyncload_requestImage()` reallocs as it grows, so a
  worker that unlocks and decodes must re-find its job by id afterwards
  (`findJob`), never keep the pointer. That is what the comment in
  `worker_main()` is about.

Vector art is refused on purpose: `src/graphics/svg.c` keeps one shared
`NSVGrasterizer`. `image_ImageData` grew an `ownsPath` flag for this — the
worker's copy of the path outlives the worker, so the ImageData frees it,
while the four ordinary constructors keep borrowing the caller's string.

## The error screen and screenshots

A script error used to print a traceback and close the window. `src/error/`
draws it instead, in the logo's colours (`#E33C77` body, `#FB78AC` frame), with
the message, its location, the call stack and a key to copy the lot. It runs
its own loop after the game's is over; the process still exits non-zero, so
tests and CI are unaffected.

- It is entered from `clove_fail()` in `fh_mainactivity.c`, which every fatal
  script path goes through. `fh_get_error()` renders into the buffer it
  returns, so it is called once and the result copied.
- **`tests/run_tests.sh` sets `CLOVE_NO_ERROR_SCREEN=1`.** Without it every
  `xfail_*` test would sit on a screen waiting for a keypress. `config.fh` can
  also switch it off with `error_screen = false`.
- Whatever the game left bound — a canvas, a shader, a scissor, a transform —
  is cleared first (`graphics_setCanvas(NULL)` then `graphics_reset()`), or
  none of it would draw.

Two environment variables take the pictures in README.md:
`CLOVE_SCREENSHOT=<path>` with an optional `CLOVE_SCREENSHOT_FRAME=<n>`
(see `screenshot_tick()` in `fh_mainactivity.c`) grabs one frame of a running
game and quits; `CLOVE_ERROR_SCREENSHOT=<path>` does the same for the error
screen. `tools/make_screenshots.sh` drives every example through them into
`opt/data/`. Re-run it after anything that changes how the examples look.

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

## Vector art

`.svg` files load through the ordinary image path: `image_ImageData` keeps the
parsed drawing (`src/graphics/svg.c`, nanosvg + nanosvgrast from
`src/include/`), `graphics_Image` takes it over in
`graphics_Image_new_with_ImageData()` and re-rasterizes it in
`graphics_Image_draw()` when the image is drawn bigger than its current
texture. Two invariants to keep in mind when touching that code:

- `graphics_Image.width/height` is the size the drawing was **authored** at and
  must not follow the texture; `texWidth/texHeight` is the texture.
- The `svg_Document` has exactly one owner. `image_ImageData_releaseVector()`
  hands it from the image data to the image, which frees it in
  `graphics_Image_free()`.

Scripting side and the Inkscape caveats: see SKILLS.md.

### The vendored nanosvg is patched — do not overwrite it blindly

`src/include/nanosvg.h` and `nanosvgrast.h` are upstream
(https://github.com/memononen/nanosvg) **plus a clip path implementation that
upstream does not have and has never had** (issue #141 is still open). Pulling
fresh copies from upstream silently removes it, and every `.svg` that came
through cairo — anything converted from PDF/EPS/AI — goes back to rendering its
gradient shapes as coloured rectangles, because cairo states those shapes as a
rectangle plus a `<clipPath>`. `tests/fh/test_svg_clip.fh` fails if the patch
is lost. To upgrade nanosvg, re-apply the patch on top:

- `nanosvg.h`: `NSVGclipPath` (public), `clipPaths`/`clipPathCount` on
  `NSVGshape`, `clipPaths` on `NSVGimage`, `NSVGclipRef` and the clip fields on
  `NSVGparser`, `clipPathIds`/`clipPathCount` on `NSVGattrib`,
  `nsvg__parseClipPath`, `nsvg__parseShapeElement` (the shape dispatch factored
  out so `<clipPath>` children reuse it), `nsvg__addClipRef`,
  `nsvg__clipPathForBounds`, `nsvg__resolveClipPaths`,
  `nsvg__scaleShapeGeometry`, the `clip-path`/`clip-rule` cases in
  `nsvg__parseAttr`, and clip-aware teardown (`nsvg__deleteShapes`).
- `nanosvgrast.h`: the `clip`/`clipMask`/`stencil`/`maskTarget`/`yMin`/`yMax`
  fields on `NSVGrasterizer`, `nsvg__rasterizeFill`, `nsvg__buildClipMask`, and
  the mask-blit plus clip-modulation branches in `nsvg__rasterizeSortedEdges`.

The masks are 8-bit coverage buffers built per shape and only over that shape's
device-space bounds, so the cost is proportional to what is actually clipped:
on a 2.9 MB, 1029-path cairo drawing it is ~5% of the rasterization time.

## Platform gotchas

- **macOS shutdown (fixed):** the bundled SDL 2.0.8 CoreAudio backend used to
  block ~15s closing the audio device at exit (`mojoAL alcCloseDevice` → SDL,
  and via `SDL_Quit`). This was fixed by upgrading the vendored SDL, first to
  2.32.10 and now to 3.4.16 (see `CHANGELOG.md`); `clove_finish()` in
  `fh_mainactivity.c` now
  runs the same full teardown (`audio_close()` before `graphics_shutdown()`'s
  `SDL_Quit()`) on every platform, with no `__APPLE__`-specific early exit.
- Build artifacts (`/build/`, `/cmake-build-*/`) and the local `glew-old/`
  backup are gitignored; don't commit them. Those rules are anchored to the
  repository root on purpose — unanchored patterns used to swallow vendored
  files (`src/3rdparty/glew/build/cmake/`, `src/3rdparty/SDL2/src/core/**`), so
  a fresh clone wouldn't configure. When adding to `.gitignore`, anchor the
  pattern and check `git status --ignored` afterwards.
- Every vendored dependency under `src/3rdparty/` is tracked as plain files —
  no submodules, no `.gitmodules` (microui used to be a stray gitlink and came
  out empty on a clone).
```
