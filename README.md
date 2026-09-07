![Alt text](opt/CLoveLogo.png?raw=true "CLove")

CLove
=====
CLove is a simple, easy to learn and use 2D game framework tested on
Mac/Linux/Windows/Web, made in C with OpenGL. Games are scripted in
[FH](https://github.com/MuresanVladMihail/FH) — a fast scripting language built for CLove —
or in Lua.

The bundled FH interpreter (vendored under `src/3rdparty/FH`) ships with a
register-based VM, separate integer/float types, fused loop opcodes, an
adaptive garbage collector and a small-object allocator — on the bundled
benchmarks it outruns Lua 5.4, Python and Ruby.

There is a level editor, a particle system, Box2D 3 physics, tweening, SVG
that stays sharp as you scale it, image loading on worker threads, and a
`pcall` that lets a game survive a bad level file instead of closing on the
player. Everything below is a screenshot of something in
[`opt/examples/fh`](opt/examples/fh) — run any of them yourself in two
commands.

How to build
============

On Linux and OS X:
- Download and install cmake, gcc, g++ (optional git). On OSX you can install these using brew.
- Go inside CLove/ and call `./build_linux.sh` or `./build_osx.sh`
- On Debian based you must have installed:
  ```
  sudo apt-get install freeglut3 freeglut3-dev libglew-dev
  libglu1-mesa libglu1-mesa-dev libgl1-mesa-glx libgl1-mesa-dev libasound2-dev
  libaudio-dev libesd0-dev libpulse-dev libroar-dev
  ```
- On Redhat: mesa-libGL mesa-libGL-devel and stable/devel versions of mesa glu

On Windows:
- Download and install mingw and let the setup configure the path for you.
  Open up the CMD and type gcc; if you get an error then type:
  `setx PATH "%PATH%;C:\MinGW\bin;"` — that command will add the bin folder to the path.
  After that check if you got gcc & g++ installed.
- Download and install CMake and let the setup configure the path for you.
- Make a new directory called build inside CLove and call:
  `cmake ../ -DCMAKE_C_COMPILER:PATH=C:/MinGW/bin/gcc.exe -DCMAKE_CXX_COMPILER:PATH=C:/MinGW/bin/g++.exe`.
  If this command does not work then go in C:\Program Files\CMake\bin and open up
  cmake-gui.exe. Tell it where CLove is and where you want to build the project.
  Also do not forget to use a custom compiler that is set to GCC as C compiler and
  G++ as the C++ compiler. After that use the make command and that's it (if you
  get errors that make does not exist then paste this into the CMD terminal:
  `copy c:\MinGW\bin\mingw32-make.exe c:\MinGW\bin\make.exe`)
- Download DX SDK 2010 only if you get errors from SDL when building:
  http://www.microsoft.com/en-us/download/details.aspx?id=6812
- SDL2 and audio (MojoAL) are built and linked statically into `clove.exe`, so
  there are no `.dll` files to copy next to it after building.

For Web:
- Install emscripten (1.38.15, commit 7a0e27441eda6cb0e3f1210e6837cae4b080ab4c) and add it to your path.
- Run `./build_web.sh`. When it's done you will have "clove.bc" and other files.
  The .bc file is used for linking with your C/C++ programs, the rest is used for Lua.
- For testing you have the following options:
  1. Copy the generated files into /var/www/html/ and run localhost (you must have apache2 installed)
  2. Open a terminal and run: `python3 -m http.server`. Run index.html and go to localhost:8000

NOTE:
CLove might work with other versions of emscripten but I haven't tested!
Inside the build scripts you can disable/enable Lua and Physfs.
When you want to release a project you must modify build_web.sh to preload your files.

Running a game
==============
Run `clove` from the directory that contains your `main.fh` (and optionally
`config.fh`), or pass a packaged game: `clove mygame.love`. See `opt/examples/fh`
for ready-to-run FH examples and `opt/examples/lua` for the Lua ones.

Features
========
- Scripting languages: FH and Lua
- Can be used as a shared or static library.
- Native C support.
- Easy to learn and use api.
- Cross platform (Linux, MacOS, Web and Windows).
- Custom package format.
- Powerful Batch system.
- Powerful Particle system.
- Image loading and drawing.
- Vector art (SVG, e.g. straight out of Inkscape) loaded and drawn like any
  other image, re-rasterized as it is scaled up so it stays sharp.
- Image creation from scratch or from a template & save (png, bmp, tga).
- Meshes.
- Render To Texture (canvas) system.
- Sound loading and playing (Vorbis and Wav).
- Streaming support for Vorbis files.
- Primitive drawing.
- UI module.
- Filesystem functions.
- OpenGL 3.3 core (OpenGL ES 2.0 on the web build).
- Networking: BSD-socket helpers in `src/net/net.c` (unix only, TCP over IPv4
  or IPv6). Not yet reachable from a script — there are no bindings for it, and
  the calls block, so they would stall the frame as written.
- Powerful font loading and drawing using batch system.
- Support for image fonts.
- Keyboard, mouse and joystick support.
- Tweening: 31 easings, chaining, looping and yoyo.
- Physics: Box2D 3.1.1 behind LÖVE-shaped bindings.
- Asynchronous image loading on a worker thread pool.
- Error handling: `pcall` in scripts, and an error screen for the player.

Examples
--------

FH:

The state returned by `love_load` is passed back to `love_update` and
`love_draw`. Callbacks like `love_keypressed`, `love_focus` or `love_quit`
are optional — define only the ones you need.

~~~php
# Example of drawing an image
fn love_load() {
    let self = {};
    self.image = love_graphics_newImage("image.png");
    return self;
}

fn love_update(dt, self) {
}

fn love_draw(self) {
    love_graphics_draw(self.image, 200, 200);
}

fn main() {}
~~~

~~~php
# Example of drawing some primitives
fn love_draw() {
    love_graphics_rectangle("fill", 100, 100, 32, 16);
    love_graphics_rectangle("line", 200, 200, 32, 32);
    love_graphics_circle("fill", 270, 200, 32, 16);
    love_graphics_circle("line", 300, 100, 32, 8);
}

fn main() {}
~~~

~~~php
# Example of playing music
fn love_load() {
    let self = {};
    self.ogg_music = love_audio_newSource("music.ogg", "stream");
    love_audio_play(self.ogg_music);
    return self;
}

fn main() {}
~~~

Lua:

~~~lua
-- Example of drawing an image
local image = love.graphics.newImage("image.png")

function love.draw()
    love.graphics.draw(image, 200, 200)
end
~~~

~~~lua
-- Example of drawing some primitives
function love.draw()
    love.graphics.rectangle("fill", 100, 100, 32, 16)
    love.graphics.rectangle("line", 200, 200, 32, 32)
    love.graphics.circle("fill", 270, 200, 32, 16)
    love.graphics.circle("line", 300, 100, 32, 8)
end
~~~

~~~lua
-- Example of playing music
local ogg_music = love.audio.newSource("music.ogg")
function love.load()
    ogg_music:play()
end
~~~

Editor support
--------------

`tools/Sublime/CLove` is a Sublime Text package for CLove games: highlighting of
the whole `love_*` API, completions for every binding and `config.fh` key, and
a build system that runs `clove`. It builds on the FH package
(`tools/Sublime/FH` in the [FH repository](https://github.com/MuresanVladMihail/FH)),
which is required -- see `tools/Sublime/CLove/README.md`.

Contact
-------

1. GitHub issues: https://github.com/MuresanVladMihail/CLove/issues
1. Email: muresanvladmihail@gmail.com

What it looks like
------------------

Every picture here is one of the examples in [`opt/examples/fh`](opt/examples/fh),
taken by `tools/make_screenshots.sh` so they can be remade rather than left to
go stale. Run any of them with two commands:

```sh
cd opt/examples/fh/particles
../../../../build/clove
```

### The editor — [`opt/examples/fh/editor`](opt/examples/fh/editor)

A 2D level editor written in FH on top of [`opt/packages/editor`](opt/packages/editor):
hierarchy, inspector, undo/redo, physics bodies and fixtures, JSON scenes, and
a console. [`opt/examples/fh/game`](opt/examples/fh/game) embeds it behind F1
and reloads the level live.

![The CLove editor](opt/data/example_editor.png?raw=true "opt/examples/fh/editor")

### Particles — [`particles`](opt/examples/fh/particles)

![Particle system](opt/data/example_particles.png?raw=true "opt/examples/fh/particles")

### Physics — [`physics`](opt/examples/fh/physics)

Box2D 3.1.1 through `love.physics`: a world, bodies, fixtures and a mouse joint
to drag them with. [`joints`](opt/examples/fh/joints) covers every joint kind.

![Box2D physics](opt/data/example_physics.png?raw=true "opt/examples/fh/physics")

### Vector art — [`vector_art`](opt/examples/fh/vector_art)

An `.svg` loads like any other image and is re-rasterized as it is drawn
bigger, so it stays sharp. On the left the same drawing pinned to one
resolution, for comparison.

![SVG re-rasterized as it scales](opt/data/example_vector_art.png?raw=true "opt/examples/fh/vector_art")

### Shaders — [`shaders`](opt/examples/fh/shaders)

GLSL through LÖVE's `position`/`effect` pair, with `extern` uniforms.

![GLSL shaders](opt/data/example_shaders.png?raw=true "opt/examples/fh/shaders")

### Tweening — [`tweens`](opt/examples/fh/tweens)

`love.tween` is CLove's own — LÖVE has no equivalent. All 31 easings drawn as
curves, plus chaining, delays, looping and yoyo.

![All 31 easings](opt/data/example_tweens.png?raw=true "opt/examples/fh/tweens")

### UI — [`ui`](opt/examples/fh/ui)

The microui widget set: windows, buttons, sliders, tree nodes and popups.

![The UI module](opt/data/example_ui.png?raw=true "opt/examples/fh/ui")

### Meshes and noise — [`mesh`](opt/examples/fh/mesh), [`noise`](opt/examples/fh/noise)

![Textured mesh](opt/data/example_mesh.png?raw=true "opt/examples/fh/mesh")
![Simplex noise](opt/data/example_noise.png?raw=true "opt/examples/fh/noise")

### When a game breaks

A script error used to print a traceback to a terminal the player very likely
does not have open, and the window would just disappear. Now it lands here —
the message, where it happened, the whole call stack, and a key to copy it all
so it can be pasted into a bug report.

![The CLove error screen](opt/data/error_screen.png?raw=true "the error screen")

Scripts can catch failures themselves with `pcall`, which works on CLove's own
bindings too:

~~~php
let r = pcall(love_graphics_newImage, "level3/bg.png");
if (r.ok) { self.bg = r.value; }
else      { self.bg = love_graphics_newImage("art/missing.png"); }
~~~

Set `error_screen = false` in `config.fh` for a game that would rather draw its
own, or set `CLOVE_NO_ERROR_SCREEN=1` in the environment (the test runner does).

### Elsewhere

CLove on the web build, on Linux and on OS X:

![Web](opt/data/1.png?raw=true "Web")
![Linux](opt/data/2.png?raw=true "Linux")
![Os X](opt/data/3.png?raw=true "Os X")

Contribuitors
-------------
1. MasterGeek


License
-------

CLove comes with two licenses which you can choose from:

Copyright © 2015 - 2026 Mureșan Vlad Mihail

Contact Info muresanvladmihail@gmail.com

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

The origin of this software must not be misrepresented; you must not claim that you wrote the original software. Shall you use this software in a product, an acknowledgment and the contact info(if there is any) of the author(s) must be placed in the product documentation.

This notice may not be removed or altered from any source distribution.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

OR: 

Copyright © 2015 - 2026 Mureșan Vlad Mihail

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
