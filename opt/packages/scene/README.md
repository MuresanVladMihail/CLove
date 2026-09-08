# scene — read a scene the CLove 2D editor saved

`Scene()` is the read side of the document `opt/examples/fh/editor` writes.
Game code asks it for entities by id, by name or by group instead of reaching
into the JSON, so a change to the document layout stays inside this package.

```fh
include "../../packages/scene/scene.fh"

fn love_load() {
    let scene = Scene("levels/forest.json");

    let player  = scene.find("Player");
    let pickups = scene.in_group("pickups");

    love_physics_setMeter(scene.meter());
    let world = love_physics_newWorld(scene.gravity()[0], scene.gravity()[1]);
}
```

`include` is textual, so the path is relative to the file doing the including,
and each file should be included exactly once.

## Constructor

`Scene(source)` — `source` is a path to a `.json` file the editor saved, or a
document map you already parsed. Documents up to version 2 are understood. It raises on a missing file, on something
that is not a scene document, and on a document written by a newer editor: a
level that will not load is a bug, not a state every caller should poll for.

## Looking entities up

| Call | Returns |
| --- | --- |
| `all()` / `count()` | Every entity, in document order / how many there are. |
| `get(id)` | The entity with that stable id, or `null`. |
| `find(name)` / `find_all(name)` | First match, or every match. Names are not unique; ids are. |
| `in_group(group)` | Everything tagged with that group, in document order. |
| `group_names()` | Every group used anywhere in the scene, without duplicates. |
| `filter(predicate)` | Everything the predicate accepts. |

## Reading one entity

`id`, `name`, `is_visible`, `position`, `angle`, `center`, `size`, `rect`,
`color`, `fixture`, `filter_data`, `body_type`, `sprite_path`, `groups`,
`has_group`, `props`, `prop`, `has_prop`, `links`, `link`.

`filter_data(e)` is `[category, mask, group]` — what the thing is, what it is willing
to touch, and the group override — ready for `love_fixture_setFilterData()`. A
level written before the filter existed reads as Box2D's own default.

`sprite_path()` returns a path or `null`, and `sprite_quad()` the rectangle of
that image the entity actually draws — `[x, y, w, h]` when its artwork is one
tile of a spritesheet, `null` when it is the whole file. The editor stores
nothing else about a sprite. `love_graphics_newImage()` loads `.svg` and `.png`
through the same call, and `love_image_isVector()` tells them apart afterwards;
a crop becomes drawable with `love_graphics_newQuad(x, y, w, h, image width,
image height)`, passed to `love_graphics_draw()` before the position.

`position()` is the document's top-left corner; Box2D wants `center()`.

## Properties

`props(e)` is the free-form map the level author filled in for that entity —
whatever the editor has no field for. `prop(e, key, fallback)` reads one with
a default, which is what a level authored before the property existed needs.

Values keep the type they were authored with: `true`/`false` come back as
bools, anything that parses as a number as a number, everything else as text.
So `scene.prop(e, "speed", 60) * dt` works with nothing to convert.

```
for (let e in scene.in_group("enemies")) {
    spawn_enemy(scene.center(e),
                scene.prop(e, "speed", 60),
                scene.prop(e, "patrol_radius", 120));
}
```

Groups say *what* a thing is, properties say *with what parameters*. Between
them the behaviour stays in game code and the level stays data — which is why
there is no scripting attached to an entity.

## Links

`link(e, name)` follows a named reference to another entity and hands back the
*entity*, not the id — an id the caller has to look up itself is a chore with a
bug in it. `links(e)` is the raw map.

```
let door = scene.find("Door");
let exit = scene.link(door, "exit");        # the entity it leads to
let level = scene.prop(door, "level", "");  # or a level name, as a property
```

A property's value is data; a link's value is another entity. They are kept
apart because the editor drops a link whose target is deleted, so a dangling
one should not appear in a file it wrote — `link()` still answers `null` for a
name that is not set.

## Joints

`joints()` is every joint in the level; `joints_of(e)` those touching one
entity. A joint holds two entities together and belongs to neither, so it lives
at the top level and names its ends by id:

```
{ "id": 5, "type": "distance", "a": 2, "b": 3,
  "anchor": [300, 200], "collide": false }
```

`"distance"` is a rigid link, `"revolute"` a hinge. Build them *after* the
bodies — both ends have to exist first. A level written before joints existed
reads as having none.

## The tile map

A level's ground is often painted rather than placed: the document carries the
spritesheets it was painted from and the cells painted out of them.

| Call | Returns |
| --- | --- |
| `tilemap()` | The raw map: cell size and layers. |
| `tile_size()` | `[width, height]` of one cell, in world units. |
| `tilesets()` / `tileset(id)` | The sheets, and one by id. |
| `tile_source(ts, tx, ty)` | Where that tile sits in its sheet, `[x, y, w, h]`. |
| `tile_layers()` / `tile_layer(name)` | The layers, back first, and one by name. |
| `tile_cells(layer)` | Every painted cell as `[column, row, sheet id, tile column, tile row]`. |
| `tile_count(layer)` | How many cells it holds. |
| `tile_at(layer, cx, cy)` | What is painted in one cell, or `null`. |
| `tile_cell_at(x, y)` / `tile_rect(cx, cy)` | World point → cell, and cell → the rectangle it covers. |
| `tile_bounds()` | `[min_x, min_y, max_x, max_y]` over every cell, or `null`. |
| `tile_is_solid(ts, tx, ty)` | Does that tile of that sheet collide wherever it is painted? |
| `tile_cell_is_solid(layer, cell)` | Does this cell collide — because of its layer or its tile? |
| `solid_tile_rects()` | All of that collision, merged. |

A cell names a **column and a row in the sheet**, not an index into it: an
index has to be read back through the sheet's column count, which is a property
of the image file rather than of the level — crop the sheet or swap it for one
a tile wider and every index means a different picture.

```
let size = scene.tile_size();
let cells = scene.tile_cells(scene.tile_layers()[0]);
for (let i = 0; i < len(cells); i++) {
    let c = cells[i];
    let ts = scene.tileset(c[2]);
    let src = scene.tile_source(ts, c[3], c[4]);
    # one quad per tile, built once -- and a sprite batch for a big map
    love_graphics_draw(sheet_image, quad_for(src),
        c[0] * size[0], c[1] * size[1], 0,
        size[0] / ts.tile_w, size[1] / ts.tile_h);
}
```

A cell collides when its **layer** is marked solid (a ground layer, whatever is
painted in it) or when the **tile** it holds is (`tile_is_solid()` — stone is
stone in every level it lands in). Both are authored in the editor, and
`tile_cell_is_solid()` is the two questions asked as one.

`solid_tile_rects()` hands back `[x, y, w, h]` in world units with each row of
touching cells already merged into one rectangle, ready for
`love_physics_newRectangleShape()` on a static body. That is one shape per run
rather than one per tile — Box2D would otherwise spend its time on contacts
between neighbours that can never move, and a moving box catches on the seam
between two of them. `opt/examples/fh/game` builds exactly this.

A level written before tile maps existed (document version 1) reads as a map
with no sheets and no cells rather than as an error.

Tiles are static geometry: the merged rectangles have nowhere to keep a body of
their own. Anything that has to move is an entity — the editor's
**Brush -> new entity** makes one wearing a tile, and `sprite_quad()` is how it
comes back.

## Spatial queries

| Call | Returns |
| --- | --- |
| `at_point(x, y)` | Everything covering that point, topmost first. |
| `in_rect(x, y, w, h)` | Everything overlapping that rectangle. |
| `nearest(x, y)` | The entity whose centre is closest. |
| `bounds()` | `[min_x, min_y, max_x, max_y]` over the scene, or `null` when empty. |

These test the fixture box as the editor lays it out, axis-aligned and
ignoring rotation. They are for picking a spawn point or finding what is
nearby; use the physics world for anything exact.

## The document

`raw()`, `version()`, `meter()`, `gravity()`.

`meter()` is the pixels-per-metre the level was authored at. Pass it to
`love_physics_setMeter()` before building a world, or every distance is off by
that ratio.

## Scope

Reading only. The package makes no engine calls, so it runs headless and is
cheap to test — see `tests/fh/test_scene_package.fh`. Turning a scene into
live images and bodies stays in the caller, and there is deliberately no way
to attach behaviour to an entity: groups, names and properties are the whole
vocabulary.

When you do build a Box2D world from a scene, put the entity's `id` in the
body's and the fixture's user data. A collision callback is handed fixtures
and nothing else, so that id is the only way back from a contact to the thing
in the level that caused it — `opt/examples/fh/editor/world.fh` does the same
for the editor's Play mode.
