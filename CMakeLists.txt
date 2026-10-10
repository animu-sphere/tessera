cmake_minimum_required(VERSION 3.20)
# VERSION is the single release version source; cmake/release.cmake checks tags against it.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION TESSERA_VERSION LIMIT_COUNT 1)
if(NOT TESSERA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain one MAJOR.MINOR.PATCH value, found '${TESSERA_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)
project(Tessera VERSION ${TESSERA_VERSION} LANGUAGES CXX)

add_library(tessera_core
    src/ui/document.cpp
    src/ui/tree.cpp
    src/ui/serialization.cpp
    src/ui/property_metadata.cpp
    src/style/resolved_style.cpp
    src/style/style_sheet.cpp
    src/layout/layout_box.cpp
    src/layout/layout_snapshot.cpp
    src/layout/flex_layout.cpp
    src/text/placeholder_text.cpp
    src/text/glyph_cache.cpp
    src/input/event.cpp
    src/input/pointer.cpp
    src/input/focus.cpp
    src/input/scroll.cpp
    src/render/draw_list.cpp
    src/render/paint.cpp
    src/render/glyph_atlas.cpp
    src/replay/replay.cpp
    src/replay/replay_serialization.cpp
    src/detail/sha256.cpp
    src/semantics/semantics.cpp
    src/inspection/inspection.cpp
)
add_library(tessera::core ALIAS tessera_core)
target_include_directories(tessera_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(tessera_core PUBLIC cxx_std_20)
set_target_properties(tessera_core PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(tessera_core PUBLIC /utf-8 PRIVATE /W4 /permissive-)
else()
    target_compile_options(tessera_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(TESSERA_BUILD_EXAMPLES "Build the core-only examples" ON)
if(TESSERA_BUILD_EXAMPLES)
    add_executable(tessera_hello_ui examples/hello-ui/main.cpp)
    target_link_libraries(tessera_hello_ui PRIVATE tessera::core)
    add_executable(tessera_flex_layout examples/flex-layout/main.cpp)
    target_link_libraries(tessera_flex_layout PRIVATE tessera::core)
    add_executable(tessera_pointer_menu examples/pointer-menu/main.cpp)
    target_link_libraries(tessera_pointer_menu PRIVATE tessera::core)
endif()

include(CTest)
option(TESSERA_BUILD_VULKAN "Build the optional Vulkan renderer and GPU fixtures" OFF)
if(TESSERA_BUILD_VULKAN)
    add_subdirectory(backends/vulkan)
endif()
option(TESSERA_BUILD_FONTS "Build the optional HarfBuzz/FreeType text services and font fixtures" OFF)
if(TESSERA_BUILD_FONTS)
    add_subdirectory(modules/fonts)
endif()
if(BUILD_TESTING)
    add_executable(tessera_document_tests tests/serialization/document_tests.cpp)
    target_link_libraries(tessera_document_tests PRIVATE tessera::core)
    target_compile_definitions(tessera_document_tests PRIVATE
        TESSERA_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/serialization/fixtures")
    add_test(NAME document COMMAND tessera_document_tests)
    foreach(contract IN ITEMS ui/property_metadata style/style_sheet layout/layout_contract layout/layout text/text text/glyph_cache input/event input/pointer input/focus input/scroll render/draw_list render/paint render/glyph_atlas replay/replay replay/replay_serialization semantics/semantics inspection/inspection)
        get_filename_component(name ${contract} NAME)
        add_executable(tessera_${name}_tests tests/${contract}_tests.cpp)
        target_link_libraries(tessera_${name}_tests PRIVATE tessera::core)
        add_test(NAME ${name} COMMAND tessera_${name}_tests)
    endforeach()
    if(TESSERA_BUILD_EXAMPLES)
        add_test(NAME hello_ui COMMAND tessera_hello_ui)
        add_test(NAME flex_layout COMMAND tessera_flex_layout)
        add_test(NAME pointer_menu COMMAND tessera_pointer_menu)
    endif()
endif()
