# Echelon @build Codex 12/08/2026 Universal SDL3 launcher host for both engine modules.
add_executable(echelon_launcher
    EngineSession.cpp
    LauncherMain.cpp
    LauncherDataImport.cpp
    ${ECHELON_SOURCE_DIR}/EngineIntegration/Source/ProcessAllocator.cpp
    LauncherInstaller.cpp
    LauncherLocks.cpp
    LauncherLocalization.cpp
    LauncherModProfiles.cpp
    LauncherMods.cpp
    LauncherSettings.cpp
    LauncherWorkspace.cpp
    PresentationService.cpp
)

# Echelon @build Codex 13/08/2026 Keep settings persistence testable without starting either engine module.
add_executable(echelon_settings_tests EXCLUDE_FROM_ALL
    EngineSession.cpp
    ${ECHELON_SOURCE_DIR}/EngineIntegration/Source/ContentLayerRuntime.cpp
    LauncherSettingsTests.cpp
    LauncherDataImport.cpp
    LauncherDownloads.cpp
    LauncherInstaller.cpp
    LauncherLocks.cpp
    LauncherLocalization.cpp
    LauncherModProfiles.cpp
    LauncherMods.cpp
    LauncherRepositories.cpp
    LauncherS3.cpp
    LauncherSettings.cpp
    LauncherWorkspace.cpp
)

target_compile_definitions(echelon_settings_tests PRIVATE ECHELON_ENGINE_HOSTED=1)

# Echelon @build Codex 05/09/2026 Declare filesystem requirements on both product targets, independently of engine directory settings.
target_compile_features(echelon_launcher PRIVATE cxx_std_17)
target_compile_features(echelon_settings_tests PRIVATE cxx_std_17)

target_link_libraries(echelon_settings_tests PRIVATE
    echelon_brand
    sdl3lib
    CURL::libcurl
    OpenSSL::Crypto
    archive_static
    yaml-cpp::yaml-cpp
)

target_include_directories(echelon_settings_tests PRIVATE
    ${ECHELON_SOURCE_DIR}/EngineIntegration/Include
)

target_link_libraries(echelon_launcher PRIVATE
    echelon_brand
    sdl3lib
    stb
    CURL::libcurl
    OpenSSL::Crypto
    archive_static
    yaml-cpp::yaml-cpp
)

target_include_directories(echelon_launcher PRIVATE
    ${ECHELON_SOURCE_DIR}/EngineIntegration/Include
)

target_compile_definitions(echelon_launcher PRIVATE
    ECHELON_GENERALS_MODULE_FILE="$<TARGET_FILE_NAME:g_generals_module>"
    ECHELON_ZEROHOUR_MODULE_FILE="$<TARGET_FILE_NAME:z_generals_module>"
    ECHELON_LAUNCHER_BACKGROUND_FILE="echelon-launcher-background.png"
    ECHELON_LAUNCHER_LOGO_FILE="echelon-logo.png"
    ECHELON_LAUNCHER_FONT_FILE="DejaVuSans.ttf"
)

set_target_properties(echelon_launcher PROPERTIES
    OUTPUT_NAME "${ECHELON_PRODUCT_SLUG}"
    RUNTIME_OUTPUT_DIRECTORY "${ECHELON_BINARY_DIR}/${ECHELON_OUTPUT_DIRECTORY}"
)

add_dependencies(echelon_launcher g_generals_module z_generals_module)
if(TARGET sage_patch)
    add_dependencies(echelon_launcher sage_patch)
endif()

add_custom_command(TARGET echelon_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ECHELON_SOURCE_DIR}/assets/launcher/echelon-launcher-background.png"
        "$<TARGET_FILE_DIR:echelon_launcher>/echelon-launcher-background.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ECHELON_SOURCE_DIR}/assets/launcher/echelon-logo.png"
        "$<TARGET_FILE_DIR:echelon_launcher>/echelon-logo.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ECHELON_SOURCE_DIR}/assets/launcher/fonts/DejaVuSans.ttf"
        "$<TARGET_FILE_DIR:echelon_launcher>/DejaVuSans.ttf"
    VERBATIM
)

install(TARGETS echelon_launcher RUNTIME DESTINATION bin)
install(TARGETS g_generals_module z_generals_module
    LIBRARY DESTINATION "${ECHELON_INSTALL_MODULE_DIRECTORY}"
    RUNTIME DESTINATION "${ECHELON_INSTALL_MODULE_DIRECTORY}"
)
install(FILES
    "${ECHELON_SOURCE_DIR}/assets/launcher/echelon-launcher-background.png"
    "${ECHELON_SOURCE_DIR}/assets/launcher/echelon-logo.png"
    "${ECHELON_SOURCE_DIR}/assets/launcher/echelon-icon.png"
    "${ECHELON_SOURCE_DIR}/assets/launcher/fonts/DejaVuSans.ttf"
    DESTINATION "${ECHELON_INSTALL_DATA_DIRECTORY}"
)

enable_testing()
add_test(NAME echelon_local_content COMMAND echelon_settings_tests)

install(FILES "${ECHELON_SOURCE_DIR}/assets/launcher/fonts/LICENSE"
    DESTINATION "share/licenses/echelon" RENAME DejaVu-LICENSE)
