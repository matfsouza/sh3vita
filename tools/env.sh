# Windows development environment (Git Bash): source tools/env.sh
# Tool locations; override any of them with the environment variable. SH3_TOOLS is the folder holding SDL2, Ghidra,
# the VitaSDK and the build folders (no spaces in the path).
export SH3_TOOLS="${SH3_TOOLS:-$HOME/tools}"
export SH3_SDL2_DIR="${SH3_SDL2_DIR:-$SH3_TOOLS/sdl2/SDL2-2.32.10/x86_64-w64-mingw32}"
export SH3_GHIDRA="${SH3_GHIDRA:-$SH3_TOOLS/ghidra_12.2_PUBLIC}"
export SH3_JAVA_HOME="${SH3_JAVA_HOME:-C:/Program Files/Microsoft/jdk-25.0.5.21-hotspot}"
# GCC/Make from WinLibs installed with winget
_winlibs=$(ls -d "$HOME"/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin 2>/dev/null | head -1)
[ -n "$_winlibs" ] && export PATH="$_winlibs:$PATH"
unset _winlibs
