# Vita build environment on Windows (Git Bash), no WSL: source tools/env_vita.sh
# Includes tools/env.sh (WinLibs GCC/Make, SDL2, Ghidra/JDK) and adds the native VitaSDK and CMake.
_here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$_here/env.sh"
export VITASDK="${VITASDK:-$SH3_TOOLS/vitasdk}"
_cmake=$(ls -d "$HOME"/AppData/Local/Microsoft/WinGet/Packages/Kitware.CMake*/cmake-*/bin 2>/dev/null | head -1)
# Kitware's CMake comes before the one shipped with WinLibs.
export PATH="$VITASDK/bin:${_cmake:+$_cmake:}$PATH"
unset _here _cmake
