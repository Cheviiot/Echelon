add_executable(g_generals WIN32)

# Use a binary name that doesn't conflict with original game.
# GeneralsX @build BenderAI 15/02/2026 Linux branding update
# GeneralsX @build fbraz3 19/08/2026 Standardize executable name across all platforms
set_target_properties(g_generals PROPERTIES OUTPUT_NAME GeneralsX)

# GeneralsX @build BenderAI 10/03/2026 Windows-specific libraries
if(WIN32)
    target_link_libraries(g_generals PRIVATE
        binkstub
        comctl32
        d3d8
        dinput8
        dxguid
        imm32
        milesstub
        vfw32
        winmm
    )
endif()

# Cross-platform core libraries
target_link_libraries(g_generals PRIVATE
    d3dx8
    g_gameengine
    g_gameenginedevice
    gi_always
)

# Linux SDL3 windowing/input
# GeneralsX @build BenderAI 11/02/2026
if(SAGE_USE_SDL3)
    target_link_libraries(g_generals PRIVATE sdl3lib)
endif()

# TODO Originally referred to build host and user, replace with git info perhaps?
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h
"#pragma once

#define VERSION_LOCALBUILDNUM 0
#define VERSION_BUILDUSER \"\"
#define VERSION_BUILDLOC \"\"
"
)

# Based on original binary values for these variables.
if (IS_VS6_BUILD)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h
"#pragma once

#define VERSION_MAJOR 1
#define VERSION_MINOR 7
#define VERSION_BUILDNUM 601
"
)
else()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h
"#pragma once

#define VERSION_MAJOR 1
#define VERSION_MINOR 8
#define VERSION_BUILDNUM 601
"
)
endif()

if(MSVC)
    target_link_options(g_generals PRIVATE "/NODEFAULTLIB:libci.lib")
endif()

target_include_directories(g_generals PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GENERALSX_BINARY_DIR}/Code/Libraries
)

target_sources(g_generals PRIVATE
    $<$<BOOL:${WIN32}>:WinMain.cpp>
    $<$<BOOL:${WIN32}>:WinMain.h>
    # Linux SDL3 entry point
    $<$<NOT:$<BOOL:${WIN32}>>:SDL3Main.cpp>
    # Linux stubs
    $<$<NOT:$<BOOL:${WIN32}>>:LinuxStubs.cpp>
)

# RC files are optional for MinGW builds
# Icon: LoadIcon() handles missing resource gracefully (uses default system icon)
# Manifest: DPI awareness metadata (nice to have but not essential)
# TYPELIB: Broken (0 bytes) and causes windres memory exhaustion errors
if(MSVC)
    # VS2005 and later adds default manifest, we need to turn it off to prevent conflict with custom manifest
    if(NOT IS_VS6_BUILD)
        target_link_options(g_generals PRIVATE
            "/MANIFEST:NO"
        )
    endif()

    target_sources(g_generals PRIVATE
        RTS.RC
    )
endif()

# Strip debug symbols to separate file for MinGW Release builds
# This creates generalsv${RTS_BUILD_OUTPUT_SUFFIX}.exe.debug (similar to MSVC .pdb files)
if(MINGW AND COMMAND add_debug_strip_target)
    add_debug_strip_target(g_generals)
endif()

# GeneralsX @build Codex 11/08/2026 Build the base game as a private universal-launcher module.
if(RTS_BUILD_UNIVERSAL_LAUNCHER AND SAGE_USE_SDL3 AND UNIX)
    add_library(g_generals_module MODULE
        SDL3Main.cpp
        LinuxStubs.cpp
    )
    target_link_libraries(g_generals_module PRIVATE
        d3dx8
        g_gameengine_host
        g_gameenginedevice_host
        gi_always
        sdl3lib
    )
    target_include_directories(g_generals_module PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${GENERALSX_BINARY_DIR}/Code/Libraries
    )
    target_compile_definitions(g_generals_module PRIVATE
        ECHELON_ENGINE_HOSTED=1
        ECHELON_ENGINE_MODULE=1
        ECHELON_ENGINE_BUILD=1
    )
    set_target_properties(g_generals_module PROPERTIES
        OUTPUT_NAME EchelonGeneralsEngine
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        LIBRARY_OUTPUT_DIRECTORY "${ECHELON_BINARY_DIR}/${ECHELON_OUTPUT_DIRECTORY}"
    )
    if(APPLE)
        target_link_options(g_generals_module PRIVATE "-Wl,-exported_symbol,_Echelon_GetEngineModuleV2")
    else()
        target_link_options(g_generals_module PRIVATE
            "-Wl,--version-script=${ECHELON_SOURCE_DIR}/EngineIntegration/Source/engine-module.exports"
            "-Wl,--exclude-libs,ALL"
            "-Wl,-Bsymbolic"
        )
    endif()
endif()
