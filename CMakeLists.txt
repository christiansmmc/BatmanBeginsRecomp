# Batman Begins (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the SDK: sdk/ here, two folders up inside the SDK's templates, a checkout beside this repository, else downloaded
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../..)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp)
else()
    include(FetchContent)
    FetchContent_Declare(xbe_sdk GIT_REPOSITORY https://github.com/TGP482/XboxXBE-Recomp.git GIT_TAG main)
    FetchContent_MakeAvailable(xbe_sdk)
    set(SDK ${xbe_sdk_SOURCE_DIR})
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")

# the options screen's fonts, inside the program; their licences go beside it
set(OPTIONS_FONTS_SOURCE "")
foreach(font IN ITEMS "Michroma:Michroma-Regular" "BarlowMedium:BarlowSemiCondensed-Medium" "BarlowSemiBold:BarlowSemiCondensed-SemiBold")
    string(REPLACE ":" ";" font "${font}")
    list(GET font 0 font_symbol)
    list(GET font 1 font_file)
    set(font_file ${CMAKE_CURRENT_SOURCE_DIR}/src/common/fonts/${font_file}.ttf)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${font_file})
    file(READ ${font_file} font_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," font_bytes "${font_hex}")
    string(APPEND OPTIONS_FONTS_SOURCE "extern const unsigned char kFont${font_symbol}[] = {${font_bytes}};\nextern const unsigned kFont${font_symbol}Size = sizeof(kFont${font_symbol});\n")
endforeach()
set(OPTIONS_FONTS_FILE ${CMAKE_CURRENT_BINARY_DIR}/options_fonts.cpp)
file(CONFIGURE OUTPUT ${OPTIONS_FONTS_FILE} CONTENT "${OPTIONS_FONTS_SOURCE}" @ONLY)
list(APPEND XBE_HOST_SOURCES ${OPTIONS_FONTS_FILE})

# partitions as xbe_symbols wrote them into config/game.toml
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
# no config yet: build.bat writes it with xbe_symbols, the change reconfigures
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# target name = GAME_EXE in build.bat, xbe = GAME_XBE
add_xbe_image(BatmanBegins default.xbe game.toml ${GAME_PARTITIONS} game)

# The program's icon, from the title and save images inside the player's own XBE (tools/icon), so it is never in the
# repository. SDL takes the first icon of the program for its window, so the title bar and taskbar show it too.
enable_language(RC)
add_executable(xbe_icon tools/icon/main.cpp)
target_compile_features(xbe_icon PRIVATE cxx_std_20)
set(GAME_ICON ${CMAKE_CURRENT_BINARY_DIR}/game_icon.ico)
add_custom_command(OUTPUT ${GAME_ICON}
    COMMAND xbe_icon ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe ${GAME_ICON}
    DEPENDS xbe_icon ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe
    COMMENT "Icon from the game's title image")
set(GAME_ICON_RC ${CMAKE_CURRENT_BINARY_DIR}/game_icon.rc)
file(CONFIGURE OUTPUT ${GAME_ICON_RC} CONTENT "1 ICON \"${GAME_ICON}\"\n")
set_source_files_properties(${GAME_ICON_RC} PROPERTIES OBJECT_DEPENDS ${GAME_ICON})
target_sources(BatmanBegins PRIVATE ${GAME_ICON_RC})

# native tests of the host code, without the game's code
enable_testing()
function(add_native_test name)
    add_executable(${name}_test tests/native/${name}_test.cpp ${ARGN})
    target_include_directories(${name}_test PRIVATE src tests/native ${SDK}/runtime)
    target_compile_features(${name}_test PRIVATE cxx_std_20)
    add_test(NAME ${name} COMMAND ${name}_test)
endfunction()
add_native_test(pad_script src/common/pad_script.cpp)
add_native_test(keyboard_pad src/common/keyboard_pad.cpp)
add_native_test(options_model src/common/options_model.cpp)
add_native_test(movie_skip src/common/movie_skip.cpp)
add_native_test(wav_dump)
add_native_test(log sdk/runtime/core/log.cpp)
add_native_test(fps_policy)
add_native_test(frame_step)
add_native_test(vertex_input)
add_native_test(vertex_program)
add_native_test(volume_size)
add_native_test(device_lost)
add_native_test(dred_report)
add_native_test(sampler_bias)
add_native_test(xbe_icon)
target_include_directories(xbe_icon_test PRIVATE tools/icon)
add_native_test(save_commit)
add_native_test(settings_file)
target_link_libraries(settings_file_test PRIVATE tomlplusplus::tomlplusplus)
add_native_test(movie_path)
add_native_test(pixel_shader sdk/runtime/gpu/shaders.cpp)
add_native_test(shader_cache sdk/runtime/gpu/shader_cache.cpp sdk/runtime/gpu/shaders.cpp)

# The executable rebuilds the DLL at start when the DLL is older than it. A host-only build keeps the same exports, so
# the DLL is still valid; if the exports did change, the DLL relinks right after this anyway.
add_custom_command(TARGET BatmanBegins POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${CMAKE_BINARY_DIR}/BatmanBegins.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/src/common/fonts/OFL-Michroma.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/src/common/fonts/OFL-BarlowSemiCondensed.txt ${CMAKE_BINARY_DIR})
