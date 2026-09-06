# Echelon @build Codex 05/09/2026 Compile hosted engines independently of upstream monolithic targets.
configure_file(
    "${ECHELON_SOURCE_DIR}/Launcher/BrandIdentity.h.in"
    "${ECHELON_BINARY_DIR}/generated/LauncherIntegration/BrandIdentity.h"
    @ONLY
)
configure_file("${ECHELON_SOURCE_DIR}/Launcher/BrandIdentity.json.in"
    "${ECHELON_BINARY_DIR}/generated/brand.json" @ONLY)
add_library(echelon_brand INTERFACE)
target_include_directories(echelon_brand INTERFACE "${ECHELON_BINARY_DIR}/generated")

function(echelon_add_host_variant base)
    # Echelon @build Codex 06/09/2026 Propagate SDL3 headers to hosted engine variants.
    # The upstream game-engine target includes SDL3 directly, while the launcher owns the
    # shared SDL library; keep this bridge in the product layer instead of editing upstream.
    if(SAGE_USE_SDL3 AND TARGET SDL3::Headers)
        target_link_libraries(${base} PUBLIC SDL3::Headers)
    endif()
	get_target_property(sources ${base} SOURCES)
    add_library(${base}_host STATIC ${sources})
    foreach(property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES COMPILE_OPTIONS
            COMPILE_DEFINITIONS PRECOMPILE_HEADERS INTERFACE_COMPILE_OPTIONS
            INTERFACE_COMPILE_DEFINITIONS LINK_OPTIONS INTERFACE_LINK_OPTIONS)
        get_target_property(value ${base} ${property})
        if(value)
            set_property(TARGET ${base}_host PROPERTY ${property} "${value}")
        endif()
    endforeach()
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(value ${base} ${property})
        if(value)
            string(REGEX REPLACE "(^|;)g_gameengine(;|$)" "\\1g_gameengine_host\\2" value "${value}")
            string(REGEX REPLACE "(^|;)z_gameengine(;|$)" "\\1z_gameengine_host\\2" value "${value}")
            set_property(TARGET ${base}_host PROPERTY ${property} "${value}")
        endif()
    endforeach()
    target_link_libraries(${base}_host PRIVATE echelon_brand)
    target_compile_definitions(${base}_host PRIVATE
        ECHELON_BRAND=1 ECHELON_ENGINE_HOSTED=1 ECHELON_ENGINE_MODULE_ALLOCATOR=1
    )
    set_target_properties(${base}_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
