#!/bin/bash
# Publish the AppImage linux/make_appimage.sh built, with update-linux.txt, to a GitHub release (replacing the files
# there): the launcher's updater (runtime/beyond_ui/fe_update_linux.cpp) reads update-linux.txt from the latest release
# and offers the AppImage it names when its version is newer than the running one.
# Usage: linux/publish_release.sh [TAG]   (default: the latest release). Needs gh, logged in with write access.
set -e -o pipefail
W="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
D="$W/linux/dist"; APP="$D/Metroid_Prime_4_Recomp-x86_64.AppImage"; MAN="$D/update-linux.txt"
REPO="${MP4_REPO:-$(sed -n 's/^#define MP4_REPO "\(.*\)"/\1/p' "$W/runtime/beyond_ui/fe.h")}"
[ -f "$APP" ] && [ -f "$MAN" ] || { echo "error: build first (linux/make_appimage.sh)" >&2; exit 1; }
# the file list must describe this very AppImage: a rebuilt AppImage with an old update-linux.txt would fail every update
read -r _ SHA SIZE NAME < <(grep '^appimage ' "$MAN")
[ "$NAME" = "$(basename "$APP")" ] && [ "$SIZE" = "$(stat -c %s "$APP")" ] && [ "$SHA" = "$(sha256sum "$APP" | cut -d' ' -f1)" ] \
  || { echo "error: update-linux.txt doesn't match the AppImage (run linux/make_appimage.sh again)" >&2; exit 1; }
grep -q -- '-modified$' <(sed -n 's/^commit //p' "$MAN") && echo "note: built from uncommitted changes"
TAG="${1:-$(gh release view -R "$REPO" --json tagName -q .tagName)}"
echo "publishing $(sed -n 's/^version //p' "$MAN") to $REPO $TAG"
gh release upload "$TAG" -R "$REPO" "$APP" "$MAN" --clobber
# what was uploaded must be what was built
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gh release download "$TAG" -R "$REPO" -p "$(basename "$MAN")" -D "$T"
cmp -s "$T/$(basename "$MAN")" "$MAN" && echo "ok: $(basename "$APP") and $(basename "$MAN") are on $TAG" || { echo "error: the uploaded update-linux.txt differs" >&2; exit 1; }
