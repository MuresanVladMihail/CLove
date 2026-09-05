# CLove for Sublime Text

Highlighting, completions and a build system for CLove games written in FH.

CLove games *are* FH scripts, so this package does not redefine the language: it
inherits the FH grammar (`extends: Packages/FH/FH.sublime-syntax`) and adds the
engine's `love_*` API on top. That means **the FH package is required** -- it
lives in the FH repository at `tools/Sublime/FH` and must be installed as
`Packages/FH`. Sublime Text 4 build 4075 or newer is needed for `extends`.

## Installing

Install the FH package first, then this one, into your Sublime `Packages`
directory (`Preferences > Browse Packages…`):

```sh
# Linux
cp -r ../../../../FH/tools/Sublime/FH ~/.config/sublime-text/Packages/FH
cp -r .                               ~/.config/sublime-text/Packages/CLove

# macOS
P="$HOME/Library/Application Support/Sublime Text/Packages"
cp -r ../../../../FH/tools/Sublime/FH "$P/FH"
cp -r .                               "$P/CLove"
```

Both packages claim the `.fh` extension, so tell Sublime which one you want for
a given project: open a game script and pick
`View > Syntax > Open all with current extension as… > CLove`. Plain FH scripts
that are not CLove games can stay on **FH**.

## What you get

- **`CLove.sublime-syntax`** -- everything the FH syntax highlights, plus all
  272 functions registered by `src/fhapi/*.c` scoped `support.function.clove`,
  and the 14 lifecycle callbacks scoped `support.function.callback.clove`.
  A name that is not part of the API -- `love_udpate`, say -- is left as an
  ordinary call, which makes the classic "my callback never runs" typo visible.
  A callback *definition* (`fn love_draw(self)`) keeps FH's
  `entity.name.function` scope, so it still appears in the symbol list.
- **`CLove.sublime-completions`** -- all 272 API functions, annotated with their
  module and source file; the 14 callbacks as ready-made skeletons with the
  right parameters; every `config.fh` key; and `clovegame`, which expands to a
  complete `main.fh`.
- **`CLove.sublime-build`** -- `Tools > Build` runs `clove` from the current
  file's directory, which is how the engine finds `main.fh`. Errors are
  clickable. Variants: run from the project root, run a packaged `.love`
  archive, and run the engine's own `tests/run_tests.sh`.
  If `clove` is not on your `PATH`, edit `shell_cmd`.
- **`CLove.sublime-settings`** -- 4-space soft tabs, no completions inside
  strings or comments.

Comment toggling, indentation rules and the symbol list come from the FH
package: their selectors target `source.fh`, and CLove's `source.fh.clove`
matches it.

## A note on the input callbacks

The engine hands the input callbacks a **single array**, not one argument per
value (see `fh_keyboard_keypressed()` and friends, which pack everything into
one `fh_new_array()` before calling). The completions use the real shapes:

```php
fn love_keypressed(key)      { }   # key = [name, keycode, isrepeat]
fn love_keyreleased(key)     { }   # key = [name, keycode]
fn love_mousepressed(m)      { }   # m   = [x, y, button]
fn love_mousereleased(m)     { }   # m   = [x, y, button]
fn love_joystickpressed(j)   { }   # j   = [id, button]
fn love_joystickreleased(j)  { }   # j   = [id, button]
fn love_textinput(text)      { }   # a string
fn love_wheelmoved(y)        { }   # a number
fn love_focus(focused)       { }   # a bool
```

`love_joystick_*` query functions do not exist yet -- `joystick.c` registers an
empty `c_funcs[]` table -- so only the two joystick callbacks are offered.

## Keeping the API list in sync

Both the syntax's name lists and the completions are generated from the
`c_funcs[]` tables in `src/fhapi/*.c`. After adding, renaming or removing a
binding, run:

```sh
python3 tools/Sublime/CLove/update_api.py
```

It rewrites the block between the `GENERATED` markers in
`CLove.sublime-syntax` and regenerates `CLove.sublime-completions`. The
callback signatures and `config.fh` keys are the hand-maintained part, at the
top of that script.

## Running the syntax tests

`syntax_test_clove.fh` holds 325 scope assertions in Sublime's syntax-test
format. With both packages installed, open it and choose
`Tools > Build With… > Syntax Tests`.
