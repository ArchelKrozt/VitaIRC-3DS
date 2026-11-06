#!/usr/bin/env bash
# Builds dist/VitaIRC.3dsx (Homebrew Launcher). Needs devkitPro with the 3ds-dev group
# and 3ds-mbedtls, 3ds-curl, 3ds-zlib.
# make can't handle the spaces of the iCloud path, so the sources are copied to
# ~/.vitairc-3ds-build/src and built there.
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
WORK="$HOME/.vitairc-3ds-build"
mkdir -p "$WORK/src" "$SRC/dist"
rsync -a --delete --exclude .git --exclude dist --exclude build --exclude .DS_Store --exclude '*.3dsx' --exclude '*.smdh' --exclude '*.elf' "$SRC/" "$WORK/src/"
make -C "$WORK/src" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" "$@"
for f in VitaIRC.3dsx VitaIRC.cia; do
	[ -f "$WORK/src/$f" ] && cp -f "$WORK/src/$f" "$SRC/dist/"
done
ls -la "$SRC/dist/"
