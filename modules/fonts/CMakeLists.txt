# No font library discovery occurs in a core-only configuration. Installed packages are
# used as found; nothing is downloaded.
find_package(harfbuzz CONFIG REQUIRED)
find_package(Freetype 2.13.3 EXACT REQUIRED)
get_target_property(harfbuzz_include_dirs harfbuzz::harfbuzz INTERFACE_INCLUDE_DIRECTORIES)
find_file(TESSERA_HB_VERSION_HEADER hb-version.h PATHS ${harfbuzz_include_dirs} NO_DEFAULT_PATH)
if(NOT TESSERA_HB_VERSION_HEADER)
    message(FATAL_ERROR "Fonts could not find hb-version.h in '${harfbuzz_include_dirs}'.")
endif()
file(STRINGS "${TESSERA_HB_VERSION_HEADER}" harfbuzz_version REGEX "^#define HB_VERSION_STRING ")
string(REGEX REPLACE "^#define HB_VERSION_STRING \"([^\"]*)\".*" "\\1" harfbuzz_version "${harfbuzz_version}")
# Shaping fixtures are exact; another HarfBuzz version needs its own adoption record.
if(NOT harfbuzz_version STREQUAL "10.2.0")
    message(FATAL_ERROR "Expected HarfBuzz 10.2.0, got '${harfbuzz_version}' from ${TESSERA_HB_VERSION_HEADER}")
endif()

add_library(tessera_fonts font_shaper.cpp font_profile.cpp font_profile_serialization.cpp replay_fonts.cpp)
add_library(tessera::fonts ALIAS tessera_fonts)
target_include_directories(tessera_fonts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tessera_fonts PUBLIC tessera::core PRIVATE harfbuzz::harfbuzz Freetype::Freetype)
if(MSVC)
    target_compile_options(tessera_fonts PRIVATE /W4 /permissive-)
else()
    target_compile_options(tessera_fonts PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(TARGET tessera_vulkan_menu)
    target_link_libraries(tessera_vulkan_menu PRIVATE tessera::fonts)
    target_compile_definitions(tessera_vulkan_menu PRIVATE TESSERA_MENU_FONTS=1)
    if(BUILD_TESTING AND TESSERA_BUILD_TESTING)
        add_test(NAME vulkan_menu_fonts_smoke COMMAND tessera_vulkan_menu --smoke --fonts
            "${PROJECT_SOURCE_DIR}/tests/fixtures/fonts/NotoSans-Regular.ttf"
            "${PROJECT_SOURCE_DIR}/tests/fixtures/fonts/NotoSansJP-Regular.otf")
        # Native smokes own foreground focus/capture; they must not overlap each other.
        set_tests_properties(vulkan_menu_fonts_smoke PROPERTIES RESOURCE_LOCK native_menu)
    endif()
endif()
if(BUILD_TESTING AND TESSERA_BUILD_TESTING)
    if(TARGET tessera_vulkan_tests)
        target_link_libraries(tessera_vulkan_tests PRIVATE tessera::fonts)
        target_compile_definitions(tessera_vulkan_tests PRIVATE
            TESSERA_REAL_GLYPH_FIXTURES=1
            TESSERA_FONT_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/fonts")
    endif()
    add_executable(tessera_font_shaper_tests ../../tests/text/font_shaper_tests.cpp)
    target_link_libraries(tessera_font_shaper_tests PRIVATE tessera::fonts)
    target_compile_definitions(tessera_font_shaper_tests PRIVATE
        TESSERA_FONT_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/fonts")
    if(MSVC)
        target_compile_options(tessera_font_shaper_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME font_shaper COMMAND tessera_font_shaper_tests)
    add_executable(tessera_font_profile_tests ../../tests/text/font_profile_tests.cpp)
    target_link_libraries(tessera_font_profile_tests PRIVATE tessera::fonts)
    target_compile_definitions(tessera_font_profile_tests PRIVATE
        TESSERA_FONT_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/fonts")
    if(MSVC)
        target_compile_options(tessera_font_profile_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME font_profile COMMAND tessera_font_profile_tests)
    add_executable(tessera_glyph_raster_tests ../../tests/text/glyph_raster_tests.cpp)
    target_link_libraries(tessera_glyph_raster_tests PRIVATE tessera::fonts)
    target_compile_definitions(tessera_glyph_raster_tests PRIVATE
        TESSERA_FONT_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/fonts"
        TESSERA_GLYPH_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/glyphs"
        TESSERA_GLYPH_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    if(MSVC)
        target_compile_options(tessera_glyph_raster_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME glyph_raster COMMAND tessera_glyph_raster_tests)
    # The fixed per-face raster policy must override this process default.
    set_tests_properties(glyph_raster PROPERTIES ENVIRONMENT "FREETYPE_PROPERTIES=cff:no-stem-darkening=0")
endif()
