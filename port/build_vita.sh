#!/bin/bash
# Build do VPK do Vita no Windows (Git Bash), sem WSL: bash port/build_vita.sh [-DOPCAO=valor ...]
# Precisa de port/gen/ (python recomp/x86rec.py), do VitaSDK e das bibliotecas (tools/build_vitagl.sh).
# Exemplo: bash port/build_vita.sh -DSH3_TRACE_CALLS=OFF -DSH3_GUEST_MB=200
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/../tools/env_vita.sh"
B=${B:-$SH3_TOOLS/build/sh3vita}   # no spaces in the path
MAKE_PROGRAM=$(command -v mingw32-make)

# O CMake do VitaSDK passa os arquivos do VPK (LiveArea) sem aspas ao vita-pack-vpk: um caminho com espaços
# ("Claude Silent Hill 3 Port") quebra o empacotamento. O build enxerga o projeto por uma junction sem espaços
# (docs/BUGS.md BUG-021); junctions são criadas pelo PowerShell (docs/REGRAS.md R-003).
NOSPACE=${SH3_NOSPACE:-$SH3_TOOLS/src/sh3vita}
if [ ! -e "$NOSPACE/port/CMakeLists.txt" ]; then
    mkdir -p "$(dirname "$NOSPACE")"
    proj_win=$(cd "$here/.." && pwd -W)
    powershell -NoProfile -Command "New-Item -ItemType Junction -Path '${NOSPACE//\//\\}' -Target '${proj_win//\//\\}' | Out-Null"
fi
here="$NOSPACE/port"

# vitaGL precisa preservar profundidade/stencil entre cenas (STORE_DEPTH_STENCIL=1): sem isso, atores somem
# depois de trocas de render target. Recusa o build com uma biblioteca antiga.
vgl_lib="$VITASDK/arm-vita-eabi/lib/libvitaGL.a"
for mode in Load Store; do
    if ! arm-vita-eabi-nm -u "$vgl_lib" | grep -F "sceGxmDepthStencilSurfaceSetForce${mode}Mode" > /dev/null; then
        echo "vitaGL sem STORE_DEPTH_STENCIL=1: rode bash tools/build_vitagl.sh" >&2
        exit 1
    fi
done

cmake -S "$here" -B "$B" -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM="$MAKE_PROGRAM" \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release "$@"
# x86rt.c logs __DATE__/__TIME__ ("[build] runtime compiled"): recompiled every build so that the log names the VPK.
touch "$here/src/rt/x86rt.c"
cmake --build "$B" -j"$(nproc)"
mkdir -p "$here/../build"
cp "$B/sh3.vpk" "$here/../build/sh3.vpk"
ls -la "$here/../build/sh3.vpk"
