# Echelon @build Codex 05/09/2026 Give Meson the same SDL instance used by the launcher and hosted engines.
if(NOT TARGET SDL3-shared OR NOT SDL3_SOURCE_DIR OR NOT SDL3_BINARY_DIR)
    message(FATAL_ERROR "DXVK requires the configured shared SDL3 build")
endif()

set(ECHELON_DXVK_PKGCONFIG_DIR "${ECHELON_BINARY_DIR}/generated/dxvk-pkgconfig/$<CONFIG>")
file(GENERATE OUTPUT "${ECHELON_DXVK_PKGCONFIG_DIR}/sdl3.pc" CONTENT
"Name: SDL3
Description: Echelon shared SDL3 build
Version: ${SDL3_VERSION}
Libs: \"$<TARGET_FILE:SDL3-shared>\" -Wl,-rpath,\"$<TARGET_FILE_DIR:SDL3-shared>\"
Cflags: -I\"${SDL3_SOURCE_DIR}/include\" -I\"${SDL3_BINARY_DIR}/include-revision\"
")
set(ECHELON_DXVK_PKGCONFIG_ENV "PKG_CONFIG_PATH=${ECHELON_DXVK_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}")
