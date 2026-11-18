#!/usr/bin/env bash
# Prepares a Vita test in one command: builds the VPK and updates the "Teste Vita" folder next to the project
# (a mirror of the Vita's ux0:, see docs/VITA-TESTE.md). Usage, from sh3vita/:
#   bash tools/teste_vita.sh            build the VPK, then update the folder
#   bash tools/teste_vita.sh --sem-build  only update the folder (VPK already built)
# The game files come from build/gamedata (tools/prepare_data.py); only what changed is listed in "O QUE MUDOU.txt".
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="../Teste Vita"
if [ "${1:-}" != "--sem-build" ]; then
    bash port/build_vita.sh
fi
[ -f build/sh3.vpk ] || { echo "build/sh3.vpk não existe: rode sem --sem-build" >&2; exit 1; }
[ -d build/gamedata/data ] || { echo "build/gamedata não existe: rode tools/prepare_data.py (docs/mapa/Preparador e mods.md)" >&2; exit 1; }
.venv/Scripts/python tools/vita_package.py build/gamedata build/sh3.vpk "$OUT"
cp docs/VITA-TESTE.md "$OUT/LEIA-ME.txt"; rm -f "$OUT/LEIA-ME.md"
cat "$OUT/O QUE MUDOU.txt"
