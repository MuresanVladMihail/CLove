# CLove examples (FH)

Each directory is a complete CLove program. Run one by building the engine
and launching it from inside that directory:

```sh
./build_osx.sh                       # or build_linux.sh / build_web.sh
cd opt/examples/fh/hello
../../../../build/clove
```

CLove loads `main.fh` from the current working directory, so the directory
you are in *is* the game. `escape` quits every example here.

`assets/` holds the art, audio and fonts the examples share, one copy each;
they reach it as `../assets/<file>`. An example meant to be packaged into a
`.love` archive should keep its own assets instead, the way `editor/` and
`game/` do.

## The examples

Roughly in the order they are worth reading.

| | what it shows |
| --- | --- |
| [`hello`](hello) | the smallest complete program: `love_load`, `love_update(dt, state)`, `love_draw(state)`, and how `state` gets from one to the others |
| [`config`](config) | `config.fh`, the window settings CLove reads before it opens a window — the file next to `main.fh` lists every key |
| [`draw_image`](draw_image) | `love_graphics_draw`: position, rotation about an origin, scale, shear, tint, flip, and drawing part of an image through a Quad |
| [`shapes`](shapes) | `love.geometry`: circles, rectangles, points and polygons, filled or outlined |
| [`font`](font) | the built-in font, a TTF at a chosen size, a bitmap font, and the metric calls (`getWidth`, `getWrap`, ascent/descent) |
| [`audio`](audio) | a static source with volume, pitch, looping and pause — and where state has to live so a key event can reach it |
| [`batch`](batch) | sprite batches: 50 000 sprites in one draw call, with a key held down to compare against one call each |
| [`canvas`](canvas) | render-to-texture: draw once into a canvas, then draw the canvas |
| [`particles`](particles) | particle systems: five presets covering the emission areas, the size and colour curves, spin, and `moveTo` for trails |
| [`shaders`](shaders) | GLSL through LOVE's `position`/`effect` pair, `extern` uniforms, and `love_shader_send` |
| [`mesh`](mesh) | your own vertices, with per-vertex colour and texture coordinates |
| [`vector_art`](vector_art) | `.svg` loaded like any other image, re-rasterized as it is drawn bigger |
| [`physics`](physics) | Box2D through `love.physics`: a world, bodies, fixtures, and a mouse joint to drag them |
| [`ui`](ui) | the microui widget set: windows, buttons, sliders, tree nodes, popups |
| [`editor`](editor) | the 2D level editor built on `opt/packages/editor` — a real application, not a demo |
| [`game`](game) | a game that embeds that editor behind F1 and reloads the level live |

`editor/` and `game/` carry their own `art/` and a `packages` symlink, so
they run and package on their own.

## Writing one

Every callback is optional — CLove checks whether the function exists before
calling it, so an example only defines what it uses:

```fh
fn love_load()            { ... return state; }   # state reaches the two below
fn love_update(dt, state) { ... }
fn love_draw(state)       { ... }
```

The input callbacks (`love_keypressed`, `love_mousepressed`,
`love_wheelmoved`, `love_textinput`, `love_resize`, `love_focus`,
`love_quit`, ...) do **not** receive that state — they take only their own
arguments. Anything a key event has to touch therefore lives in a
module-level map, as in [`audio`](audio); a global `let` binding is constant
in FH, but the map it points at is not.

Poll a held key with `love_keyboard_isDown("left")`; handle a press once in
`love_keypressed(key)`, where `key[0]` is the name. The names are the ones in
`src/keyboard.c` — `"escape"`, not `"esc"`.

The full scripting surface is in [SKILLS.md](../../../SKILLS.md).
