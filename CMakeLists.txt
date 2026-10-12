cmake_minimum_required(VERSION 3.25)
project(wots4_mod_harakiri LANGUAGES C CXX)

# When building standalone, pass -DWOTS4_SDK_DIR=<path to wots4-mods>
if(NOT COMMAND wots4_add_mod)
    set(WOTS4_SDK_DIR "" CACHE PATH "Path to wots4-mods (ModLoader)")
    if(NOT EXISTS "${WOTS4_SDK_DIR}/cmake/Wots4.cmake")
        message(FATAL_ERROR "Set WOTS4_SDK_DIR to the path of wots4-mods")
    endif()
    include(${WOTS4_SDK_DIR}/cmake/Wots4.cmake)
    enable_testing()
    set(WOTS4_BUILD_TESTS ON)
endif()

# Use the Mod's folder name in the game folder (mods/<name>/) as <name>
wots4_use_minhook()
wots4_add_mod(harakiri SOURCES src/mod.cpp DEF src/mod.def
    LIBS wots4_minhook FILES config.ini LICENSES docs/licenses/minhook.txt)
if(WOTS4_BUILD_TESTS)
    wots4_add_probe(harakiri_damage_tests SOURCES tests/damage_tests.cpp LIBS wots4_minhook)
    wots4_add_probe(harakiri_ui_tests SOURCES tests/ui_tests.cpp LIBS wots4_minhook)
    set(HARAKIRI_TEST_EXE "${WOTS4_ROOT}/research/bin/WayOfTheSamurai4.unpacked.exe"
        CACHE FILEPATH "Decrypted game copy for offline hook tests")
    if(EXISTS "${HARAKIRI_TEST_EXE}")
        wots4_add_probe(harakiri_exe_tests SOURCES tests/exe_tests.cpp LIBS wots4_minhook
            ARGS "${HARAKIRI_TEST_EXE}" NEEDS_EXE)
    endif()
endif()
