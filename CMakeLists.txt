cmake_minimum_required(VERSION 3.25)
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/GeneralsX" CACHE PATH "Upstream dependency manifest")
project(Echelon LANGUAGES C CXX)

set(ECHELON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ECHELON_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
option(RTS_BUILD_UNIVERSAL_LAUNCHER "Build Echelon with both hosted engines" ${UNIX})
enable_testing()

# Echelon @build Codex 05/09/2026 Keep upstream source and build roots explicit.
include_directories("${ECHELON_SOURCE_DIR}/EngineIntegration/Include")
add_subdirectory(GeneralsX)
# Echelon @build Codex 06/09/2026 Track the module export map and expose V3 on Apple without editing upstream.
set(ECHELON_ENGINE_EXPORTS_FILE "${ECHELON_SOURCE_DIR}/EngineIntegration/Source/engine-module.exports")
foreach(ECHELON_ENGINE_MODULE g_generals_module z_generals_module)
    if(TARGET ${ECHELON_ENGINE_MODULE})
        set_property(TARGET ${ECHELON_ENGINE_MODULE} APPEND PROPERTY LINK_DEPENDS "${ECHELON_ENGINE_EXPORTS_FILE}")
        if(APPLE)
            target_link_options(${ECHELON_ENGINE_MODULE} PRIVATE "-Wl,-exported_symbol,_Echelon_GetEngineModuleV3")
        endif()
    endif()
endforeach()
