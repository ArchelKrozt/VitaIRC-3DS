#!/usr/bin/env bash
# Copies VitaIRC's portable core (IRC, TLS, config, history, translation,
# profiles) from the PS Vita project into core/, adapted for the 3DS:
# paths, names and version. The Vita calls they still make (time, folders,
# random numbers, sleeping) are implemented in source/compat/.
# Run it again whenever the Vita core changes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VITA="${VITA_SRC:-$ROOT/../VITA-IRC/VitaIRC/src}"
OUT="$ROOT/core"
FILES="irc.c irc.h conn.c conn.h config.c config.h history.c history.h util.c util.h
       minijson.c minijson.h net.c net.h httpc.c httpc.h social.c social.h i18n.c i18n.h"
mkdir -p "$OUT"
for f in $FILES; do
	sed -e 's|ux0:data/VitaIRC|sdmc:/3ds/VitaIRC|g' \
	    -e 's|"ux0:data"|"sdmc:/3ds"|g' \
	    -e 's|app0:|romfs:/|g' \
	    -e 's|PlayStation Vita|Nintendo 3DS|g' \
	    -e 's|"Vita%04u"|"Vita3DS%04u"|' \
	    -e 's|edited with VitaShell|edited on a computer (SD card: 3ds/VitaIRC)|' \
	    -e 's|^#define APP_VERSION .*|#define APP_VERSION   "1.0"|' \
	    -e 's|^#define UPDATE_REPO .*|#define UPDATE_REPO   "ArchelKrozt/VitaIRC-3DS"|' \
	    -e 's|^const char \*T(const char \*es)$|const char *T_core(const char *es)|' \
	    -e 's|^#define MAX_MSGS      500|#define MAX_MSGS      300|' \
	    -e 's|utf8_to_utf16|vu_utf8_to_utf16|g' -e 's|utf16_to_utf8|vu_utf16_to_utf8|g' \
	    "$VITA/$f" > "$OUT/$f"
done
echo "core/ updated from $VITA"
