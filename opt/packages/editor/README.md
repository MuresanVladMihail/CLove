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
button opens that sheet as a palette over the viewport. Click a tile there, or
*drag a block* of them, and it becomes the brush; pressing Tiles again puts the
palette away. It narrows with the viewport and gives up when there is no room
left for it.

Three ways to lay tiles down, chosen in the Tilemap section:

| Mode | What a click does |
| --- | --- |
| **Brush** | Stamps the brush where you click, and drags a stroke — the cells between two mouse positions are filled in, so a fast drag leaves no gaps. |
| **Rect** | Drag out a rectangle; the cells land when the button comes up, with a block brush tiled across it. |
| **Fill** | Replaces the run of identical tiles you clicked on. It never spreads into empty space — a map is unbounded, and filling the sky would be filling forever; use Rect for that. |

**erase** turns whichever of them into its opposite, and holding `Shift` does
the same for one click: a key is the quick way for a cell and the wrong way for
a hundred.

Layers are drawn back to front, the way the hierarchy's z-order works for
entities. A cell collides when **either** its layer or its tile says so:

* **`solid` on a layer** — everything painted in it collides. A ground layer.
* **`solid` on a tile** — that tile collides wherever it is painted, in any
  layer. `Shift`+click it on the sheet, or use the checkbox under the sheet's
  settings; a marked tile carries a blue corner in the palette. This is the
  stone block in a layer of grass that is not solid, and it is a toggle —
  clicking again takes it off.

A solid tile also says *what* it collides as: **friction**, **bounce**,
**sensor**, the collision **filter** (the same layer/hits bit fields an
entity's fixture has), and a **name** — what a contact callback is handed when
something hits it, since a merged run has no entity behind it. An entity's user
data is its id, a number; a tile's is that string, so a game can tell ice from
lava. Cells only merge into one shape when they collide *alike*, so a patch of
ice keeps its own friction instead of taking the floor's.

Play then merges every colliding cell into static bodies, a row of touching
cells at a time. Switch **Show fixtures** on and the merged rectangles are
outlined in the viewport, so what will collide is visible before you press
Play.

**Brush -> selected sprite** gives whatever is selected the brush's tile as its
artwork: the same sheet, cropped to that one tile. A crate and the ground it
sits on can come out of the same file.

**Brush -> new entity** goes further: a dynamic body one cell big, in the
middle of the view, wearing that tile. A cell can never be dynamic — the solid
ones become static shapes when Play starts, and a merged shape has nowhere to
keep a body — so anything meant to be pushed around is an entity that happens
to wear the same tile.

The document grows two fields for all this:

```json
"tilesets": [ { "id": 1, "name": "terrain.png", "path": "art/terrain.png",
                "tile_w": 32, "tile_h": 32, "margin": 0, "spacing": 0,
                "tiles": { "0,3": { "solid": true, "friction": 0.05,
                                    "restitution": 0, "sensor": false,
                                    "category": 1, "mask": 65535, "group": 0,
                                    "name": "ice" } } } ],
"tilemap":  { "tile_w": 32, "tile_h": 32,
              "layers": [ { "id": 1, "name": "Ground", "visible": true,
                            "solid": true, "cells": { "3,-1": [1, 4, 2] } } ] }
```

A cell is `[tileset id, column, row]` — where the tile *is in the sheet*, not
an index into it, so cropping the sheet or swapping it for one a tile wider
does not repaint the level behind your back. What a tile *is* lives on the
sheet rather than in every cell that uses it: stone is stone in every level,
and only the tiles somebody has said something about are in the file at all.

Layers can be renamed, reordered (`Down` / `Up`, back to front), hidden and
cleared. The map is drawn through one sprite batch per (layer, sheet), rebuilt
when the document settles — while a stroke is being painted the visible cells
are drawn one at a time instead, because rebuilding a ten-thousand-cell batch
per painted cell would cost more than it saves.

Reading it back from a game is `opt/packages/scene`; `opt/examples/fh/game`
draws a map and collides with it in about sixty lines.

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
