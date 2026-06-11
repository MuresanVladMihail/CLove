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

`love_filesystem_read`, `write`, `append`, `exists`, `remove`, `rename`,
`state`, `enumerate`. (PHYSFS-backed; writes go to the save directory.)

## Timer — `src/fhapi/timer.c`

`love_timer_getTime`, `getDelta`, `getFPS`, `getAverageDelta`,
`love_timer_sleep(ms)`.

## UI — `src/fhapi/ui.c`

An immediate-mode GUI (microui): windows, panels, buttons, checkboxes, sliders,
textboxes, labels, tree nodes, popups and a row/column layout system
(`love_ui_begin` / `love_ui_end`, `love_ui_button`, `love_ui_slider`, ...).

## System / misc — `src/fhapi/love.c`

`love_getVersion() -> [major, minor, revision, codename]`.

---

When in doubt about an exact signature or argument order, read the binding in
`src/fhapi/<module>.c`: each function validates its arguments up front, so the
`fh_set_error("Expected ...")` messages double as the spec.
