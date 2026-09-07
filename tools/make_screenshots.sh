#!/bin/sh
# Takes the pictures README.md shows, one per example, by running each of them
# for a moment and asking the engine for a frame (CLOVE_SCREENSHOT, see
# screenshot_tick() in src/fh_mainactivity.c). Run it after anything that
# changes how the examples look, so the README does not drift from the engine.
#
#   tools/make_screenshots.sh [path-to-clove-binary]
#
# The results land in opt/data/.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
out="$root/opt/data"

clove=${1:-}
if [ -z "$clove" ]; then
    for c in "$root/build/clove" "$root/clove"; do
        [ -x "$c" ] && clove="$c" && break
    done
fi
if [ -z "$clove" ] || [ ! -x "$clove" ]; then
    echo "error: clove binary not found; build it first or pass its path" >&2
    exit 2
fi

mkdir -p "$out"

# example : frame to grab. A few need longer than the default before there is
# anything to see -- particles have to fill the screen, tweens have to move.
shoot() {
    name=$1
    frame=${2:-60}
    dir="$root/opt/examples/fh/$name"
    if [ ! -f "$dir/main.fh" ]; then
        echo "  skip $name (no main.fh)"
        return
    fi
    printf '  %-12s frame %-4s' "$name" "$frame"
    ( cd "$dir" && \
      CLOVE_SCREENSHOT="$out/example_$name.png" \
      CLOVE_SCREENSHOT_FRAME="$frame" \
      "$clove" >/dev/null 2>&1 ) || true
    if [ -f "$out/example_$name.png" ]; then
        echo " ok"
    else
        echo " FAILED"
    fi
}

echo "Taking example screenshots into opt/data/"
shoot particles  180
shoot physics    95
shoot shaders    90
shoot vector_art 60
shoot ui         60
shoot tweens     120
shoot mesh       60
shoot noise      90
shoot editor     60

# The error screen has no example of its own: it needs a game that fails.
echo "  error screen"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/clove_shot.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/config.fh" <<'EOF'
fn love_config(c) {
    c.window_title = "CLove";
    c.window_width = 900;
    c.window_height = 560;
}
EOF
cat > "$tmp/main.fh" <<'EOF'
fn load_level(name) { return parse_level(name); }
fn parse_level(name) { error("could not parse '" + name + "': unexpected token '}' on line 42"); }
fn love_load() { let self = {}; self.level = load_level("levels/forest.json"); return self; }
fn love_draw(self) {}
fn main() {}
EOF
( cd "$tmp" && CLOVE_ERROR_SCREENSHOT="$out/error_screen.png" "$clove" >/dev/null 2>&1 ) || true
[ -f "$out/error_screen.png" ] && echo "  error screen ok" || echo "  error screen FAILED"

echo "done"
