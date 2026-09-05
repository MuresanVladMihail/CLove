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
document map you already parsed. It raises on a missing file, on something
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
`color`, `fixture`, `body_type`, `sprite_path`, `groups`, `has_group`,
`props`, `prop`, `has_prop`.

`sprite_path()` returns a path or `null` — the editor stores nothing else
about a sprite. `love_graphics_newImage()` loads `.svg` and `.png` through the
same call, and `love_image_isVector()` tells them apart afterwards.

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
