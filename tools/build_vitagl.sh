#!/bin/bash
# Compila e instala o vitaGL usado pelo port, no Windows (Git Bash), sem WSL: bash tools/build_vitagl.sh
# Revisão fixa + port/patches/vitagl-shared-depth.patch; STORE_DEPTH_STENCIL=1 é obrigatório (sem ele, atores somem
# depois de trocas de render target — checado também por tests/test_vita_libs.py e pelo build do VPK).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/env_vita.sh"
REV=2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8
SRC=${SH3_VITAGL_SRC:-$SH3_TOOLS/src/vitaGL}   # no spaces in the path
PATCH="$here/../port/patches/vitagl-shared-depth.patch"
PATCH2="$here/../port/patches/vitagl-depthless-fbo.patch"   # FBO scenes without depth surface (BUG-029)
MAKE=${MAKE:-mingw32-make}

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone https://github.com/Rinnegatamante/vitaGL "$SRC"
fi
cd "$SRC"
git fetch --quiet origin "$REV" 2>/dev/null || true
git checkout --quiet --force "$REV"
git clean -fdxq
git apply "$PATCH"
git apply "$PATCH2"
"$MAKE" -B NO_DEBUG=1 STORE_DEPTH_STENCIL=1 -j"$(nproc)"
"$MAKE" NO_DEBUG=1 STORE_DEPTH_STENCIL=1 install
echo "vitaGL $REV instalado em $VITASDK/arm-vita-eabi/lib"
