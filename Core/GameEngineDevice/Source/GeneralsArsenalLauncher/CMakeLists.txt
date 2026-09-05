# GeneralsArsenal @build Codex 12/08/2026 Universal SDL3 launcher host for both engine modules.
add_executable(generals_arsenal_launcher
    LauncherMain.cpp
    LauncherDownloads.cpp
    LauncherInstaller.cpp
	LauncherLocalization.cpp
    LauncherModProfiles.cpp
    LauncherMods.cpp
    LauncherRepositories.cpp
    LauncherS3.cpp
    LauncherSettings.cpp
)

# GeneralsArsenal @build Codex 13/08/2026 Keep settings persistence testable without starting either engine module.
add_executable(generals_arsenal_settings_tests EXCLUDE_FROM_ALL
    ContentLayerRuntime.cpp
    LauncherSettingsTests.cpp
    LauncherDownloads.cpp
    LauncherInstaller.cpp
	LauncherLocalization.cpp
    LauncherModProfiles.cpp
    LauncherMods.cpp
    LauncherRepositories.cpp
    LauncherS3.cpp
    LauncherSettings.cpp
)

target_link_libraries(generals_arsenal_settings_tests PRIVATE
    sdl3lib
    CURL::libcurl
    OpenSSL::Crypto
    archive_static
    yaml-cpp::yaml-cpp
)

target_include_directories(generals_arsenal_settings_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
)

target_link_libraries(generals_arsenal_launcher PRIVATE
    sdl3lib
    stb
    CURL::libcurl
    OpenSSL::Crypto
    archive_static
    yaml-cpp::yaml-cpp
)

target_include_directories(generals_arsenal_launcher PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
)

target_compile_definitions(generals_arsenal_launcher PRIVATE
    GENERALS_ARSENAL_GENERALS_MODULE_FILE="$<TARGET_FILE_NAME:g_generals_module>"
    GENERALS_ARSENAL_ZEROHOUR_MODULE_FILE="$<TARGET_FILE_NAME:z_generals_module>"
    GENERALS_ARSENAL_LAUNCHER_BACKGROUND_FILE="generals-arsenal-launcher-background.png"
    GENERALS_ARSENAL_LAUNCHER_LOGO_FILE="generals-arsenal-logo.png"
    GENERALS_ARSENAL_LAUNCHER_FONT_FILE="DejaVuSans.ttf"
)

set_target_properties(generals_arsenal_launcher PROPERTIES
    OUTPUT_NAME "${GENERALS_ARSENAL_PRODUCT_SLUG}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${GENERALS_ARSENAL_OUTPUT_DIRECTORY}"
)

add_dependencies(generals_arsenal_launcher g_generals_module z_generals_module)

add_custom_command(TARGET generals_arsenal_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/launcher/generals-arsenal-launcher-background.png"
        "$<TARGET_FILE_DIR:generals_arsenal_launcher>/generals-arsenal-launcher-background.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/launcher/generals-arsenal-logo.png"
        "$<TARGET_FILE_DIR:generals_arsenal_launcher>/generals-arsenal-logo.png"
    VERBATIM
)

install(TARGETS generals_arsenal_launcher RUNTIME DESTINATION bin)
install(TARGETS g_generals_module z_generals_module
    LIBRARY DESTINATION "${GENERALS_ARSENAL_INSTALL_MODULE_DIRECTORY}"
    RUNTIME DESTINATION "${GENERALS_ARSENAL_INSTALL_MODULE_DIRECTORY}"
)
install(FILES
    "${CMAKE_SOURCE_DIR}/assets/launcher/generals-arsenal-launcher-background.png"
    "${CMAKE_SOURCE_DIR}/assets/launcher/generals-arsenal-logo.png"
    "${CMAKE_SOURCE_DIR}/assets/launcher/generals-arsenal-icon.png"
    DESTINATION "${GENERALS_ARSENAL_INSTALL_DATA_DIRECTORY}"
)
