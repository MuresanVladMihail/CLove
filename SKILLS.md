# CLove scripting reference (FH)

What you can build with CLove and the script API to do it. Functions are grouped
by module; the authoritative, always-current list of each module's functions is
the `c_funcs[]` table at the bottom of the matching `src/fhapi/<module>.c`.

Lua games use the same capabilities under the `love.*` namespace (see
`src/luaapi/`); this document uses the FH `love_*` spelling.

## The shape of a game

```php
fn love_load() {
    # called once; return value is passed to love_update/love_draw as the game
    # state ("self")
    let self = {};
    self.img = love_graphics_newImage("player.png");
    return self;
}

fn love_update(dt, self) {
    # called every frame; dt is seconds since last frame
}

fn love_draw(self) {
    # called every frame after update
    love_graphics_draw(self.img, 100, 100);
}

fn main() {}   # required entry point
```

Optional window settings live in `config.fh`:

```php
fn love_config(c) {
    c.window_title  = "My Game";
    c.window_width  = 800;
    c.window_height = 600;
    c.window_vsync  = true;
    # window_resizable, window_fullscreen, window_icon, window_bordless ...
}
```

## Lifecycle & input callbacks (define the ones you need — all optional except love_load/update/draw)

- `love_load()` / `love_update(dt, self)` / `love_draw(self)`
- `love_focus(focused)` , `love_quit()`
- `love_keypressed(key)` , `love_keyreleased(key)` , `love_textinput(text)`
- `love_mousepressed(x, y, button)` , `love_mousereleased(...)` , `love_wheelmoved(y)`
- `love_joystickpressed(...)` , `love_joystickreleased(...)`

`love_event_quit()` ends the game; `love_event_reload()` requests a reload.

## Graphics — `src/fhapi/graphics*.c`

- **Drawing & transforms** (`graphics.c`): `love_graphics_draw`,
  `love_graphics_clear`, `love_graphics_push` / `love_graphics_pop`,
  `love_graphics_translate` / `rotate` / `scale` / `shear` / `origin` / `reset`.
- **Primitives** (`graphics_geometry.c`): `love_geometry_rectangle`, `circle`,
  `line`, `polygon`, `points`.
- **Images** (`image.c`): `love_graphics_newImage(path | imagedata)`,
  `love_graphics_newImageData(w, h)`, `love_image_setPixel(d,x,y,r,g,b,a)`,
  `love_image_getPixel(d,x,y) -> [r,g,b,a]`, width/height/filter/wrap getters.
- **Quads** (`graphics_quad.c`): `love_graphics_newQuad(x,y,w,h,sw,sh)` (stores
  normalised coords), `love_quad_setViewport(q,x,y,w,h)`,
  `love_quad_getViewport(q) -> [x,y,w,h]`.
- **Text** (`graphics_font.c`, `graphics_bitmapfont.c`): `love_graphics_newFont`,
  `love_graphics_setFont` / `getFont`, `love_graphics_print(text, x, y, ...)`.
- **Sprite batches** (`graphics_batch.c`): `newSpriteBatch` + `batch_add` /
  `batch_set` / `batch_bind` / `batch_unbind` / `batch_flush` / `batch_clear`.
- **Canvas / render-to-texture** (`graphics_canvas.c`): `love_graphics_newCanvas`,
  `love_graphics_setCanvas`.
- **Shaders** (`graphics_shader.c`): `love_graphics_newShader`,
  `love_graphics_setShader`, `love_shader_send`.
- **Meshes** (`graphics_mesh.c`) and **particle systems**
  (`graphics_particlesystem.c`).
- **Window** (`graphics_window.c`): `love_window_getWidth` / `getHeight` /
  `getDimensions`, `setTitle`, `setVsync`, `setIcon`, `getDisplayCount`, ...

All of the above need the GL context (i.e. a window) and so only run inside a
real game, not headless.

## Audio — `src/fhapi/audio.c`

`love_audio_newSource(path, "static" | "stream")` then `love_audio_play`,
`pause`, `resume`, `stop`. Vorbis (.ogg) and Wav are supported; streaming is for
.ogg. (mojoAL/OpenAL over SDL.)

## Input — `src/fhapi/{keyboard,mouse,joystick}.c`

Poll state with `love_keyboard_isDown(key)`, `love_mouse_getX/getY`,
`love_mouse_isDown(button)`, `love_joystick_*`, or react via the callbacks above.

## Math — `src/fhapi/math.c`

`love_math_noise(x [, y, z, w])` — simplex noise in [-1, 1], 1–4 dimensions.
(General arithmetic, arrays, maps, closures, strings come from the FH language
itself — see the FH docs under `src/3rdparty/FH`.)

## Filesystem — `src/fhapi/filesystem.c`

PHYSFS-backed; reads see both the game's source (archive or directory) and the
save directory, writes go to the save directory only.

- **Read/write:** `love_filesystem_read(name)`, `write(name, data)`,
  `append(name, data)`, `exists(name)`, `remove(name)`,
  `rename(old, new)`.
- **Directories:** `love_filesystem_mkDir(name)`, `isDir(name)`,
  `isSymLink(name)`, `enumerate(path) -> [names]` (list a directory's
  contents; requires `setIdentity` to have been called first).
- **Metadata:** `love_filesystem_getInfo(path) -> [type, size, modtime,
  accesstime, createtime]` (`type` is `"file"`, `"directory"`, `"symlink"` or
  `"other"`; returns `false` if the path doesn't exist), `love_filesystem_state(name [, mode])`
  (`mode` is one of `"e"` exists, `"x"` executable, `"w"` writable, `"r"`
  readable, `"rw"`/`"wr"` both; defaults to `"e"`).
- **Source & identity:** `love_filesystem_setSource(path)` / `getSource()`
  set/get the directory or archive CLove reads the game from;
  `love_filesystem_setIdentity(name)` picks the save directory (under the
  OS's per-user app-data location) and `love_filesystem_getSaveDirectory([company, project])`
  returns its full path — call `setIdentity` before any write, `mkDir`, or
  `enumerate` call. `love_filesystem_getUsrDir()` and
  `love_filesystem_getCurrentDirectory()` return the platform user directory
  and the process's current working directory, respectively.

## Timer — `src/fhapi/timer.c`

`love_timer_getTime`, `getDelta`, `getFPS`, `getAverageDelta`,
`love_timer_sleep(ms)`.

## UI — `src/fhapi/ui.c`

An immediate-mode GUI (microui): windows, panels, buttons, checkboxes, sliders,
textboxes, labels, tree nodes, popups and a row/column layout system
(`love_ui_begin` / `love_ui_end`, `love_ui_button`, `love_ui_slider`, ...).

## System / misc — `src/fhapi/love.c`

`love_getVersion() -> [major, minor, revision, codename]`.

## Configuration (`config.fh`) — `src/fhapi/config.c`

If present, `config.fh` must define `love_config(c)`; it's called once before
the window exists, and CLove reads whichever of the following keys are set on
`c` (all optional):

| Key | Type | Effect |
| --- | --- | --- |
| `window_title` | string | Sets the window title. |
| `window_width`, `window_height` | number | Sets the window size (each defaults to the other's current value if only one is given). |
| `window_min_width`, `window_min_height` | number | Sets the window's minimum size. |
| `window_max_width`, `window_max_height` | number | Sets the window's maximum size. |
| `window_x`, `window_y` | number | Sets the window's screen position. |
| `window_bordless` | bool | Removes the window border/decorations. |
| `window_resizable` | bool | Allows the user to resize the window. |
| `window_vsync` | bool | Enables/disables vsync. |
| `window_destroy` | bool | If true, destroys the window immediately (`graphics_shutdown()`) — for running headless. |
| `window_icon` | string (path) | Loads and sets the window icon. |
| `window_fullscreen` | bool | Toggles fullscreen (uses the `"fullscreen"` type when true). |
| `window_fullscreentype` | string | Enables fullscreen with a specific type (passed through to the platform). |
| `version` | string | Expected CLove version; if it doesn't match the running version, a warning is logged (the game still runs). |

There is no separate `config.fh`-level default object — any key you don't set
is simply left at the engine's built-in default.

---

When in doubt about an exact signature or argument order, read the binding in
`src/fhapi/<module>.c`: most binding functions validate their arguments up
front, so `fh_set_error("Expected ...")` / `"Illegal parameter, expected ..."`
messages usually double as a spec — but treat that as guidance, not a
guarantee, since some functions accept variable argument counts by design
and a few may not validate everything.
