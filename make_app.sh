#!/bin/sh
# Build Hercules.app from your own copy of the game files.
# usage: ./make_app.sh /path/to/game_files   (folder holding HERCULES.FS, ENG1.DLL, ENG3.DLL, TITLE.DLL [, DATA/*.ETV])
set -e
SRC="${1:?usage: $0 /path/to/game_files}"
for f in HERCULES.FS ENG1.DLL ENG3.DLL TITLE.DLL; do [ -f "$SRC/$f" ] || { echo "missing $SRC/$f"; exit 1; }; done
make
APP=Hercules.app
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp build/hercules "$APP/Contents/MacOS/hercules"
cp "$SRC"/HERCULES.FS "$SRC"/ENG1.DLL "$SRC"/ENG3.DLL "$SRC"/TITLE.DLL "$APP/Contents/Resources/"
[ -d "$SRC/DATA" ] && cp -R "$SRC/DATA" "$APP/Contents/Resources/DATA"
cat > "$APP/Contents/Info.plist" <<'P'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleDisplayName</key><string>Hercules (native)</string>
<key>CFBundleExecutable</key><string>hercules</string>
<key>CFBundleIdentifier</key><string>local.hercules.native</string>
<key>CFBundleName</key><string>Hercules</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>LSMinimumSystemVersion</key><string>12.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
P
# the executable loads libSDL2 dynamically; bundle it if you want a standalone app
codesign --force --sign - "$APP" 2>/dev/null || true
echo "built $APP"
