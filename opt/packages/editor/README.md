# editor — the 2D editor, as something a game can switch on

```
include "packages/editor/editor.fh"
```

The editor is not a program; it is a thing a program turns on. Press a key in
your game, move a platform, press it again, and the game is already running the
new level. The level being edited and the level being played are the same
document in the same process — no file to save in between, no reload, no second
window.

`opt/examples/fh/game` is that, in about 200 lines. `opt/examples/fh/editor` is
the same editor hosted by a program that does nothing else.

## Lifecycle

| Call | What it does |
| --- | --- |
| `editor_new(opts)` | The editor state. `opts` may carry `"scene"` (the level to open, overriding what `editor.json` remembers), `"standalone"` (true when the editor *is* the program, so Esc quits it; embedded, Esc only switches it off) and `"active"` (whether it starts switched on). |
| `editor_update(ed, dt)` | A no-op while it is switched off. |
| `editor_draw(ed)` | Likewise. Call it *last*, so the panels sit over your game rather than under it. |
| `editor_toggle(ed)` / `editor_set_active(ed, on)` | Switching off hands the mouse back and stops the simulation if Play was running — the game underneath is about to want both. |
| `editor_active(ed)` | Whether it is on. |

## What a host reads

| Call | Returns |
| --- | --- |
| `editor_revision(ed)` | A number that moves on every edit, undo, redo and load, and on nothing else. Watch it to know the level changed under you; it deliberately does not move for a camera pan or a selection, so a game does not rebuild its world because someone scrolled. |
| `editor_document(ed)` | The live document. Hand it straight to `Scene()` from `packages/scene` — it takes a parsed map as readily as a path, so nothing goes through a file. |
| `editor_scene_path(ed)` | The level currently open. |

When to rebuild is the host's call. The game example does it on the way out of
the editor, which is what "edit the map, then see the change" means; a host
that wanted the change to land mid-edit would rebuild whenever the revision
moves instead.

## Input

CLove hands the input callbacks no state map, so the package parks each event
until the next `editor_update()` picks it up — and the *host* forwards them,
because FH has one global namespace and a game needs those names for itself:

```
fn love_mousepressed(m)  { editor_mousepressed(m); }
fn love_mousereleased(m) { editor_mousereleased(m); }
fn love_wheelmoved(y)    { editor_wheelmoved(y); }
fn love_quit()           { return editor_quit(); }

fn love_keypressed(k) {
    if (k[0] == "f1" && !k[2]) { editor_toggle(g.ed); return 0; }
    editor_keypressed(k);
}
```

`love_quit()` returning true aborts the quit, which is how the unsaved-changes
prompt catches the window's close button. Forward it or that prompt never
appears.

## Tiles

A map is painted, not placed. **Inspector > Tilemap** adds a spritesheet and
says how big one tile of it is — plus the margin around the sheet and the
spacing between two tiles, for one that came out of a packer. The **Tiles**
button opens that sheet as a palette over the viewport: click a tile there,
then click or drag in the scene to lay it down, `Shift` to rub it out. Pressing
Tiles again puts the palette away; it narrows with the viewport and gives up
when there is no room left for it.

Layers are drawn back to front, the way the hierarchy's z-order works for
entities, and any of them can be marked **solid** — Play then builds static
bodies from it, one per row of touching cells rather than one per tile.

**Brush -> selected sprite** gives whatever is selected the brush's tile as its
artwork: the same sheet, cropped to that one tile. A crate and the ground it
sits on can come out of the same file.

The document grows two fields for all this:

```json
"tilesets": [ { "id": 1, "name": "terrain.png", "path": "art/terrain.png",
                "tile_w": 32, "tile_h": 32, "margin": 0, "spacing": 0 } ],
"tilemap":  { "tile_w": 32, "tile_h": 32,
              "layers": [ { "id": 1, "name": "Ground", "visible": true,
                            "solid": true, "cells": { "3,-1": [1, 4, 2] } } ] }
```

A cell is `[tileset id, column, row]` — where the tile *is in the sheet*, not
an index into it, so cropping the sheet or swapping it for one a tile wider
does not repaint the level behind your back. Reading it back from a game is
`opt/packages/scene`; `opt/examples/fh/game` draws a map and collides with it
in about forty lines.

## Layout

The panel metrics are fixed; the window is not. The screen size and the
viewport are recomputed each frame from the actual window, so the editor fits
whatever it is hosted in — a game's window is not going to be 1280x760 because
the editor would like it to be. microui applies a window's rect only the first
time it sees it, so panels do not move if the window is resized *while running*;
size the window before the first frame.

## Files it writes

Beside the game, next to `main.fh`: every `*.json` is a level, and `editor.json`
is the editor's own settings — grid step, snapping, what the viewport draws, and
which level to reopen. None of that belongs in a level file, which is why it is
not in one.
