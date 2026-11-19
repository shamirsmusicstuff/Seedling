#!/bin/bash
# One macOS installer package for the Rust build, a tick-box per format (all ticked by default):
#   Seedling.vst3       -> /Library/Audio/Plug-Ins/VST3
#   Seedling.clap       -> /Library/Audio/Plug-Ins/CLAP
#   Seedling.component  -> /Library/Audio/Plug-Ins/Components   (Audio Unit, for Logic / GarageBand / MainStage)
#
# Usage: installer/macos/build_pkg_rust.sh <version> <Seedling.vst3> <Seedling.clap> <Seedling.component | -> [output dir]
set -euo pipefail

VER="${1:?version}"; VST3="${2:?path to Seedling.vst3}"; CLAP="${3:?path to Seedling.clap}"; AU="${4:?path to Seedling.component, or -}"; OUT="${5:-.}"
WORK="$(mktemp -d)"
PLISTBUDDY=/usr/libexec/PlistBuddy

component_pkg() {
    local id="$1" bundle="$2" loc="$3"; shift 3
    local name; name="$(basename "$bundle")"
    local root="$WORK/root-$id"
    mkdir -p "$root"
    cp -R "$bundle" "$root/$name"
    codesign --force --deep --sign - "$root/$name"                 # ad-hoc signature (not notarised)

    local plist="$WORK/$id.plist"
    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    local n; n="$("$PLISTBUDDY" -c Print "$plist" | grep -c BundleIsRelocatable || true)"
    for ((i = 0; i < n; i++)); do
        "$PLISTBUDDY" -c "Set :$i:BundleIsRelocatable false" "$plist"
        "$PLISTBUDDY" -c "Set :$i:BundleOverwriteAction upgrade" "$plist" 2>/dev/null || true
    done

    pkgbuild --root "$root" --component-plist "$plist" --identifier "com.seedlingaudio.seedling.$id" \
             --version "$VER" --install-location "$loc" "$@" "$WORK/Seedling-$id.pkg"
}

component_pkg vst3 "$VST3" /Library/Audio/Plug-Ins/VST3
component_pkg clap "$CLAP" /Library/Audio/Plug-Ins/CLAP

HAVE_AU=0
if [ "$AU" != "-" ]; then
    HAVE_AU=1
    mkdir -p "$WORK/scripts-au"
    cat > "$WORK/scripts-au/postinstall" <<'SH'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
SH
    chmod +x "$WORK/scripts-au/postinstall"
    component_pkg au "$AU" /Library/Audio/Plug-Ins/Components --scripts "$WORK/scripts-au"
fi

{
    echo '<?xml version="1.0" encoding="utf-8"?>'
    echo '<installer-gui-script minSpecVersion="2">'
    echo "  <title>Seedling $VER</title>"
    echo '  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>'
    echo '  <domains enable_localSystem="true"/>'
    echo '  <choices-outline>'
    echo '    <line choice="vst3"/>'
    echo '    <line choice="clap"/>'
    [ "$HAVE_AU" = 1 ] && echo '    <line choice="au"/>'
    echo '  </choices-outline>'
    echo '  <choice id="vst3" title="VST3" description="For Ableton Live, Reaper, Bitwig, Cubase and other VST3 hosts. Installs to /Library/Audio/Plug-Ins/VST3." start_selected="true">'
    echo '    <pkg-ref id="com.seedlingaudio.seedling.vst3"/>'
    echo '  </choice>'
    echo '  <choice id="clap" title="CLAP" description="For Bitwig, Reaper and other CLAP hosts. Installs to /Library/Audio/Plug-Ins/CLAP." start_selected="true">'
    echo '    <pkg-ref id="com.seedlingaudio.seedling.clap"/>'
    echo '  </choice>'
    if [ "$HAVE_AU" = 1 ]; then
        echo '  <choice id="au" title="Audio Unit" description="For Logic Pro, GarageBand, MainStage and other Audio Unit hosts. Installs to /Library/Audio/Plug-Ins/Components." start_selected="true">'
        echo '    <pkg-ref id="com.seedlingaudio.seedling.au"/>'
        echo '  </choice>'
    fi
    echo "  <pkg-ref id=\"com.seedlingaudio.seedling.vst3\" version=\"$VER\">Seedling-vst3.pkg</pkg-ref>"
    echo "  <pkg-ref id=\"com.seedlingaudio.seedling.clap\" version=\"$VER\">Seedling-clap.pkg</pkg-ref>"
    [ "$HAVE_AU" = 1 ] && echo "  <pkg-ref id=\"com.seedlingaudio.seedling.au\" version=\"$VER\">Seedling-au.pkg</pkg-ref>"
    echo '</installer-gui-script>'
} > "$WORK/dist.xml"

mkdir -p "$OUT"
productbuild --distribution "$WORK/dist.xml" --package-path "$WORK" "$OUT/Seedling-Rust-$VER-macOS.pkg"
echo "wrote $OUT/Seedling-Rust-$VER-macOS.pkg ($([ "$HAVE_AU" = 1 ] && echo 'VST3 + CLAP + Audio Unit' || echo 'VST3 + CLAP'))"
