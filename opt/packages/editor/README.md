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
