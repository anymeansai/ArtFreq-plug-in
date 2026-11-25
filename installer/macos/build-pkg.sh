#!/bin/bash
# Builds the macOS installer package for ArtFreq (by AnyMeans).
#
#   installer/macos/build-pkg.sh <build-dir> <output-dir> [version]
#
# Produces <output-dir>/ArtFreq-<version>-macOS.pkg: a standard Apple installer with a welcome
# page, the licence, and a conclusion page with the first steps. It installs both plug-ins
# (ArtFreq, the instrument, and "ArtFreq FX", the effect; each is a choice of its own, both on by
# default), each in both formats: the VST3 into /Library/Audio/Plug-Ins/VST3 and the Audio Unit
# (.component, for Logic Pro and GarageBand) into /Library/Audio/Plug-Ins/Components. Then it
# offers to move the downloaded installer to the Trash. The version defaults to the one in
# CMakeLists.txt (project(ArtFreq VERSION x.y.z)).
#
# <build-dir> is a CMake build folder with a Release build of the ArtFreq and ArtFreqFX targets,
# i.e. one that contains ArtFreq_artefacts/Release/VST3/ArtFreq.vst3, .../AU/ArtFreq.component,
# ArtFreqFX_artefacts/Release/VST3/ArtFreq FX.vst3 and .../AU/ArtFreq FX.component. macOS only
# (pkgbuild, productbuild). The package is not signed with a Developer ID or notarised.
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
    VERSION=$(sed -nE 's/^project\(ArtFreq VERSION ([0-9.]+).*/\1/p' "$REPO/CMakeLists.txt")
fi
# Digits and dots only: the version also goes into file names and through sed below.
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+)*$ ]] || die "could not determine a version (got '$VERSION')"

# The two plug-ins: bundle name (it has a space in the effect's), CMake target, package id's stem.
PLUGINS=(
    "ArtFreq|ArtFreq|com.anymeans.artfreq"
    "ArtFreq FX|ArtFreqFX|com.anymeans.artfreqfx"
)

# The two formats of each: the build's folder, the bundle's extension, the end of the package's
# id and file name, and where it is installed.
FORMATS=(
    "VST3|vst3|vst3|/Library/Audio/Plug-Ins/VST3"
    "AU|component|au|/Library/Audio/Plug-Ins/Components"
)

for entry in "${PLUGINS[@]}"; do
    IFS='|' read -r NAME TARGET _ <<< "$entry"
    for format in "${FORMATS[@]}"; do
        IFS='|' read -r FOLDER EXTENSION _ _ <<< "$format"
        PLUGIN="$BUILD_DIR/${TARGET}_artefacts/Release/$FOLDER/$NAME.$EXTENSION"
        [ -f "$PLUGIN/Contents/MacOS/$NAME" ] || die "missing plug-in: $PLUGIN (build the $TARGET target in Release first)"
        [ -f "$PLUGIN/Contents/Info.plist" ] || die "not a complete bundle (no Info.plist): $PLUGIN"
    done
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs" "$WORK/scripts" "$WORK/resources" "$OUT_DIR"

plist_set_false()
{
    /usr/libexec/PlistBuddy -c "Set $2 false" "$1" 2> /dev/null \
        || /usr/libexec/PlistBuddy -c "Add $2 bool false" "$1"
}

# One component package per plug-in and format (the installer offers each plug-in as a choice,
# which installs both of its formats).
for entry in "${PLUGINS[@]}"; do
  for format in "${FORMATS[@]}"; do
    IFS='|' read -r NAME TARGET IDENTIFIER_STEM <<< "$entry"
    IFS='|' read -r FOLDER EXTENSION SUFFIX LOCATION <<< "$format"
    PLUGIN="$BUILD_DIR/${TARGET}_artefacts/Release/$FOLDER/$NAME.$EXTENSION"
    IDENTIFIER="$IDENTIFIER_STEM.$SUFFIX"
    PACKAGE="$TARGET-$SUFFIX"   # ArtFreq-vst3.pkg, ArtFreqFX-au.pkg, ... (distribution.xml)

    ROOT="$WORK/root-$PACKAGE"   # becomes $LOCATION
    mkdir -p "$ROOT"
    # The payload's top folder stands for the install location, and its permissions can end up on
    # that folder: make sure it is a normal 755 folder whatever the umask.
    chmod 755 "$ROOT"

    # ---- the plug-in --------------------------------------------------------------------------
    STAGED="$ROOT/$NAME.$EXTENSION"
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
    # Always install into the system plug-in folder: mark the bundle non-relocatable, so the
    # installer never "updates" a stray copy found elsewhere on the disk instead, and not
    # version-checked, so it can install over any other version (older or newer).
    PLIST="$WORK/$PACKAGE.plist"
    pkgbuild --analyze --root "$ROOT" "$PLIST" > /dev/null

    entries=0
    while /usr/libexec/PlistBuddy -c "Print :$entries" "$PLIST" > /dev/null 2>&1; do
        plist_set_false "$PLIST" ":$entries:BundleIsRelocatable"
        plist_set_false "$PLIST" ":$entries:BundleIsVersionChecked"
        entries=$((entries + 1))
    done
    # Only bundles can be relocated, so if pkgbuild did not see one there is nothing to switch off.
    if [ "$entries" -eq 0 ]; then
        echo "warning: pkgbuild did not list $NAME.$EXTENSION as a bundle; it will be installed as plain files." >&2
    fi

    pkgbuild --root "$ROOT" --component-plist "$PLIST" \
             --identifier "$IDENTIFIER" --version "$VERSION" \
             --install-location "$LOCATION" "$WORK/pkgs/$PACKAGE.pkg"
  done
done

# The last step: no files, only the postinstall script. Copied so its permissions do not depend
# on how the repository was checked out.
cp "$HERE/scripts/postinstall" "$WORK/scripts/postinstall"
chmod 755 "$WORK/scripts/postinstall"
pkgbuild --nopayload --scripts "$WORK/scripts" \
         --identifier "com.anymeans.artfreq.finish" --version "$VERSION" "$WORK/pkgs/finish.pkg"

# ---- the product archive ------------------------------------------------------------------
cp "$HERE/resources/welcome.html" "$HERE/resources/conclusion.html" "$WORK/resources/"
cp "$REPO/LICENSE" "$WORK/resources/license.txt"
sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

PKG="$OUT_DIR/ArtFreq-$VERSION-macOS.pkg"
rm -f "$PKG"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" \
             --resources "$WORK/resources" --version "$VERSION" "$PKG"
echo "built $PKG"
