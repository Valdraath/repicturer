#!/bin/sh
# adds "RePicturer" to the application menu / dock of the current user (no root needed)
DIR=$(cd "$(dirname "$0")" && pwd)
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$APPS"
chmod +x "$DIR/RePicturer" "$DIR/repicturer-cli" 2>/dev/null
cat > "$APPS/repicturer.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=RePicturer
Comment=Image upscaler: waifu2x, Real-CUGAN and Real-ESRGAN on any Vulkan GPU
Exec="$DIR/RePicturer" %F
Path=$DIR
Icon=$DIR/icon.png
Terminal=false
Categories=Graphics;RasterGraphics;
MimeType=image/png;image/jpeg;image/webp;image/bmp;image/tiff;image/x-tga;
StartupWMClass=RePicturer
EOF
chmod +x "$APPS/repicturer.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1
echo "installed: $APPS/repicturer.desktop"
