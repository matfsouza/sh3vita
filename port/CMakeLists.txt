cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "")
endif()

project(sh3vita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Silent Hill 3 Enhanced Edition")   # bubble title
set(VITA_TITLEID "SH3EE0001" CACHE STRING "VPK title ID")
set(VITA_VERSION "01.20" CACHE STRING "VPK application version (keep the title ID unchanged)")

# C code generated from the player's own exe (port/gen/, never versioned): run recomp/x86rec.py first.
file(GLOB GEN_SRC ${CMAKE_SOURCE_DIR}/gen/*.c)
file(GLOB RT_SRC
  src/rt/*.c src/win32/*.c src/d3d8/*.c src/dx/*.c src/vita/*.c src/third_party/tlsf/tlsf.c)

# Vita RAM is tight: guest memory (20 MB image + game heap) and host heap.
if(NOT DEFINED SH3_GUEST_MB)
  set(SH3_GUEST_MB 256) #  200 left ~87 MB of heap after the 113 MB image and area changes failed to allocate
endif()
# Host heap: the malloc arena only uses ~5 MB in game (measured); the rest of the reserved block was lost to the guest heap.
set(SH3_HOST_HEAP_MB 24)
if(NOT DEFINED SH3_VGL_RAM_MB)
  set(SH3_VGL_RAM_MB 32) # RAM taken by vitaGL; SH3_SPARE_MB stays free for SDL and the threads
endif()
set(SH3_SPARE_MB 12)

add_executable(sh3 ${GEN_SRC} ${RT_SRC})
option(SH3_ALLOC_PROF "Diagnostic: host-heap allocation site profiler (not for release)" OFF)
if(SH3_ALLOC_PROF)
  target_compile_definitions(sh3 PRIVATE SH3_ALLOC_PROF=1)
  target_link_options(sh3 PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free)
endif()
option(SH3_TEST_OPTIMIZATIONS "Enable qualified experimental optimizations in a tester VPK" OFF)
if(SH3_TEST_OPTIMIZATIONS)
  target_compile_definitions(sh3 PRIVATE SH3_TEST_OPTIMIZATIONS=1)
endif()
option(SH3_TEST_QUALITY_512 "Enable combined 512 texture qualification profile (requires corrected isolated vitaGL)" OFF)
if(SH3_TEST_QUALITY_512)
  if(NOT SH3_TEST_OPTIMIZATIONS)
    message(FATAL_ERROR "The combined quality profile requires SH3_TEST_OPTIMIZATIONS")
  endif()
  target_compile_definitions(sh3 PRIVATE SH3_TEST_QUALITY_512=1)
endif()
target_include_directories(sh3 PRIVATE src src/rt src/d3d8 gen "$ENV{VITASDK}/arm-vita-eabi/include/SDL2")
target_compile_definitions(sh3 PRIVATE
  SH3_GUEST_MB=${SH3_GUEST_MB} SH3_HOST_HEAP_MB=${SH3_HOST_HEAP_MB} SH3_VGL_RAM_MB=${SH3_VGL_RAM_MB} SH3_SPARE_MB=${SH3_SPARE_MB}
  SH3_LOG_PATH="ux0:data/sh3/sh3log.txt")
target_compile_options(sh3 PRIVATE -O2
  -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable -Wno-int-conversion)
# Generated code: -Os by default (measured -3 ms/frame in the hospital vs -O2; the guest code is huge -> I-cache footprint). -DSH3_GEN_FLAGS="" for -O2.
set(SH3_GEN_FLAGS "-Os" CACHE STRING "extra compile flags for generated guest code")
# Hot functions (tools/hotlist.py -> gen/x86_hot.h): __attribute__((hot)) -> contiguous. Measured: no gain (hospital 37.5 ms with or without) -> only with -DSH3_HOT=ON.
if(EXISTS ${CMAKE_SOURCE_DIR}/gen/x86_hot.h AND SH3_HOT)
  set_property(SOURCE ${GEN_SRC} APPEND PROPERTY COMPILE_OPTIONS -include ${CMAKE_SOURCE_DIR}/gen/x86_hot.h)
endif()
if(SH3_GEN_FLAGS)
  separate_arguments(SH3_GEN_FLAGS_LIST UNIX_COMMAND "${SH3_GEN_FLAGS}")
  set_property(SOURCE ${GEN_SRC} APPEND PROPERTY COMPILE_OPTIONS ${SH3_GEN_FLAGS_LIST})
endif()
# Movie (pl_mpeg + NEON conversion): hot decoding loop, -O3.
set_source_files_properties(src/dx/movie.c PROPERTIES COMPILE_OPTIONS "-O3")

set(SH3_VITAGL_LIBRARY "vitaGL" CACHE STRING "vitaGL library (build with NO_DEBUG=1 STORE_DEPTH_STENCIL=1)")
target_link_libraries(sh3
  ${SH3_VITAGL_LIBRARY} vitashark SceShaccCgExt mathneon taihen_stub
  SDL2
  SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub SceSysmodule_stub SceAvPlayer_stub SceGxm_stub
  SceCommonDialog_stub SceAppMgr_stub SceTouch_stub SceHid_stub SceMotion_stub SceKernelDmacMgr_stub
  SceShaccCg_stub SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub SceKernelModulemgr_stub
  ScePower_stub SceProcessmgr_stub SceRtc_stub SceIme_stub SceJpeg_stub
  stdc++ m z)

target_link_options(sh3 PRIVATE -Wl,--allow-multiple-definition)
set(SH3_SCE_PADDING "${CMAKE_CURRENT_SOURCE_DIR}/src/vita/sce-padding.ld")
target_link_options(sh3 PRIVATE "LINKER:-T,${SH3_SCE_PADDING}")
set_property(TARGET sh3 APPEND PROPERTY LINK_DEPENDS "${SH3_SCE_PADDING}")
vita_create_self(eboot.bin sh3 UNSAFE)
set(SH3_SHADER_CACHE_DIR "${CMAKE_SOURCE_DIR}/../build/shader-cache" CACHE PATH "validated compiled GPU cache bundle")
option(SH3_REQUIRE_SHADER_CACHE "require a compiled cache bundle for a release package" OFF)
set(SH3_CACHE_VPK_FILES)
if(EXISTS "${SH3_SHADER_CACHE_DIR}/manifest.json")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(SH3_CACHE_TOOL "${CMAKE_SOURCE_DIR}/../tools/shader_cache_bundle.py")
  set(SH3_CACHE_HEADER "${CMAKE_SOURCE_DIR}/src/d3d8/shader_cache_version.h")
  file(STRINGS "${SH3_CACHE_HEADER}" SH3_CACHE_ABI_LINE REGEX "^#define SH3_SHADER_CACHE_ABI")
  string(REGEX REPLACE ".*\"([a-z0-9]+)\".*" "\\1" SH3_CACHE_ABI "${SH3_CACHE_ABI_LINE}")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${SH3_CACHE_TOOL}" validate "${SH3_SHADER_CACHE_DIR}"
    --cmake-list "${CMAKE_BINARY_DIR}/shader-cache-files.cmake" RESULT_VARIABLE SH3_CACHE_RESULT)
  if(NOT SH3_CACHE_RESULT EQUAL 0)
    message(FATAL_ERROR "Compiled shader bundle validation failed")
  endif()
  include("${CMAKE_BINARY_DIR}/shader-cache-files.cmake")
  foreach(SH3_CACHE_FILE IN LISTS SH3_BUNDLED_CACHE_FILES)
    list(APPEND SH3_CACHE_VPK_FILES FILE "${SH3_SHADER_CACHE_DIR}/${SH3_CACHE_FILE}" "shadercache/${SH3_CACHE_ABI}/${SH3_CACHE_FILE}")
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SH3_SHADER_CACHE_DIR}/manifest.json" "${SH3_CACHE_TOOL}" "${SH3_CACHE_HEADER}")
  add_custom_target(sh3-validate-shader-cache
    COMMAND "${Python3_EXECUTABLE}" "${SH3_CACHE_TOOL}" validate "${SH3_SHADER_CACHE_DIR}")
  add_dependencies(sh3 sh3-validate-shader-cache)
elseif(SH3_REQUIRE_SHADER_CACHE)
  message(FATAL_ERROR "A release package requires SH3_SHADER_CACHE_DIR with a validated cache bundle")
endif()
# Pictures of ui/ (TGA): stored gzip-compressed in the VPK (tools/gz_assets.py, read with gzread), so installing
# writes ~3x fewer bytes. sh3_ui(source) queues a picture under its own name in ui/.
set(SH3_UI_SRC)
macro(sh3_ui SRC)
  list(APPEND SH3_UI_SRC ${CMAKE_SOURCE_DIR}/${SRC})
endmacro()
sh3_ui(assets/controls/controls.tga)
# Controls screen: one picture per game language and control layout (tools/make_controls_help.ps1)
foreach(SH3_LANG RANGE 7)
  foreach(SH3_LAYOUT RANGE 2)
    set(SH3_CTL controls_${SH3_LANG}_${SH3_LAYOUT}.tga)
    if(EXISTS ${CMAKE_SOURCE_DIR}/livearea_user/${SH3_CTL})   # the version over the wallpaper (official artwork)
      sh3_ui(livearea_user/${SH3_CTL})
    else()
      sh3_ui(assets/credits/${SH3_CTL})
    endif()
  endforeach()
endforeach()
# LiveArea / boot artwork: port/livearea_user (made by tools/make_livearea.py and make_credits.ps1 from the user's
# pictures, not versioned) wins over the versioned placeholders file by file.
set(SH3_ART_FILES)
foreach(SH3_ART icon0.png bg.png startup.png pic0.png)
  if(EXISTS ${CMAKE_SOURCE_DIR}/livearea_user/${SH3_ART})
    set(SH3_ART_${SH3_ART} livearea_user/${SH3_ART})
  else()
    set(SH3_ART_${SH3_ART} livearea/${SH3_ART})
  endif()
endforeach()
foreach(SH3_UI version.tga)   # (credits.tga: the in-game credits screen was removed)
  if(EXISTS ${CMAKE_SOURCE_DIR}/livearea_user/${SH3_UI})
    sh3_ui(livearea_user/${SH3_UI})
  else()
    sh3_ui(assets/credits/${SH3_UI})
  endif()
endforeach()
# one-time "Preparing graphics" screen (tools/make_prep_screen.ps1); without it the port draws a plain bar
if(EXISTS ${CMAKE_SOURCE_DIR}/livearea_user/prep.tga)
  sh3_ui(livearea_user/prep.tga)
elseif(EXISTS ${CMAKE_SOURCE_DIR}/assets/credits/prep.tga)
  sh3_ui(assets/credits/prep.tga)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/livearea_user/loading.tga)
  sh3_ui(livearea_user/loading.tga)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SH3_GZ_DIR ${CMAKE_BINARY_DIR}/uigz)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/gz_assets.py" "${SH3_GZ_DIR}" ${SH3_UI_SRC}
  RESULT_VARIABLE SH3_GZ_RESULT)
if(NOT SH3_GZ_RESULT EQUAL 0)
  message(FATAL_ERROR "tools/gz_assets.py failed")
endif()
foreach(SH3_SRC IN LISTS SH3_UI_SRC)
  get_filename_component(SH3_NAME ${SH3_SRC} NAME)
  list(APPEND SH3_ART_FILES FILE ${SH3_GZ_DIR}/${SH3_NAME} ui/${SH3_NAME})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SH3_SRC})
endforeach()
vita_create_vpk(sh3.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${SH3_ART_icon0.png} sce_sys/icon0.png
  FILE ${SH3_ART_pic0.png} sce_sys/pic0.png
  FILE ${SH3_ART_bg.png} sce_sys/livearea/contents/bg.png
  FILE ${SH3_ART_startup.png} sce_sys/livearea/contents/startup.png
  FILE livearea/template.xml sce_sys/livearea/contents/template.xml
  ${SH3_ART_FILES}
  FILE assets/controls/README.md ui/CONTROLLER-ART-CREDITS.md
  FILE assets/controls/Kenney-License.txt ui/Kenney-License.txt
  ${SH3_CACHE_VPK_FILES})

# Diagnostics: ring of the last guest functions (cmake -DSH3_TRACE_CALLS=ON), printed before a FATAL.
option(SH3_TRACE_CALLS "ring buffer of recent guest function entries" OFF)
if(SH3_TRACE_CALLS)
  set_property(SOURCE ${GEN_SRC} APPEND PROPERTY COMPILE_OPTIONS
    -finstrument-functions -finstrument-functions-exclude-file-list=rt.h)
  target_compile_definitions(sh3 PRIVATE SH3_TRACE_CALLS=1)
endif()

# Sampled profile of the guest code (cmake -DSH3_PROFILE=ON, env SH3_PROFILE=start,end); see src/rt/calltrace.c.
option(SH3_PROFILE "sampling profiler of guest functions" OFF)
if(SH3_PROFILE)
  # Preserve the release guest optimization/layout flags when profiling.
  set_property(SOURCE ${GEN_SRC} APPEND PROPERTY COMPILE_OPTIONS
    -finstrument-functions -finstrument-functions-exclude-file-list=rt.h)
  target_compile_definitions(sh3 PRIVATE SH3_PROFILE=1)
endif()
