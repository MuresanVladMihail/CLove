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

## Lifecycle & input callbacks (all optional — define the ones you need)

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
- **Images** (`image.c`): `love_graphics_newImage(path | imagedata [, scale])`,
  `love_graphics_newImageData(w, h)`, `love_image_setPixel(d,x,y,r,g,b,a)`,
  `love_image_getPixel(d,x,y) -> [r,g,b,a]`, width/height/filter/wrap getters.
- **Vector art** (`image.c`, `graphics/svg.c`): a `.svg` path given to
  `love_graphics_newImage()` is loaded as vector art - same function, same
  `love_graphics_draw()`, see the section below.
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

## Vector art (SVG) — `src/graphics/svg.c`

```fh
self.logo = love_graphics_newImage("logo.svg");   # nothing special
love_graphics_draw(self.logo, 100, 100, 0, 8, 8); # still sharp at 8x
```

`love_graphics_newImage()` recognises `.svg` files and loads them as vector
art: the drawing is kept around and rasterized on demand, so drawing the image
bigger (through `sx`/`sy`, or through `love_graphics_scale()`) re-rasterizes it
instead of magnifying pixels. Everything else — quads, filters, wrap, batches,
`love_image_getPixel` — keeps working on the pixels as usual.

- The image reports the size the drawing was **authored** at
  (`love_image_getWidth/getHeight/getDimensions`), not the size of the current
  rasterization, so layout code does not change when the resolution does.
- Re-rasterization is quantised to powers of two and capped at 4096 px on the
  longest side, so a growing sprite re-rasterizes a handful of times, not every
  frame.
- `love_graphics_newImage(path, scale)` pins the scale instead (`1` = the
  authored size, `2` = twice the resolution, ...): no automatic work at all.
- `love_image_setVectorScale(img, scale)` pins it later, `scale = 0` hands the
  image back to automatic; `love_image_getVectorScale(img)` reads it back and
  `love_image_isVector(img)` says whether the image is vector art at all.
- Sprite batches, meshes and particle systems draw the texture the image
  currently holds — pin a scale with `newImage(path, scale)` when a vector
  image feeds one of those.
- `love_image_getPixel` reads the rasterization made when the image was loaded,
  not the one currently on the GPU.
- Editing the pixels of a vector image (`love_image_refresh` with plain image
  data) turns it into an ordinary raster image, as you would expect.

### Inkscape

Save as **Plain SVG** or **Inkscape SVG** — both load; Inkscape's extra
attributes are ignored. The renderer is nanosvg, which covers paths, shapes,
groups, transforms, strokes (dashes, caps, joins), solid fills, gradients,
opacity and clip paths, but **not**:

- `<text>` — pick the text and use *Path → Object to Path* before saving.
- masks and filters (blur, drop shadow) — flatten them in Inkscape
  (*Filters → ... → apply*, or *Edit → Make a Bitmap Copy* for effects you
  cannot flatten).
- `<use>` — *Edit → Clone → Unlink Clone* turns those into real shapes.
- embedded or linked bitmaps (`<image>`) — export those as PNG and draw them
  as a separate image.

`clip-path` is supported, including `clipPathUnits="objectBoundingBox"`,
intersection of nested `<g clip-path>` levels, and a clip path defined after
the shapes that use it. Two deliberate departures from the spec: a `clip-path`
pointing at an id that does not exist leaves the shape **unclipped** rather
than hiding it, and `clip-path` on a `<clipPath>` itself is ignored.

### Files that came through cairo

An `.svg` produced by cairo — a PDF, EPS or AI file converted to SVG, or
Inkscape's *Save a Copy* through its cairo backend — needs the clip path
support above to render at all correctly, because cairo never fills the real
outline of a gradient shape. It emits a **rectangle** covering the gradient and
puts the actual outline in a `<clipPath>`:

```xml
<g clip-path="url(#clip-0)">
  <path fill="url(#linear-pattern-0)" d="M 0 0 L 400 0 L 400 200 L 0 200 Z"/>
</g>
```

Two things follow. Re-saving such a file from Inkscape does **not** undo this —
Inkscape imports the clipped groups and writes them back unchanged, so Plain
SVG and Inkscape SVG look identical to the renderer. And cairo writes every
colour as a fractional percentage (`rgb(76.861572%, 89.4104%, 41.175842%)`),
which older nanosvg releases could not parse and turned into flat grey.

To see whether a file uses the trick: `grep -c '<g clip-path' file.svg`.

Sizes are read as CSS pixels at 96 dpi, which is exactly Inkscape's user unit,
so a document sized in mm (Inkscape's default) comes out at the size the
document properties show.

Runnable example: `opt/examples/fh/vector_art` (the same drawing pinned at 1x
next to the automatic one, zooming in and out).

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
