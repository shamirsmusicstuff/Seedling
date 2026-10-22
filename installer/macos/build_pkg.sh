#!/bin/bash
# Builds a macOS installer package that puts Seedling.vst3 in /Library/Audio/Plug-Ins/VST3.
# Usage: installer/macos/build_pkg.sh <version> <path/to/Seedling.vst3> [output dir]
set -euo pipefail
VER="${1:?version}"; SRC="${2:?path to Seedling.vst3}"; OUT="${3:-.}"
WORK="$(mktemp -d)"
mkdir -p "$WORK/root"
cp -R "$SRC" "$WORK/root/Seedling.vst3"
codesign --force --deep --sign - "$WORK/root/Seedling.vst3"      # ad-hoc signature (not notarised)
pkgbuild --root "$WORK/root" --identifier com.seedlingaudio.seedling.vst3 --version "$VER" \
         --install-location /Library/Audio/Plug-Ins/VST3 "$WORK/Seedling-component.pkg"
cat > "$WORK/dist.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Seedling $VER</title>
  <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline><line choice="default"/></choices-outline>
  <choice id="default" title="Seedling (VST3)"><pkg-ref id="com.seedlingaudio.seedling.vst3"/></choice>
  <pkg-ref id="com.seedlingaudio.seedling.vst3" version="$VER">Seedling-component.pkg</pkg-ref>
</installer-gui-script>
XML
mkdir -p "$OUT"
productbuild --distribution "$WORK/dist.xml" --package-path "$WORK" "$OUT/Seedling-$VER-macOS.pkg"
echo "wrote $OUT/Seedling-$VER-macOS.pkg"
