#!/bin/bash
# Builds ONE macOS installer package with two choices (both ticked by default):
#   Seedling.vst3       -> /Library/Audio/Plug-Ins/VST3
#   Seedling.component  -> /Library/Audio/Plug-Ins/Components   (Audio Unit, for Logic / GarageBand / MainStage)
#
# Usage: installer/macos/build_pkg.sh <version> <Seedling.vst3> <Seedling.component | -> [output dir]
# Pass "-" instead of the component to build a VST3-only package.
set -euo pipefail

VER="${1:?version}"; VST3="${2:?path to Seedling.vst3}"; AU="${3:?path to Seedling.component, or -}"; OUT="${4:-.}"
WORK="$(mktemp -d)"
PLISTBUDDY=/usr/libexec/PlistBuddy

# component_pkg <id> <bundle> <install-location> <extra pkgbuild args...>
component_pkg() {
    local id="$1" bundle="$2" loc="$3"; shift 3
    local name; name="$(basename "$bundle")"
    local root="$WORK/root-$id"
    mkdir -p "$root"
    cp -R "$bundle" "$root/$name"
    codesign --force --deep --sign - "$root/$name"                 # ad-hoc signature (not notarised)

    # Never let Installer "update" some other copy of the bundle it finds elsewhere on the disk (a dev build, say):
    # always install to the standard folder.
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

HAVE_AU=0
if [ "$AU" != "-" ]; then
    HAVE_AU=1
    # macOS caches the list of Audio Units: restart the registrar so the new one shows up without a reboot.
    mkdir -p "$WORK/scripts-au"
    cat > "$WORK/scripts-au/postinstall" <<'SH'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
SH
    chmod +x "$WORK/scripts-au/postinstall"
    component_pkg au "$AU" /Library/Audio/Plug-Ins/Components --scripts "$WORK/scripts-au"
fi

# ---- distribution: a short installer with a tick-box per format ---------------------------------------------------
{
    echo '<?xml version="1.0" encoding="utf-8"?>'
    echo '<installer-gui-script minSpecVersion="2">'
    echo "  <title>Seedling $VER</title>"
    echo '  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>'
    echo '  <domains enable_localSystem="true"/>'
    echo '  <choices-outline>'
    echo '    <line choice="vst3"/>'
    [ "$HAVE_AU" = 1 ] && echo '    <line choice="au"/>'
    echo '  </choices-outline>'
    echo '  <choice id="vst3" title="VST3" description="For Ableton Live, Reaper, Bitwig, Cubase and other VST3 hosts. Installs to /Library/Audio/Plug-Ins/VST3." start_selected="true">'
    echo '    <pkg-ref id="com.seedlingaudio.seedling.vst3"/>'
    echo '  </choice>'
    if [ "$HAVE_AU" = 1 ]; then
        echo '  <choice id="au" title="Audio Unit" description="For Logic Pro, GarageBand, MainStage and other Audio Unit hosts. Installs to /Library/Audio/Plug-Ins/Components." start_selected="true">'
        echo '    <pkg-ref id="com.seedlingaudio.seedling.au"/>'
        echo '  </choice>'
    fi
    echo "  <pkg-ref id=\"com.seedlingaudio.seedling.vst3\" version=\"$VER\">Seedling-vst3.pkg</pkg-ref>"
    [ "$HAVE_AU" = 1 ] && echo "  <pkg-ref id=\"com.seedlingaudio.seedling.au\" version=\"$VER\">Seedling-au.pkg</pkg-ref>"
    echo '</installer-gui-script>'
} > "$WORK/dist.xml"

mkdir -p "$OUT"
productbuild --distribution "$WORK/dist.xml" --package-path "$WORK" "$OUT/Seedling-$VER-macOS.pkg"
echo "wrote $OUT/Seedling-$VER-macOS.pkg ($([ "$HAVE_AU" = 1 ] && echo 'VST3 + Audio Unit' || echo 'VST3 only'))"
