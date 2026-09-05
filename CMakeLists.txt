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
