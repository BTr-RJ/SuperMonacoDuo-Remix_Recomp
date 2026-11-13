cmake_minimum_required(VERSION 3.21)
project(SuperMonacoDuoRecomp VERSION 0.60.1 LANGUAGES C CXX)
option(SMGP_COPY_ROOT_LAUNCHER "Update the root launcher after a local build" ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(RECOMP_ROOT "${CMAKE_SOURCE_DIR}/.tools/engine")
set(RUNNER_ROOT "${RECOMP_ROOT}/runner")
set(GENESIS_VDP_PLANE_IMPL "SCALAR" CACHE STRING "Qualified Super Monaco plane renderer" FORCE)
include("${RECOMP_ROOT}/cmake/GenesisRecompGenerated.cmake")
set(SDL_ROOT "${RUNNER_ROOT}/external/SDL2/SDL2-2.28.5")
add_library(SDL2::SDL2 SHARED IMPORTED)
set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_IMPLIB "${SDL_ROOT}/lib/x64/SDL2.lib"
 IMPORTED_LOCATION "${SDL_ROOT}/lib/x64/SDL2.dll" INTERFACE_INCLUDE_DIRECTORIES "${SDL_ROOT}/include;${SDL_ROOT}/include/SDL2")
genesisrecomp_runner_sources(RUNNER_SOURCES TRACE ON REVERSE_DEBUG OFF)
list(REMOVE_ITEM RUNNER_SOURCES "${RUNNER_ROOT}/main.c")
# rcheevos (MIT, RetroAchievements.org): only the offline trigger runtime is built.
file(GLOB RC_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/third_party/rcheevos/src/runtime/*.c"
 "${CMAKE_SOURCE_DIR}/src/third_party/rcheevos/src/util/*.c")
add_library(rcheevos_runtime STATIC ${RC_SOURCES})
target_include_directories(rcheevos_runtime PUBLIC "${CMAKE_SOURCE_DIR}/src/third_party/rcheevos/include")
target_compile_definitions(rcheevos_runtime PRIVATE _CRT_SECURE_NO_WARNINGS RC_DISABLE_LUA)
foreach(game smgp1 smgp2)
 set(input "${CMAKE_SOURCE_DIR}/.local/build-inputs/${game}")
 genesisrecomp_add_generated_sources(GENERATED ${game} "${input}" "game.bin")
 add_executable(${game} ${RUNNER_SOURCES} "${RUNNER_ROOT}/main.c" "${RUNNER_ROOT}/game_cycles.c"
  src/game/game_spec.c src/game/controls.cpp src/game/control_config.cpp src/achievements/achievements.cpp src/audio/sound.cpp src/audio/arcade_board.cpp
  "${RUNNER_ROOT}/external/ymfm/src/ymfm_opm.cpp" src/presentation/gpu_present.cpp src/presentation/motion_bridge.cpp src/presentation/scene.cpp
  src/app.manifest ${GENERATED} "${GENERATED_CYCLES}")
 target_include_directories(${game} PRIVATE "${GENERATED_DIR}" src/game src/presentation src/audio src/achievements
  "${CMAKE_SOURCE_DIR}/.tools/librashader/include" "${RUNNER_ROOT}" "${RUNNER_ROOT}/include"
  "${RUNNER_ROOT}/video" "${RUNNER_ROOT}/audio" "${RUNNER_ROOT}/external/clowncommon"
  "${RUNNER_ROOT}/external/superzazu" "${RUNNER_ROOT}/external/ymfm/src"
  "${RECOMP_ROOT}/recompiler/src" "${RECOMP_ROOT}/external/m68k-recomp-core/common"
  "${RECOMP_ROOT}/external/m68k-recomp-core/profiles/genesis")
 target_compile_definitions(${game} PRIVATE _CRT_SECURE_NO_WARNINGS SDL_MAIN_HANDLED
  GEN_ENABLE_TRACE=1 RECOMP_LAUNCHER=0 GENESIS_HAS_RECOMP_NET=0 SONIC_REVERSE_DEBUG=0
  GENESIS_GAME_VERSION="${PROJECT_VERSION}" GAME_${game}=1)
 target_compile_options(${game} PRIVATE /W3 /bigobj /utf-8)
 target_link_libraries(${game} PRIVATE SDL2::SDL2 rcheevos_runtime retcomm_rbengine ws2_32 winmm comdlg32 d3d11 dxgi)
 genesisrecomp_runner_target(${game})
 set_target_properties(${game} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${game}")
 add_custom_command(TARGET ${game} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:${game}>")
 foreach(dll librashader.dll D3DX9_43.dll D3DCompiler_43.dll)
  add_custom_command(TARGET ${game} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
   "${CMAKE_SOURCE_DIR}/.tools/librashader/${dll}" "$<TARGET_FILE_DIR:${game}>")
 endforeach()
endforeach()
# QA probes live in tests/, which installed copies do not carry.
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/arcade_sound_probe.cpp" AND EXISTS "${CMAKE_SOURCE_DIR}/tests/controls_probe.cpp")
add_executable(smgp_arcade_sound_probe EXCLUDE_FROM_ALL tests/arcade_sound_probe.cpp src/audio/arcade_board.cpp
 "${RUNNER_ROOT}/external/superzazu/z80.c" "${RUNNER_ROOT}/external/ymfm/src/ymfm_opm.cpp")
target_include_directories(smgp_arcade_sound_probe PRIVATE src/audio "${RUNNER_ROOT}/external/superzazu" "${RUNNER_ROOT}/external/ymfm/src")
target_compile_definitions(smgp_arcade_sound_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(smgp_arcade_sound_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qa-audio")
add_executable(smgp_controls_probe EXCLUDE_FROM_ALL tests/controls_probe.cpp src/game/controls.cpp src/game/control_config.cpp
 "${RUNNER_ROOT}/gamepad.c" "${RUNNER_ROOT}/input_map.c")
target_include_directories(smgp_controls_probe PRIVATE src/game "${RUNNER_ROOT}" "${RUNNER_ROOT}/include"
 "${RUNNER_ROOT}/external/clowncommon")
target_compile_definitions(smgp_controls_probe PRIVATE SDL_MAIN_HANDLED _CRT_SECURE_NO_WARNINGS)
target_link_libraries(smgp_controls_probe PRIVATE SDL2::SDL2)
set_target_properties(smgp_controls_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qa-controls")
add_custom_command(TARGET smgp_controls_probe POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
 "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:smgp_controls_probe>")
endif()
configure_file(src/launcher/launcher.rc.in "${CMAKE_BINARY_DIR}/launcher.rc" @ONLY)
configure_file(assets/launcher/app.ico "${CMAKE_BINARY_DIR}/app.ico" COPYONLY)
add_executable(SuperMonacoDuoRecomp WIN32 src/launcher/main.cpp src/launcher/control_editor.cpp src/launcher/updates.cpp src/launcher/install.cpp src/game/control_config.cpp src/app.manifest "${CMAKE_BINARY_DIR}/launcher.rc")
target_include_directories(SuperMonacoDuoRecomp PRIVATE src/game)
target_compile_definitions(SuperMonacoDuoRecomp PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS SMGP_VERSION="${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
target_compile_options(SuperMonacoDuoRecomp PRIVATE /W3 /utf-8)
target_link_libraries(SuperMonacoDuoRecomp PRIVATE gdiplus shell32 uxtheme comctl32 ole32 advapi32 user32 SDL2::SDL2)
add_custom_command(TARGET SuperMonacoDuoRecomp POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
 "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:SuperMonacoDuoRecomp>")
set_target_properties(SuperMonacoDuoRecomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(SMGP_COPY_ROOT_LAUNCHER)
 add_custom_command(TARGET SuperMonacoDuoRecomp POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "$<TARGET_FILE:SDL2::SDL2>" "${CMAKE_SOURCE_DIR}/SDL2.dll")
 add_custom_command(TARGET SuperMonacoDuoRecomp POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "$<TARGET_FILE:SuperMonacoDuoRecomp>" "${CMAKE_SOURCE_DIR}/SuperMonacoDuoRecomp.exe")
endif()
