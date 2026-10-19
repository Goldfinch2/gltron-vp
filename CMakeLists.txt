cmake_minimum_required(VERSION 3.21)

project(gltron_vp VERSION 1.1.0 LANGUAGES C CXX)

# GLtron on the VectorPie SDK. Builds, from WSL2 Debian (see README.md):
#   cmake --preset pc && cmake --build --preset pc     # build-pc/gltron.exe
#   cmake --preset pi && cmake --build --preset pi     # build-pi/gltron (arm64)

# The installed VectorPie SDK (`cmake --install` in the SDK: CMAKE_PREFIX_PATH
# at ~/VectorPie/dist/pc or /pi, as the presets set it). Reinstall it after
# changing the SDK, then rebuild the game.
find_package(VectorPie 0.1 REQUIRED CONFIG)

set(CMAKE_C_STANDARD 99)

set(GLTRON_SOURCES                     # GLtron's own code, kept as it was
    src/game/event.c
    src/game/engine.c
    src/game/computer.c
    src/game/computer_utilities.c
    src/base/vector.c
    src/base/util.c
    src/base/random.c)

add_executable(gltron
    src/main.c
    src/render.c
    src/game/settings.c
    src/game/level.c
    ${GLTRON_SOURCES})
vp_program(gltron)                     # the SDK's link settings (static .exe, SDL2.dll)
# GLtron's 2003 code predates today's warnings: keep its sources quiet.
set_source_files_properties(${GLTRON_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# The models and sounds go next to the executable, where vp_asset_path finds them.
add_custom_command(TARGET gltron POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/data $<TARGET_FILE_DIR:gltron>/data
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/game.ini ${CMAKE_CURRENT_SOURCE_DIR}/leds.cfg
            $<TARGET_FILE_DIR:gltron>/)

# The marquee (shown by the menu, and on HDMI while playing on the board) is
# not in the repository: put your own marquee.png here. Without one, the
# menu shows its default marquee.
set(MARQUEE ${CMAKE_CURRENT_SOURCE_DIR}/marquee.png)
if(EXISTS ${MARQUEE})
    add_custom_command(TARGET gltron POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${MARQUEE} $<TARGET_FILE_DIR:gltron>/)
else()
    message(STATUS "No marquee.png: the package goes without a marquee")
endif()

# The game package: `cmake --build --preset pi --target vpk` writes
# build-pi/gltron-<version>.vpk, ready for Settings -> GAMES -> INSTALL GAMES
# (on a USB drive's games/ folder, or copied to the cabinet's
# /persistent/games/packages/). The Pi build only.
set(VPK_STAGE ${CMAKE_BINARY_DIR}/vpk/gltron)
set(VPK_MARQUEE_COPY)
if(EXISTS ${MARQUEE})
    set(VPK_MARQUEE_COPY COMMAND ${CMAKE_COMMAND} -E copy ${MARQUEE} ${VPK_STAGE}/)
endif()
add_custom_target(vpk
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/vpk
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VPK_STAGE}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gltron> ${VPK_STAGE}/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${VPK_STAGE}/data
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/game.ini
            ${CMAKE_CURRENT_SOURCE_DIR}/leds.cfg ${VPK_STAGE}/
    ${VPK_MARQUEE_COPY}
    COMMAND sh ${VECTORPIE_PACK} ${VPK_STAGE} ${CMAKE_BINARY_DIR}
    DEPENDS gltron
    VERBATIM)
