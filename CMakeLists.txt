# PS Vita build of pokefirered (PLATFORM_VITA on top of VitaSDK / vitaGL / SDL2).
#
# Prerequisites (from the repository root, see docs/guides/BUILD.md):
#   make -f make_tools.mk -j8 CC="gcc -static" CXX="g++ -static"
#   make -f vita/assets.mk -j8
# Configure and build:
#   powershell.exe -ExecutionPolicy Bypass -File .\build_vita.ps1
#   # or inside MSYS2: ./build_vita.sh

cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED ENV{VITASDK})
        message(FATAL_ERROR "VITASDK is not set; see docs/guides/BUILD.md")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()

project(pokefirered_vita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# Override VitaSDK's default -O3 Release flags so platform & game compile cleanly at -O2 without
# duplicate optimization flags (-O3 ... -O2).
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C Release flags" FORCE)

set(VITA_APP_NAME "FireRed Vita")
set(VITA_TITLEID  "PKFR00001")
set(VITA_VERSION  "00.10")

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
if(CMAKE_HOST_WIN32)
    set(HOST_EXE ".exe")
else()
    set(HOST_EXE "")
endif()
set(PREPROC "${ROOT}/tools/preproc/preproc${HOST_EXE}")

if(NOT EXISTS "${PREPROC}" OR NOT EXISTS "${ROOT}/charmap.txt")
    message(FATAL_ERROR "Host tools or charmap.txt are missing. Run make -f make_tools.mk first.")
endif()
if(NOT EXISTS "${ROOT}/include/constants/map_groups.h")
    message(FATAL_ERROR "Generated assets are missing. Run:\n  make -f vita/assets.mk -j8")
endif()

find_program(VITA_CPP     arm-vita-eabi-cpp     HINTS "$ENV{VITASDK}/bin" REQUIRED)
find_program(VITA_AS      arm-vita-eabi-as      HINTS "$ENV{VITASDK}/bin" REQUIRED)
find_program(VITA_LD      arm-vita-eabi-ld      HINTS "$ENV{VITASDK}/bin" REQUIRED)
find_program(VITA_OBJCOPY arm-vita-eabi-objcopy HINTS "$ENV{VITASDK}/bin" REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GAME_DEFINES
    -DNONMATCHING
    -DPORTABLE
    -DPORT_BRIDGE
    -DPLATFORM_VITA
    -DRENDERER_FAST_DRAW
    -DMODERN=1
    -DUBFIX
    -DFIRERED
    -DREVISION=1
    -DENGLISH)

option(VITA_LOGGING "Write VITA_LOG output to ux0:data/FRVITA/debug.log" OFF)
if(VITA_LOGGING)
    list(APPEND GAME_DEFINES -DVITA_LOGGING)
endif()

option(VITA_TELEMETRY "Write perf_frames.csv, perf_summary.txt and ui_stall.log" OFF)
option(VITA_GPU_PROBE "Telemetry plus scene-boundary GPU probes and automatic bench" OFF)
if(VITA_TELEMETRY OR VITA_GPU_PROBE)
    list(APPEND GAME_DEFINES -DVITA_TELEMETRY)
endif()
if(VITA_GPU_PROBE)
    list(APPEND GAME_DEFINES -DVITA_GPU_PROBE)
endif()

# Active in Phase F4+: Core 2 parallel DrawWorkerMain + NEON BGR555->RGB565 conversion.
option(VITA_PARALLEL_DRAW "Draw each frame on Core 2 workers (Phase F4+)" ON)
if(VITA_PARALLEL_DRAW)
    list(APPEND GAME_DEFINES -DVITA_PARALLEL_DRAW)
endif()

# Active in Phase F5+: 3D Voxel overworld renderer on Core 0 + Core 2 chunk/I/O workers.
option(VITA_VOXEL_3D "Enable 3D Voxel overworld renderer (Phase F5+)" ON)
if(VITA_VOXEL_3D)
    list(APPEND GAME_DEFINES -DCTR_VOXEL_ENABLED=1 -DVOXEL_SUPPORTED)
else()
    list(APPEND GAME_DEFINES -DCTR_VOXEL_ENABLED=0)
endif()

# Active in Phase F3+: MainThread_Boot/MainThread_Run split and GameFrame boundary in place.
option(VITA_CORE_BOUNDARY "Verify Core 0/1/2 thread boundary on linked ELF (Phase F3+)" ON)
# A translated build compiles a staging tree (tools/locales/localize.py), not a git
# checkout: it passes VITA_DOC_CHECK=OFF, since the checker reads the repository and the English
# build already checks the documents.
option(VITA_DOC_CHECK "Verify docs against code and links (tools/doc_check)" ON)

# Language of the build (docs/guides/ADDING_A_LANGUAGE.md): one code of tools/locales/languages.json, the only
# list of languages. It sets the game's GAME_LANGUAGE and, for any language but English, the native
# UI's catalog (ui2/locale.bin) and its atlas; metric languages show metric Pokedex units.
set(VITA_LOCALE "en" CACHE STRING "Language of the build: a code in tools/locales/languages.json")
set(LANGUAGES_JSON_FILE "${ROOT}/tools/locales/languages.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LANGUAGES_JSON_FILE}")
file(READ "${LANGUAGES_JSON_FILE}" LANGUAGES_JSON)
string(JSON LANGUAGE_COUNT LENGTH "${LANGUAGES_JSON}" languages)
math(EXPR LANGUAGE_LAST "${LANGUAGE_COUNT} - 1")
set(LOCALE_GAME_LANGUAGE "")
set(LOCALE_UNITS "")
foreach(LANGUAGE_INDEX RANGE ${LANGUAGE_LAST})
    string(JSON LANGUAGE_CODE GET "${LANGUAGES_JSON}" languages ${LANGUAGE_INDEX} code)
    if(LANGUAGE_CODE STREQUAL VITA_LOCALE)
        string(JSON LOCALE_GAME_LANGUAGE GET "${LANGUAGES_JSON}" languages ${LANGUAGE_INDEX} gameLanguage)
        string(JSON LOCALE_UNITS GET "${LANGUAGES_JSON}" languages ${LANGUAGE_INDEX} units)
    endif()
endforeach()
if(NOT LOCALE_GAME_LANGUAGE)
    message(FATAL_ERROR "VITA_LOCALE '${VITA_LOCALE}' is not in ${LANGUAGES_JSON_FILE}")
endif()
list(APPEND GAME_DEFINES -DGAME_LANGUAGE=${LOCALE_GAME_LANGUAGE})
if(NOT VITA_LOCALE STREQUAL "en")
    list(APPEND GAME_DEFINES -DUI_LOCALE_CATALOG)
endif()
if(LOCALE_UNITS STREQUAL "metric")
    list(APPEND GAME_DEFINES -DUI_METRIC_UNITS)
endif()
message(STATUS "Build language: ${VITA_LOCALE} (${LOCALE_GAME_LANGUAGE}, ${LOCALE_UNITS})")

string(REPLACE ";" " " GAME_DEFINES_STR "${GAME_DEFINES}")

# --- C sources: cpp -> preproc (charmap strings, INCBIN) -> compiled below ---
file(GLOB_RECURSE GAME_C CONFIGURE_DEPENDS "${ROOT}/src/*.c")
list(FILTER GAME_C EXCLUDE REGEX "\\.inc\\.c$")
# GBA-only / unused hardware units and retired Stage 1 3DS UI excluded on PS Vita (stubs in fr_vita_bridge.c).
list(FILTER GAME_C EXCLUDE REGEX "/src/(berry_fix_program|isagbprn)\\.c$|/src/platform/gba_easy_draw\\.c$|/src/platform/fr3ds/fr_bottom_ui\\.c$|/src/platform/vita/fr_vita_ui\\.c$")
if(NOT VITA_VOXEL_3D)
    list(FILTER GAME_C EXCLUDE REGEX "/src/platform/voxel/")
endif()

set(GENERATED_C)
foreach(SOURCE IN LISTS GAME_C)
    file(RELATIVE_PATH RELATIVE_SOURCE "${ROOT}" "${SOURCE}")
    set(OUTPUT "${CMAKE_BINARY_DIR}/generated/${RELATIVE_SOURCE}")
    get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
    set(EXTRA_PREPROC_FLAGS "")
    set(EXTRA_PREPROC_DEPS)
    # Stage scenes (one full GBA picture each): redirect SetVBlankCallback so vita_stage.c tracks them.
    if(RELATIVE_SOURCE MATCHES "^src/(intro|title_screen|credits|seagallop)\\.c$")
        set(EXTRA_PREPROC_FLAGS "-DCTR_GBA_STAGE -include '${ROOT}/src/platform/vita/compat/ctr_gba_stage.h'")
        list(APPEND EXTRA_PREPROC_DEPS "${ROOT}/src/platform/vita/compat/ctr_gba_stage.h")
    endif()
    add_custom_command(
        OUTPUT "${OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIR}"
        COMMAND /bin/sh -c "cd '${ROOT}' && '${CMAKE_C_COMPILER}' -E -MMD -MF '${OUTPUT}.d' -MT 'generated/${RELATIVE_SOURCE}' -std=gnu99 -Wno-trigraphs -iquote '${ROOT}/include' -iquote '${ROOT}/src/platform/compat' -iquote '${ROOT}/src' ${GAME_DEFINES_STR} ${EXTRA_PREPROC_FLAGS} '${SOURCE}' -o '${OUTPUT}.i' && '${PREPROC}' '${OUTPUT}.i' '${ROOT}/charmap.txt' > '${OUTPUT}.tmp' && mv '${OUTPUT}.tmp' '${OUTPUT}'"
        DEPFILE "${OUTPUT}.d"
        DEPENDS "${SOURCE}" "${PREPROC}" "${ROOT}/charmap.txt"
                "${ROOT}/src/platform/compat/port_platform.h"
                "${ROOT}/src/platform/compat/port_prof.h"
                ${EXTRA_PREPROC_DEPS}
        VERBATIM)
    list(APPEND GENERATED_C "${OUTPUT}")
endforeach()

# --- Data and song assembly, relinked into one object (generated/game_data.o) ---
file(GLOB DATA_ASM CONFIGURE_DEPENDS "${ROOT}/data/*.s")
list(FILTER DATA_ASM EXCLUDE REGEX "/data/multiboot_(berry_glitch_fix|ereader|pokemon_colosseum)\\.s$")
file(GLOB SONG_ASM CONFIGURE_DEPENDS "${ROOT}/sound/songs/*.s" "${ROOT}/sound/songs/midi/*.s")

set(SPECIAL_IDS "${CMAKE_BINARY_DIR}/generated/special_ids.inc")
add_custom_command(
    OUTPUT "${SPECIAL_IDS}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND /bin/sh -c "awk '/^[[:space:]]+def_special[[:space:]]/ { printf \".set SPECIAL_%s, %d\\n\", $2, id++ }' '${ROOT}/data/specials.inc' > '${SPECIAL_IDS}.tmp' && mv '${SPECIAL_IDS}.tmp' '${SPECIAL_IDS}'"
    DEPENDS "${ROOT}/data/specials.inc"
    VERBATIM)

set(ASM_DEFSYMS --defsym FIRERED=1 --defsym REVISION=1 --defsym ENGLISH=1 --defsym MODERN=1 --defsym PORTABLE=1 --defsym UBFIX=1)
string(REPLACE ";" " " ASM_DEFSYMS_STR "${ASM_DEFSYMS}")

set(GENERATED_ASM)
foreach(SOURCE IN LISTS DATA_ASM SONG_ASM)
    file(RELATIVE_PATH RELATIVE_SOURCE "${ROOT}" "${SOURCE}")
    set(OUTPUT "${CMAKE_BINARY_DIR}/generated/${RELATIVE_SOURCE}.o")
    get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
    if(SOURCE MATCHES "/sound/songs/")
        add_custom_command(
            OUTPUT "${OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIR}"
            COMMAND /bin/bash -eo pipefail -c "cd '${ROOT}' && sed -e 's/\\.4byte/\\.word/g;s/\\.2byte/\\.short/g' '${RELATIVE_SOURCE}' > '${OUTPUT}.s' && '${VITA_AS}' -march=armv7-a ${ASM_DEFSYMS_STR} -I sound -o '${OUTPUT}' '${OUTPUT}.s'"
            DEPENDS "${SOURCE}"
            VERBATIM)
    else()
        add_custom_command(
            OUTPUT "${OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIR}"
            COMMAND /bin/bash -eo pipefail -c "cd '${ROOT}' && { printf '.include \"special_ids.inc\"\\n'; '${PREPROC}' '${RELATIVE_SOURCE}' charmap.txt | '${VITA_CPP}' -I include -nostdinc -undef -Wno-unicode | tr -d '\\r' | '${PREPROC}' -ie '${RELATIVE_SOURCE}' charmap.txt | sed -e 's/\\.4byte/\\.word/g;s/\\.2byte/\\.short/g'; } > '${OUTPUT}.s' && '${VITA_AS}' -march=armv7-a ${ASM_DEFSYMS_STR} -I . -I sound -I '${CMAKE_BINARY_DIR}/generated' -o '${OUTPUT}' '${OUTPUT}.s'"
            DEPENDS "${SOURCE}" "${PREPROC}" "${ROOT}/charmap.txt" "${SPECIAL_IDS}"
            VERBATIM)
    endif()
    list(APPEND GENERATED_ASM "${OUTPUT}")
endforeach()

# Game data holds runtime pointers (maps.s, event_scripts.s, sound_data.s), so CMake renames its
# .rodata to writable .data before final link. Response file keeps paths relative to avoid MSYS2
# command-line length limits.
set(GAME_DATA_OBJECT "${CMAKE_BINARY_DIR}/generated/game_data.o")
set(GAME_DATA_RSP "${CMAKE_BINARY_DIR}/generated/game_data.rsp")
set(GAME_DATA_RSP_CONTENT "")
foreach(OBJECT IN LISTS GENERATED_ASM)
    file(RELATIVE_PATH RELATIVE_OBJECT "${CMAKE_BINARY_DIR}/generated" "${OBJECT}")
    string(APPEND GAME_DATA_RSP_CONTENT "${RELATIVE_OBJECT}\n")
endforeach()
file(WRITE "${GAME_DATA_RSP}" "${GAME_DATA_RSP_CONTENT}")
add_custom_command(
    OUTPUT "${GAME_DATA_OBJECT}"
    COMMAND "${VITA_LD}" -r -o game_data.o.tmp @game_data.rsp
    COMMAND "${VITA_OBJCOPY}" --rename-section .rodata=.data,alloc,load,data,contents game_data.o.tmp game_data.o.renamed
    COMMAND "${CMAKE_COMMAND}" -E rename game_data.o.renamed game_data.o
    DEPENDS ${GENERATED_ASM} "${GAME_DATA_RSP}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/generated"
    COMMENT "Linking game data (generated/game_data.o)"
    VERBATIM)
set_source_files_properties("${GAME_DATA_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)

# --- Game side and platform side split ---
# All mutable game state (RAM, emulated VRAM/OAM/PLTT/registers in bios.c, sound, flash save,
# and Stage 1 fr_bottom_ui.c canvas) links into game_combined.o (.data -> gamedata, .bss ->
# gamebss) for SoftReset snapshots (src/platform/vita/vita_reset.c).
set(PLATFORM_SIDE_REGEX "/src/platform/(widescreen|gba_fast_draw|gba_fast_draw_b|gba_fast_draw_c)\\.c$|/src/platform/(vita|voxel|ui)/")
set(GAME_SIDE_C)
set(PLATFORM_SIDE_C)
foreach(OUTPUT IN LISTS GENERATED_C)
    if(OUTPUT MATCHES "${PLATFORM_SIDE_REGEX}")
        list(APPEND PLATFORM_SIDE_C "${OUTPUT}")
    else()
        list(APPEND GAME_SIDE_C "${OUTPUT}")
    endif()
endforeach()

set(VITA_CPU_TUNING_OPTIONS
    -mcpu=cortex-a9
    -mtune=cortex-a9
    -mfpu=neon
    -mfloat-abi=hard)

set(COMMON_INCLUDE_DIRS
    "${ROOT}/include"
    "${ROOT}/src/platform/compat"
    "${ROOT}/src"
    "${ROOT}"
    "${ROOT}/sound")

set(COMMON_COMPILE_OPTIONS
    -std=gnu99
    ${VITA_CPU_TUNING_OPTIONS}
    -fno-strict-aliasing
    -fwrapv
    -fcommon
    -fsigned-char
    "SHELL:-iquote '${ROOT}/include'"
    "SHELL:-iquote '${ROOT}/src/platform/compat'")

# -fno-builtin is scoped strictly to game_side so platform code keeps GCC math/copy builtins.
set(GAME_COMPILE_OPTIONS
    ${COMMON_COMPILE_OPTIONS}
    -fno-builtin
    -Wno-implicit-function-declaration
    -Wno-error=incompatible-pointer-types
    -Wno-error=int-conversion
    -Wno-error=implicit-int
    -Wno-address-of-packed-member)

set(PLATFORM_COMPILE_OPTIONS
    ${COMMON_COMPILE_OPTIONS}
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-missing-field-initializers
    -fno-math-errno
    # Required by tools/core_boundary so every static object keeps its own symbol without anchor offsets.
    -fno-section-anchors)

add_library(game_side OBJECT ${GAME_SIDE_C})
target_include_directories(game_side PRIVATE ${COMMON_INCLUDE_DIRS})
target_compile_options(game_side PRIVATE ${GAME_COMPILE_OPTIONS})

set(GAME_COMBINED_OBJECT "${CMAKE_BINARY_DIR}/game_combined.o")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/game_side.abs.rsp"
     CONTENT "$<JOIN:$<TARGET_OBJECTS:game_side>,\n>\n")
add_custom_command(
    OUTPUT "${GAME_COMBINED_OBJECT}"
    COMMAND /bin/sh -c "sed -e 's|^${CMAKE_BINARY_DIR}/||' game_side.abs.rsp > game_side.rsp"
    COMMAND "${VITA_LD}" -r -d -o game_combined.o.tmp @game_side.rsp
    COMMAND "${VITA_OBJCOPY}" --rename-section .data=gamedata --rename-section .bss=gamebss game_combined.o.tmp game_combined.o.renamed
    COMMAND "${CMAKE_COMMAND}" -E rename game_combined.o.renamed game_combined.o
    DEPENDS game_side $<TARGET_OBJECTS:game_side> "${CMAKE_BINARY_DIR}/game_side.abs.rsp"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Linking the game side (game_combined.o)"
    VERBATIM)
set_source_files_properties("${GAME_COMBINED_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)

# --- Executable ---
add_executable(pokefirered ${PLATFORM_SIDE_C} "${GAME_COMBINED_OBJECT}" "${GAME_DATA_OBJECT}")
target_include_directories(pokefirered PRIVATE ${COMMON_INCLUDE_DIRS})
target_compile_options(pokefirered PRIVATE ${PLATFORM_COMPILE_OPTIONS})

# Build version stamp (gVitaBuildVersion) regenerated cleanly whenever git describe changes.
find_package(Git QUIET)
set(GAME_VERSION_C "${CMAKE_BINARY_DIR}/version/game_version.c")
add_custom_target(game_version
    COMMAND "${CMAKE_COMMAND}" -DROOT=${ROOT} -DGIT=${GIT_EXECUTABLE} -DOUT=${GAME_VERSION_C}
            -P "${ROOT}/tools/version/game_version.cmake"
    BYPRODUCTS "${GAME_VERSION_C}"
    COMMENT "Reading the game version (git describe)"
    VERBATIM)
add_dependencies(pokefirered game_version)
target_sources(pokefirered PRIVATE "${GAME_VERSION_C}")

find_package(SDL2 REQUIRED CONFIG)
target_link_libraries(pokefirered PRIVATE
    SDL2::SDL2-static
    vitaGL
    vitashark
    mathneon
    SceShaccCgExt
    SceShaccCg_stub
    SceKernelDmacMgr_stub
    taihen_stub
    SceGxm_stub
    SceDisplay_stub
    ScePower_stub
    SceAudio_stub
    SceCtrl_stub
    SceTouch_stub
    SceRtc_stub
    stdc++
    m)
target_link_options(pokefirered PRIVATE
    -Wl,-q
    -Wl,-z,nocopyreloc
    -Wl,-z,max-page-size=0x10000
    -Wl,-z,common-page-size=0x10000)

# Leaves room after the executable LOAD segment for SCE module info appended by vita-elf-create.
set_property(TARGET pokefirered PROPERTY RULE_LAUNCH_LINK "bash '${ROOT}/vita/vita_link.sh'")

# UNSAFE: the game reads and writes ux0:data/FRVITA/.
vita_create_self(eboot.bin pokefirered UNSAFE)

# --- Build-time gates (Phase F1/F3) ---
if(VITA_CORE_BOUNDARY)
    set(CORE_BOUNDARY_FLAGS "")
    if(VITA_PARALLEL_DRAW)
        list(APPEND CORE_BOUNDARY_FLAGS --require-draw-workers)
    endif()
    if(VITA_VOXEL_3D)
        list(APPEND CORE_BOUNDARY_FLAGS --require-voxel-workers)
    endif()
    set(CORE_BOUNDARY_STAMP "${CMAKE_BINARY_DIR}/core_boundary.ok")
    add_custom_command(
        OUTPUT "${CORE_BOUNDARY_STAMP}"
        COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/core_boundary/check_core_boundary.py" --strict
                ${CORE_BOUNDARY_FLAGS}
                --elf "$<TARGET_FILE:pokefirered>"
                --game "${GAME_COMBINED_OBJECT}"
                --game-data "${GAME_DATA_OBJECT}"
                --allow "${ROOT}/tools/core_boundary/allow.txt"
                --report "${CMAKE_BINARY_DIR}/core_boundary_report.txt"
        COMMAND "${CMAKE_COMMAND}" -E touch "${CORE_BOUNDARY_STAMP}"
        DEPENDS pokefirered "$<TARGET_FILE:pokefirered>"
                "${ROOT}/tools/core_boundary/check_core_boundary.py"
                "${ROOT}/tools/core_boundary/allow.txt"
        COMMENT "Checking thread boundary (tools/core_boundary)"
        VERBATIM)
    add_custom_target(core_boundary ALL DEPENDS "${CORE_BOUNDARY_STAMP}")
    add_dependencies(eboot.bin-self core_boundary)
endif()

if(VITA_DOC_CHECK)
    file(GLOB_RECURSE DOC_FILES CONFIGURE_DEPENDS "${ROOT}/docs/*.md")
    set(DOC_CHECK_STAMP "${CMAKE_BINARY_DIR}/doc_check.ok")
    add_custom_command(
        OUTPUT "${DOC_CHECK_STAMP}"
        COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/doc_check/check_docs.py" --strict
                --report "${CMAKE_BINARY_DIR}/doc_check_report.txt"
        COMMAND "${CMAKE_COMMAND}" -E touch "${DOC_CHECK_STAMP}"
        DEPENDS pokefirered "$<TARGET_FILE:pokefirered>" ${DOC_FILES}
                "${ROOT}/README.md" "${ROOT}/CONTRIBUTING.md" "${ROOT}/NOTICE.md"
                "${ROOT}/tools/doc_check/check_docs.py" "${ROOT}/tools/doc_check/allow.txt"
        COMMENT "Checking documentation (tools/doc_check)"
        VERBATIM)
    add_custom_target(doc_check ALL DEPENDS "${DOC_CHECK_STAMP}")
    add_dependencies(eboot.bin-self doc_check)
endif()

# The list of the UI's strings (tools/locales/en/ui.json, the keys of every catalog) must match the
# UI_TR / UI_N strings of the code, or a new string would never reach the translators.
file(GLOB_RECURSE UI_TEXT_SOURCES CONFIGURE_DEPENDS "${ROOT}/src/platform/*.c" "${ROOT}/src/platform/*.h")
set(UI_STRINGS_STAMP "${CMAKE_BINARY_DIR}/ui_strings.ok")
add_custom_command(
    OUTPUT "${UI_STRINGS_STAMP}"
    COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/locales/extract_ui.py" --check
    COMMAND "${CMAKE_COMMAND}" -E touch "${UI_STRINGS_STAMP}"
    DEPENDS ${UI_TEXT_SOURCES} "${ROOT}/tools/locales/extract_ui.py" "${ROOT}/tools/locales/en/ui.json"
    COMMENT "Checking the UI string list (tools/locales/extract_ui.py)"
    VERBATIM)
add_custom_target(ui_strings ALL DEPENDS "${UI_STRINGS_STAMP}")
add_dependencies(eboot.bin-self ui_strings)

# --- VPK Packaging ---
set(SCE_SYS "${ROOT}/vita/sce_sys")
set(SCE_SYS_FILES
    icon0.png
    pic0.png
    livearea/contents/bg.png
    livearea/contents/startup.png
    livearea/contents/template.xml)
set(VPK_FILE_ARGS)
set(VPK_FILE_DEPS)
foreach(FILE_NAME IN LISTS SCE_SYS_FILES)
    set(SCE_FILE "${SCE_SYS}/${FILE_NAME}")
    if(EXISTS "${ROOT}/vita/sce_sys_custom/${FILE_NAME}")
        set(SCE_FILE "${ROOT}/vita/sce_sys_custom/${FILE_NAME}")
    endif()
    list(APPEND VPK_FILE_ARGS -a "${SCE_FILE}=sce_sys/${FILE_NAME}")
    list(APPEND VPK_FILE_DEPS "${SCE_FILE}")
endforeach()

# Future Phase F5-F7 external assets (packaged automatically when present)
file(GLOB VOXEL_ASSETS CONFIGURE_DEPENDS "${ROOT}/assets/voxel/*.*")
foreach(VA IN LISTS VOXEL_ASSETS)
    get_filename_component(VA_NAME "${VA}" NAME)
    list(APPEND VPK_FILE_ARGS -a "${VA}=voxel/${VA_NAME}")
    list(APPEND VPK_FILE_DEPS "${VA}")
endforeach()

# A non-English build packs the atlas with its language's letters, which build_vita.sh writes into
# the build folder (tools/ui2/build_atlas.py --locale needs Pillow, which the Python CMake finds may
# not have), and the UI catalog compiled from tools/locales/<code>/ui.json.
file(GLOB UI2_ASSETS CONFIGURE_DEPENDS "${ROOT}/assets/ui2/*.bin")
foreach(UA IN LISTS UI2_ASSETS)
    get_filename_component(UA_NAME "${UA}" NAME)
    if(NOT VITA_LOCALE STREQUAL "en" AND UA_NAME STREQUAL "ui_atlas.bin")
        set(UA "${CMAKE_BINARY_DIR}/ui2/ui_atlas.bin")
        if(NOT EXISTS "${UA}")
            message(FATAL_ERROR "${UA} is missing: build a non-English VPK with build_vita.sh (VITA_LOCALE=${VITA_LOCALE})")
        endif()
    endif()
    list(APPEND VPK_FILE_ARGS -a "${UA}=ui2/${UA_NAME}")
    list(APPEND VPK_FILE_DEPS "${UA}")
endforeach()
if(NOT VITA_LOCALE STREQUAL "en")
    set(LOCALE_BIN "${CMAKE_BINARY_DIR}/ui2/locale.bin")
    add_custom_command(OUTPUT "${LOCALE_BIN}"
        COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/locales/compile_ui.py" --locale "${VITA_LOCALE}" --out "${LOCALE_BIN}"
        DEPENDS "${ROOT}/tools/locales/compile_ui.py" "${LANGUAGES_JSON_FILE}"
                "${ROOT}/tools/locales/en/ui.json" "${ROOT}/tools/locales/${VITA_LOCALE}/ui.json"
        COMMENT "Compiling the ${VITA_LOCALE} UI catalog"
        VERBATIM)
    list(APPEND VPK_FILE_ARGS -a "${LOCALE_BIN}=ui2/locale.bin")
    list(APPEND VPK_FILE_DEPS "${LOCALE_BIN}")
endif()

# HGSS Follower Pokemon assets packed into app0:followers/followers.pak
file(GLOB_RECURSE FOLLOWER_ASSETS CONFIGURE_DEPENDS
    "${ROOT}/assets/followers/mon/*.bin"
    "${ROOT}/assets/followers/text/*.bin")
if(FOLLOWER_ASSETS)
    set(FOLLOWERS_PAK "${CMAKE_BINARY_DIR}/followers.pak")
    add_custom_command(
        OUTPUT "${FOLLOWERS_PAK}"
        COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/pack_followers.py"
            --input "${ROOT}/assets/followers"
            --output "${FOLLOWERS_PAK}"
        DEPENDS "${ROOT}/tools/pack_followers.py" ${FOLLOWER_ASSETS}
        COMMENT "Packing followers.pak"
        VERBATIM)
    list(APPEND VPK_FILE_ARGS -a "${FOLLOWERS_PAK}=followers/followers.pak")
    list(APPEND VPK_FILE_DEPS "${FOLLOWERS_PAK}")
endif()

set(PARAM_SFO "${CMAKE_BINARY_DIR}/param.sfo")
add_custom_command(
    OUTPUT "${PARAM_SFO}"
    COMMAND "${VITA_MKSFOEX}" -d ATTRIBUTE2=12 -s APP_VER=${VITA_VERSION} -s TITLE_ID=${VITA_TITLEID} "${VITA_APP_NAME}" "${PARAM_SFO}"
    DEPENDS "${CMAKE_BINARY_DIR}/eboot.bin"
    COMMENT "Generating param.sfo"
    VERBATIM)
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/pokefirered.vpk"
    COMMAND "${VITA_PACK_VPK}" ${VPK_FILE_ARGS} -s "${PARAM_SFO}" -b "${CMAKE_BINARY_DIR}/eboot.bin" "${CMAKE_BINARY_DIR}/pokefirered.vpk"
    DEPENDS "${PARAM_SFO}" "${CMAKE_BINARY_DIR}/eboot.bin" ${VPK_FILE_DEPS}
    COMMENT "Building pokefirered.vpk"
    VERBATIM)
add_custom_target(pokefirered-vpk ALL DEPENDS "${CMAKE_BINARY_DIR}/pokefirered.vpk")
add_dependencies(pokefirered-vpk eboot.bin-self)
