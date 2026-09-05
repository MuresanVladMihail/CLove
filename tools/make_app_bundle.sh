#!/bin/sh
# Wrap a CLove game in a macOS .app bundle.
#
#   tools/make_app_bundle.sh <game-dir> [app-name] [output-dir]
#
# CLove loads main.fh from the *current working directory* (see
# fh_main_activity_load in src/fh_mainactivity.c), and an app launched from
# Finder starts with "/" as its cwd. The bundle therefore ships the game under
# Contents/Resources/game and its executable is a small launcher that chdirs
# there before exec'ing the engine.
#
# The result is an ordinary double-clickable app: it gets a Dock icon, a name
# of its own, and macOS treats it as an application rather than a loose binary
# (which is what lets screen-reading and automation tools address it).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)

game=${1:-}
name=${2:-CLove}
outdir=${3:-$root/build}

if [ -z "$game" ] || [ ! -f "$game/main.fh" ]; then
    echo "usage: tools/make_app_bundle.sh <game-dir> [app-name] [output-dir]" >&2
    echo "       <game-dir> must contain main.fh" >&2
    exit 2
fi
game=$(cd "$game" && pwd)

engine=""
for c in "$root/build/clove" "$root/clove"; do
    [ -x "$c" ] && engine="$c" && break
done
if [ -z "$engine" ]; then
    echo "error: no clove binary found. Build it first (./build_osx.sh)." >&2
    exit 2
fi

app="$outdir/$name.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

cp "$engine" "$app/Contents/MacOS/clove"
cp -R "$game" "$app/Contents/Resources/game"

# The bundle executable: chdir into the game, then become the engine. exec
# keeps the pid, so macOS still associates the process with this bundle.
cat > "$app/Contents/MacOS/$name" <<'LAUNCHER'
#!/bin/sh
# Resolve both paths before the chdir: after it, a relative $0 (which is what
# you get running the launcher from a shell rather than through Finder) no
# longer points anywhere.
bin=$(cd "$(dirname "$0")" && pwd)
cd "$bin/../Resources/game" || exit 1
exec "$bin/clove" "$@"
LAUNCHER
chmod +x "$app/Contents/MacOS/$name" "$app/Contents/MacOS/clove"

icon_entry=""
src_icon="$root/opt/icon.png"
if [ -f "$src_icon" ] && command -v iconutil >/dev/null 2>&1; then
    set +e
    iconset=$(mktemp -d)/icon.iconset
    mkdir -p "$iconset"
    ok=1
    for s in 16 32 64 128 256 512; do
        sips -z $s $s "$src_icon" --out "$iconset/icon_${s}x${s}.png" >/dev/null 2>&1 || ok=0
    done
    if [ "$ok" -eq 1 ] && iconutil -c icns "$iconset" -o "$app/Contents/Resources/$name.icns" >/dev/null 2>&1; then
        icon_entry="    <key>CFBundleIconFile</key>
    <string>$name</string>"
    fi
    rm -rf "$(dirname "$iconset")"
    set -e
fi

cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>$name</string>
    <key>CFBundleDisplayName</key>
    <string>$name</string>
    <key>CFBundleIdentifier</key>
    <string>org.clove.$(echo "$name" | tr '[:upper:] ' '[:lower:]-')</string>
    <key>CFBundleExecutable</key>
    <string>$name</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
$icon_entry
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>10.13</string>
</dict>
</plist>
PLIST

# Let Launch Services notice it straight away.
touch "$app"
if [ -x /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister ]; then
    /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$app" >/dev/null 2>&1 || true
fi

echo "built $app"
