# CLove examples (Lua)

The Lua backend is **off by default**. Build with it enabled first:

```sh
mkdir -p build-lua && cd build-lua
cmake -DUSE_LUA=ON ../ && make -j4
```

Then run an example from this directory:

```sh
cd opt/examples/lua
../../../build-lua/clove
```

CLove loads `main.lua` from the current working directory, and `conf.lua`
next to it for the window settings. So only `main.lua` runs directly — the
other files here are separate programs. To try one, copy it over `main.lua`
(or rename it) in a scratch directory:

```sh
mkdir -p /tmp/try && cp *.png conf.lua /tmp/try/
cp mesh.lua /tmp/try/main.lua
cd /tmp/try && /path/to/clove
```

Key names are the ones in `src/keyboard.c` — `"escape"`, not `"esc"`.

## What is here

| | |
| --- | --- |
| `main.lua` | sprite batches: 150 000 sprites, with `use_batch` at the top to compare against one draw call each |
| `demo_rectangle.lua` | shapes and input |
| `mesh.lua` | a mesh with per-vertex colour |
| `font.lua` | fonts and text metrics |
| `example2.lua` | images, quads and transforms |
| `filesystem_example.lua` | `love.filesystem` |
| `joystick_demo.lua` | gamepad input |
| `net.lua` | `love.net` |
| `testThread.lua`, `thread_main.lua` | threads |
| `3d simple example.lua`, `model_viewer.lua`, `main_model_viewer.lua` | a small software 3D renderer built on the 2D API |
| `all_input_func_example.lua` | every input callback, printing what it receives |
| `boot.lua` | what the engine runs before your `main.lua` |
| `package/` | packaging a game into a `.clove.tar` and loading it back |

The FH examples in [`../fh`](../fh) are the maintained set, and cover more of
the engine. This directory is the Lua mirror of the older ones.
