# Echelon @build Codex 16/10/2026 Core layer of the layered launcher: std + OpenSSL::Crypto only, no SDL, no network.

add_library(echelon_core STATIC
    Source/OperationError.cpp
    Source/StringUtils.cpp
    Source/Logger.cpp
    Source/IniDocument.cpp
    Source/FileUtils.cpp
    Source/FsLock.cpp
)

target_compile_features(echelon_core PUBLIC cxx_std_17)

target_include_directories(echelon_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

target_link_libraries(echelon_core PRIVATE OpenSSL::Crypto)

file(GLOB ECHELON_CORE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp")

add_executable(echelon_core_tests ${ECHELON_CORE_TEST_SOURCES})

target_link_libraries(echelon_core_tests PRIVATE echelon_core)

target_include_directories(echelon_core_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Tests"
)

add_test(NAME echelon_core COMMAND echelon_core_tests)
