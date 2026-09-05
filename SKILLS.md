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
- `love_mousepressed(m)` , `love_mousereleased(m)` , `love_wheelmoved(y)`
- `love_joystickpressed(...)` , `love_joystickreleased(...)`

The two mouse-button callbacks take **one** argument, an array `[x, y, button]`
— not three — with `button` one of `"l"`, `"r"`, `"m"`, `"wu"`, `"wd"`.

The engine pumps SDL events at the *end* of a frame, after `love_update` and
`love_draw` (`fh_main_loop` in `src/fh_mainactivity.c`), so a press reaches
these callbacks and is acted on by the next update. That matters for anything
click-precise: `love_mouse_isDown()` only reports the button's state at the
moment `love_update` ran, which drops a click whose press and release fall in
one frame and reads whatever position the pointer has drifted to since. Latch
the event and use the position it carries. `opt/examples/fh/editor` does this
for its gizmo handles.

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
  Metrics work without a font of your own: `love_font_getWidth(text)` and
  `love_font_getHeight()` measure with the default font, and
  `love_graphics_print` loads it on demand. Pass a font first
  (`love_font_getWidth(font, text)`) to measure with that one instead.
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

## Physics (Box2D) — `src/fhapi/physics.c`

LÖVE's `love.physics` on Box2D 3.x. Everything you pass or get back is in
**pixels and radians**; `love_physics_setMeter(px)` sets how many pixels make a
Box2D metre (30 by default, as in LÖVE), and the bindings convert both ways.

```fh
fn love_load() {
    g_world = love_physics_newWorld(0, 900);          # gravity in px/s^2
    love_world_setCallbacks(g_world, "on_hit", null); # function *names*

    let ground = love_physics_newBody(g_world, 400, 580, "static");
    love_physics_newFixture(ground, love_physics_newRectangleShape(800, 40), 1);

    let ball = love_physics_newBody(g_world, 400, 100, "dynamic");
    let f = love_physics_newFixture(ball, love_physics_newCircleShape(20), 1);
    love_fixture_setRestitution(f, 0.6);
    return {};
}

fn on_hit(a, b, contact) { println("bump"); }

fn love_update(dt, self) { love_world_update(g_world, 1.0 / 60.0); }
```

The object model is LÖVE's: a **World** holds **Bodies**, a Body carries
**Fixtures**, and a Fixture pairs a Body with a reusable **Shape**. **Joints**
connect two bodies. A child holds its parent alive, so — exactly as in LÖVE —
a body the script stops referencing is eventually collected and leaves the
world; keep your bodies in a variable.

- **Module:** `love_physics_setMeter` / `getMeter`, `newWorld(gx, gy [, sleep])`,
  `newBody(world, x, y [, "static"|"dynamic"|"kinematic"])`,
  `newCircleShape([x, y,] r)`, `newRectangleShape(w, h)` or
  `(x, y, w, h [, angle])`, `newPolygonShape(x1, y1, ... )` (3–8 points, loose
  numbers or one array), `newEdgeShape(x1, y1, x2, y2)`,
  `newChainShape(loop, x1, y1, ...)`, `newFixture(body, shape [, density])`.
- **Joints:** `newDistanceJoint`, `newRevoluteJoint`, `newPrismaticJoint`,
  `newWheelJoint`, `newWeldJoint`, `newMotorJoint`, `newMouseJoint`,
  `newFrictionJoint`, `newRopeJoint` — LÖVE's argument order.
- **World:** `update(world, dt [, subSteps])`, `setGravity` / `getGravity`,
  `setSleepingAllowed` / `isSleepingAllowed`, `getBodyCount` / `getBodies`,
  `getJointCount` / `getJoints`, `setCallbacks` / `getCallbacks`,
  `rayCast(x1,y1,x2,y2) -> [[fixture,x,y,nx,ny,fraction], ...]`,
  `rayCastClosest(...) -> [fixture,x,y,nx,ny,fraction]` or `null`,
  `queryBoundingBox(x1,y1,x2,y2) -> [fixture, ...]`, `destroy`, `isDestroyed`.
- **Body:** position (`getX`, `getY`, `getPosition`, `setPosition`,
  `setTransform`), `getAngle` / `setAngle`, velocities, `applyForce(fx, fy [, x,
  y])`, `applyLinearImpulse`, `applyTorque`, `applyAngularImpulse`, `getMass`,
  `getInertia`, `getMassData`, `resetMassData`, damping, `getGravityScale`,
  `setFixedRotation`, `setBullet`, `setAwake`, `setActive`, `getType` /
  `setType`, the local/world point and vector conversions, `getFixtures`,
  `getJoints`, `getWorld`, `get/setUserData`, `destroy`, `isDestroyed`.
- **Fixture:** density, friction, restitution, `isSensor`, the filter
  (`setCategory` / `getCategory` / `setMask` / `getMask` / `setGroupIndex`,
  `get/setFilterData`), `testPoint`, `rayCast`, `getBoundingBox`,
  `getMassData`, `getShape`, `getBody`, `get/setUserData`, `destroy`,
  `isDestroyed`.
- **Shape:** `getType` (`"circle"`, `"polygon"`, `"edge"`, `"chain"`),
  `getRadius`, `getPoint` (circle centre), `getPoints`, `getChildCount`.
- **Joint:** `getType`, `getBodies`, `getAnchors`, `getReactionForce`,
  `getReactionTorque`, `getCollideConnected`, `destroy`, `isDestroyed`, plus
  per-kind methods under `love_distancejoint_*`, `love_revolutejoint_*`,
  `love_prismaticjoint_*`, `love_wheeljoint_*`, `love_weldjoint_*`,
  `love_mousejoint_*`, `love_motorjoint_*`, `love_frictionjoint_*` and
  `love_ropejoint_*`.
- **Contact** (the third argument to a collision callback):
  `getNormal`, `getPositions`, `getFriction`, `getRestitution`, `isTouching`,
  `getFixtures`.

A collision callback is handed fixtures and nothing else, so put whatever
identifies the object in `body:setUserData()` and `fixture:setUserData()` when
you build the body. For a level authored in the editor that is the entity's
`id`, which is what turns a contact back into something the scene package can
look up (`opt/examples/fh/editor/world.fh`).

Where this differs from LÖVE 11, and why:

- **Callbacks are function names**, not function values: FH can only call a
  global function by name from C. `love_world_setCallbacks(world, begin, end)`
  takes two names or `null`; only begin/end contact exist, because Box2D 3
  reports contacts as events collected during the step and has no post-solve
  event of LÖVE's shape. They are dispatched right after `world:update()`.
- **No gear or pulley joints** — Box2D 3 dropped them. Friction and rope
  joints are built from the motor and distance joints, but behave as LÖVE's do
  and still report `"friction"` / `"rope"` from `joint:getType()`.
- **User data is a number or a string** (or `null`), not an arbitrary value:
  FH gives C no way to keep a script value alive across a garbage collection.
- **`fixture:setSensor()` can only confirm what a fixture already is.** Box2D 3
  fixes that at creation time.
- **Multiple return values come back as an array**, like everywhere else in
  CLove: `let g = love_world_getGravity(w); let gx = g[0];`.

Runnable example: `opt/examples/fh/physics` (a heap of falling shapes you can
drag around with a mouse joint).

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
  `enumerate` call. `love_filesystem_getUsrDir()` returns the platform user
  directory. **`love_filesystem_getCurrentDirectory()` does not return the
  current directory** despite the name -- it answers PhysFS's base directory,
  which is where the executable lives. For the directory the game was launched
  from, and against which its relative paths resolve, use
  `love_filesystem_getWorkingDirectory()`.

### Browsing outside PhysFS

Everything above goes through PhysFS, so it only ever sees the mounted source
and save directories. A tool -- a level editor picking an image, say -- has to
walk the actual machine, and these three do not go through PhysFS at all:

| Call | Returns |
| --- | --- |
| `love_filesystem_list(path)` | `[{ "name": string, "dir": bool }, ...]` for a real directory. Directories come first, then names, case-insensitively -- the order a file picker wants. `"."` and `".."` are left out. A directory that cannot be read (missing, no permission) gives `null` rather than raising, so a browser can show it empty and let the user walk back out. |
| `love_filesystem_getWorkingDirectory()` | The process's working directory, which is where CLove loaded `main.fh` from. |
| `love_filesystem_getHomeDirectory()` | The user's home directory, or `null`. Unlike `getUsrDir()` this needs no `setIdentity`. |

Image loading takes absolute paths too — `love_graphics_newImage` reaches the
file through `stdio`, not PhysFS — so a path chosen this way loads as it is.
`opt/examples/fh/editor/browser.fh` is a complete picker built on these.

## Timer — `src/fhapi/timer.c`

`love_timer_getTime`, `getDelta`, `getFPS`, `getAverageDelta`,
`love_timer_sleep(ms)`.

## UI — `src/fhapi/ui.c`

An immediate-mode GUI (microui): windows, panels, buttons, checkboxes, sliders,
textboxes, labels, tree nodes, popups and a row/column layout system
(`love_ui_begin` / `love_ui_end`, `love_ui_button`, `love_ui_slider`, ...).
Build a UI between `love_ui_begin()` and `love_ui_end()`; the engine draws it
after `love_draw`, so anything you draw yourself sits underneath.

A few widgets need their return value read the right way:

| Call | Returns |
| --- | --- |
| `love_ui_button(label, opt)` | `true` on the frame it was clicked. |
| `love_ui_checkbox(label, state, id)` | `true` on the frame it was clicked — the script keeps the state and flips it. |
| `love_ui_slider(value, low, high, step, [id], [opt], [decimals])` | The edited value, so `x = love_ui_slider(x, ...)` is the usual form. |
| `love_ui_number(value, step, [id], [opt], [decimals])` | The edited value, same as the slider but with no fixed range — drag to change, click to type. What an inspector wants for coordinates and sizes. |
| `love_ui_pushId(key)` / `love_ui_popId()` | Nothing. microui hashes a widget's *label* into its id, so two list rows showing the same text are the same widget; wrap each row in a push/pop of something unique (an entity id, a loop index) to keep them apart. |
| `love_ui_textbox(id, [seed_text], [opt])` | `[res, text]`. microui owns the buffer, keyed by `id`, so what the user types survives between frames; `seed_text` fills it the first time that id is seen. Compare `res` against `love_ui_res_state("submit")` / `"change"`. `love_ui_clear_textbox([id])` empties one box, or all of them when called with no argument. |

`decimals` is how many digits the value is shown with, 0 to 6, defaulting to
microui's own 2 — pass 0 for a field that holds a whole number, like a grid
step, so it does not read "20.00". It is a digit count rather than a printf
format on purpose: a format string coming from a script is a footgun.

The `id` on a check box, a slider, a number field and a text box is not
optional decoration: microui derives a widget's identity from the *address* of
the value it is handed, and every one of these bindings hands it the same stack
slot. Two widgets sharing an id also share one hover and one focus slot, and
what that looks like is a control that only responds when it happens to be the
last one drawn. Give each one a number of its own.

`opt` values come from `love_ui_opt("noclose" / "noframe" / "notitle" /
"noresize" / "popup" / "autosize")` and `love_ui_align("center" / "right")`;
they are microui's `MU_OPT_*` bits and combine by addition. Passing `notitle +
noresize` to `love_ui_begin_window` turns a window into a fixed panel — see
`opt/examples/fh/editor` for a docked-panel layout, and `opt/examples/fh/ui`
for the plain widget gallery.

Three more calls exist for the awkward corners of microui's state:

| Call | What it answers |
| --- | --- |
| `love_ui_mouse_over()` | Whether the pointer is over any window, panel or popup. A game drawing its own viewport underneath floating UI asks this before acting on a click, so a press meant for a window does not also land in the scene. |
| `love_ui_popup_open(name)` | Whether that popup is on screen. `love_ui_begin_popup` still answers true on the frame the popup is being dismissed, so it cannot tell you this by itself. Call it from the window that opened the popup — the name is hashed against the id stack. |
| `love_ui_setWindowOpen(name, open)` | Re-opens a window microui's own close button has latched shut. Once that button is pressed the container's open flag is 0 and `love_ui_begin_window` answers false for good. |

Two limits worth knowing: a window's rect is only applied the first time
microui sees that window (there is no binding to move a live one afterwards),
and a popup opened with `love_ui_open_popup` can only be dismissed by clicking
outside it.

**Every widget, layout and draw call needs a container.** `love_ui_rect`,
`love_ui_control_text`, `love_ui_text`, the layout calls and all the widgets
read the top of one of microui's stacks, and only `love_ui_begin_window`,
`love_ui_begin_panel` and `love_ui_begin_popup` push onto them. Calling one at
top level — from `love_draw`, say, to put a label next to the cursor — used to
abort the process on an assertion inside microui; it now raises an ordinary
script error, but the call still does nothing. Draw outside the UI with
`love_geometry_*` and `love_graphics_print` instead.

## Two transform gotchas

`love_graphics_setScissor(x, y, w, h)` passes its rectangle straight to
`glScissor`, whose origin is the **bottom** left of the window — every other
drawing call in CLove measures from the top left. To clip a region `top` pixels
down from the top, pass `y = window_height - top - h`.

`love_graphics_scale()` and `love_graphics_rotate()` rebuild the current matrix
from the module's own scale/rotation state rather than composing into it (see
`src/graphics/matrixstack.c`), so they are absolute setters, while
`love_graphics_translate()` is relative and composes. A LÖVE-shaped
`translate(); scale();` therefore loses the translation: scale first, then
translate by the offset divided by the scale. `opt/examples/fh/editor` builds
its camera that way.

## System / misc — `src/fhapi/love.c`

`love_getVersion() -> [major, minor, revision, codename]`.

FH's `json_stringify(value, [pretty])` takes an optional second argument:
`true` switches from the compact single line to indented output, which is what
you want for anything a human or a diff tool reads. Note that microui works in
`float`, so a number that has been through a slider or a number field comes
back as `0.40000000596046448` — quantise before storing it (see `round_to()`
in `opt/examples/fh/editor`).

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
