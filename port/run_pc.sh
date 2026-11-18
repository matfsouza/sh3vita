#!/bin/bash
# Builds then runs the PC build without a dialog box, with a maximum delay (s).
# Usage: bash port/run_pc.sh [secondes]
set -u
cd "$(dirname "$0")"
export PATH="/j/tools/mingw64/bin:$PATH"
mingw32-make -f Makefile.pc -j16 SHELL=sh > ../build/pc/build.log 2>&1 || { grep -E 'error|undefined' ../build/pc/build.log | head -30; exit 1; }
cd ../build/pc
rm -f sh3log.txt
SH3_HEADLESS=1 timeout "${1:-30}" ./sh3.exe "../game"
echo "exit=$?"
tail -n 40 sh3log.txt
