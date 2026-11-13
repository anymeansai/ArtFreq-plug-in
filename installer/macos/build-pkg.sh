#!/bin/bash
# Builds the macOS installer package for FreqArt (by AnyMeans).
#
#   installer/macos/build-pkg.sh <build-dir> <output-dir> [version]
#
# Produces <output-dir>/FreqArt-<version>-macOS.pkg: a standard Apple installer with a welcome
# page, the licence, and a conclusion page with the first steps. It installs both plug-ins into
# /Library/Audio/Plug-Ins/VST3 (FreqArt.vst3, the instrument, and "FreqArt FX.vst3", the effect;
# each is a choice of its own, both on by default), then offers to move the downloaded installer
# to the Trash. The version defaults to the one in CMakeLists.txt (project(FreqArt VERSION x.y.z)).
#
# <build-dir> is a CMake build folder with a Release build of the FreqArt and FreqArtFX targets,
# i.e. one that contains FreqArt_artefacts/Release/VST3/FreqArt.vst3 and
# FreqArtFX_artefacts/Release/VST3/FreqArt FX.vst3. macOS only (pkgbuild, productbuild). The
# package is not signed with a Developer ID or notarised.
set -euo pipefail

USAGE="usage: build-pkg.sh <build-dir> <output-dir> [version]"
BUILD_DIR="${1:?$USAGE}"
OUT_DIR="${2:?$USAGE}"
VERSION="${3:-}"

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

die()
{
    echo "build-pkg.sh: $*" >&2
    exit 1
}

for tool in pkgbuild productbuild ditto codesign xattr lipo /usr/libexec/PlistBuddy; do
    command -v "$tool" > /dev/null 2>&1 || die "$tool not found (this script runs on macOS only)"
done

if [ -z "$VERSION" ]; then
    VERSION=$(sed -nE 's/^project\(FreqArt VERSION ([0-9.]+).*/\1/p' "$REPO/CMakeLists.txt")
fi
# Digits and dots only: the version also goes into file names and through sed below.
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+)*$ ]] || die "could not determine a version (got '$VERSION')"

# The two plug-ins: bundle name (it has a space in the effect's), CMake target, package id.
PLUGINS=(
    "FreqArt|FreqArt|com.anymeans.freqart.vst3"
    "FreqArt FX|FreqArtFX|com.anymeans.freqartfx.vst3"
)

for entry in "${PLUGINS[@]}"; do
    IFS='|' read -r NAME TARGET _ <<< "$entry"
    PLUGIN="$BUILD_DIR/${TARGET}_artefacts/Release/VST3/$NAME.vst3"
    [ -f "$PLUGIN/Contents/MacOS/$NAME" ] || die "missing plug-in: $PLUGIN (build the $TARGET target in Release first)"
    [ -f "$PLUGIN/Contents/Info.plist" ] || die "not a complete bundle (no Info.plist): $PLUGIN"
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs" "$WORK/scripts" "$WORK/resources" "$OUT_DIR"

plist_set_false()
{
    /usr/libexec/PlistBuddy -c "Set $2 false" "$1" 2> /dev/null \
        || /usr/libexec/PlistBuddy -c "Add $2 bool false" "$1"
}

# One component package per plug-in, so the installer can offer each as a choice.
for entry in "${PLUGINS[@]}"; do
    IFS='|' read -r NAME TARGET IDENTIFIER <<< "$entry"
    PLUGIN="$BUILD_DIR/${TARGET}_artefacts/Release/VST3/$NAME.vst3"

    ROOT="$WORK/root-$TARGET"   # becomes /Library/Audio/Plug-Ins/VST3
    mkdir -p "$ROOT"
    # The payload's top folder stands for the install location, and its permissions can end up on
    # /Library/Audio/Plug-Ins/VST3: make sure it is a normal 755 folder whatever the umask.
    chmod 755 "$ROOT"

    # ---- the plug-in --------------------------------------------------------------------------
    STAGED="$ROOT/$NAME.vst3"
    ditto "$PLUGIN" "$STAGED"
    # Extended attributes (quarantine, provenance) do not belong in an installer payload.
    xattr -cr "$STAGED"

    # JUCE signs the bundle before it writes moduleinfo.json into it, which breaks the signature's
    # seal. CI re-signs the build before calling this script; for a build straight out of CMake,
    # sign the installer's copy (ad hoc) here so what gets installed always verifies.
    if ! codesign --verify --strict "$STAGED" > /dev/null 2>&1; then
        echo "$NAME's signature does not verify (normal straight after a build): signing the installer's copy ad hoc."
        codesign --force --sign - "$STAGED"
    fi
    codesign --verify --strict "$STAGED"

    if ! lipo "$STAGED/Contents/MacOS/$NAME" -verify_arch arm64 x86_64 2> /dev/null; then
        echo "warning: $NAME is not a universal binary ($(lipo -archs "$STAGED/Contents/MacOS/$NAME")); configure with -DCMAKE_OSX_ARCHITECTURES=\"arm64;x86_64\" for one that runs on every Mac." >&2
    fi

    # ---- its component package ----------------------------------------------------------------
    # Always install into the system VST3 folder: mark the bundle non-relocatable, so the
    # installer never "updates" a stray copy found elsewhere on the disk instead, and not
    # version-checked, so it can install over any other version (older or newer).
    PLIST="$WORK/$TARGET.plist"
    pkgbuild --analyze --root "$ROOT" "$PLIST" > /dev/null

    entries=0
    while /usr/libexec/PlistBuddy -c "Print :$entries" "$PLIST" > /dev/null 2>&1; do
        plist_set_false "$PLIST" ":$entries:BundleIsRelocatable"
        plist_set_false "$PLIST" ":$entries:BundleIsVersionChecked"
        entries=$((entries + 1))
    done
    # Only bundles can be relocated, so if pkgbuild did not see one there is nothing to switch off.
    if [ "$entries" -eq 0 ]; then
        echo "warning: pkgbuild did not list $NAME.vst3 as a bundle; it will be installed as plain files." >&2
    fi

    pkgbuild --root "$ROOT" --component-plist "$PLIST" \
             --identifier "$IDENTIFIER" --version "$VERSION" \
             --install-location "/Library/Audio/Plug-Ins/VST3" "$WORK/pkgs/$TARGET.pkg"
done

# The last step: no files, only the postinstall script. Copied so its permissions do not depend
# on how the repository was checked out.
cp "$HERE/scripts/postinstall" "$WORK/scripts/postinstall"
chmod 755 "$WORK/scripts/postinstall"
pkgbuild --nopayload --scripts "$WORK/scripts" \
         --identifier "com.anymeans.freqart.finish" --version "$VERSION" "$WORK/pkgs/finish.pkg"

# ---- the product archive ------------------------------------------------------------------
cp "$HERE/resources/welcome.html" "$HERE/resources/conclusion.html" "$WORK/resources/"
cp "$REPO/LICENSE" "$WORK/resources/license.txt"
sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

PKG="$OUT_DIR/FreqArt-$VERSION-macOS.pkg"
rm -f "$PKG"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" \
             --resources "$WORK/resources" --version "$VERSION" "$PKG"
echo "built $PKG"
