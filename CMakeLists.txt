cmake_minimum_required(VERSION 3.20)
# VERSION is the single release version source; cmake/release.cmake checks tags against it.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION TESSERA_VERSION LIMIT_COUNT 1)
if(NOT TESSERA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain one MAJOR.MINOR.PATCH value, found '${TESSERA_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)
project(Tessera VERSION ${TESSERA_VERSION} LANGUAGES CXX)

# Embedded consumers opt into Tessera's development targets independently of
# their own BUILD_TESTING setting. Standalone workflows retain their defaults.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(tessera_development_defaults ON)
    include(CTest)
else()
    set(tessera_development_defaults OFF)
endif()
option(TESSERA_BUILD_TESTING "Build Tessera checks when BUILD_TESTING is enabled" ${tessera_development_defaults})

add_library(tessera_core
    src/ui/document.cpp
    src/ui/tree.cpp
    src/ui/serialization.cpp
    src/ui/source_map.cpp
    src/ui/property_metadata.cpp
    src/ui/keyed.cpp
    src/ui/bindings.cpp
    src/ui/authoring.cpp
    src/ui/component.cpp
    src/ui/component_mount.cpp
    src/ui/component_tree_mount.cpp
    src/ui/async.cpp
    src/style/resolved_style.cpp
    src/style/style_sheet.cpp
    src/style/theme.cpp
    src/layout/layout_box.cpp
    src/layout/layout_snapshot.cpp
    src/layout/flex_layout.cpp
    src/text/placeholder_text.cpp
    src/text/glyph_cache.cpp
    src/input/event.cpp
    src/input/text_edit.cpp
    src/input/pointer.cpp
    src/input/focus.cpp
    src/input/scroll.cpp
    src/commands/commands.cpp
    src/commands/execution.cpp
    src/commands/transactions.cpp
    src/commands/gestures.cpp
    src/commands/shortcuts.cpp
    src/reactive/runtime.cpp
    src/render/draw_list.cpp
    src/render/paint.cpp
    src/render/glyph_atlas.cpp
    src/render/capture.cpp
    src/render/cpu_renderer.cpp
    src/replay/replay.cpp
    src/replay/replay_serialization.cpp
    src/detail/sha256.cpp
    src/semantics/semantics.cpp
    src/semantics/semantic_snapshot.cpp
    src/inspection/inspection.cpp
    src/inspection/invariants.cpp
    src/inspection/offscreen_runner.cpp
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

option(TESSERA_BUILD_EXAMPLES "Build the core-only examples" ${tessera_development_defaults})
if(TESSERA_BUILD_EXAMPLES)
    add_executable(tessera_first_app examples/first-app/main.cpp)
    target_link_libraries(tessera_first_app PRIVATE tessera::core)
    add_executable(tessera_settings_starter examples/settings-starter/main.cpp)
    target_link_libraries(tessera_settings_starter PRIVATE tessera::core)
    add_executable(tessera_renderer_diagnostics examples/renderer-diagnostics/main.cpp
        examples/renderer-diagnostics/panel.cpp examples/renderer-diagnostics/host.cpp)
    target_link_libraries(tessera_renderer_diagnostics PRIVATE tessera::core)
    add_executable(tessera_hello_ui examples/hello-ui/main.cpp)
    target_link_libraries(tessera_hello_ui PRIVATE tessera::core)
    add_executable(tessera_flex_layout examples/flex-layout/main.cpp)
    target_link_libraries(tessera_flex_layout PRIVATE tessera::core)
    add_executable(tessera_pointer_menu examples/pointer-menu/main.cpp)
    target_link_libraries(tessera_pointer_menu PRIVATE tessera::core)
    add_executable(tessera_tool_panel examples/tool-panel/main.cpp)
    target_link_libraries(tessera_tool_panel PRIVATE tessera::core)
    add_executable(tessera_editor_prototype examples/editor-prototype/main.cpp)
    target_link_libraries(tessera_editor_prototype PRIVATE tessera::core)
    target_compile_definitions(tessera_editor_prototype PRIVATE
        TESSERA_EDITOR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/examples/editor-prototype/preview.json")
    add_executable(tessera_settings examples/settings/main.cpp)
    target_link_libraries(tessera_settings PRIVATE tessera::core)
endif()

option(TESSERA_BUILD_VULKAN "Build the optional Vulkan renderer and GPU fixtures" OFF)
if(TESSERA_BUILD_VULKAN)
    add_subdirectory(backends/vulkan)
endif()
option(TESSERA_BUILD_FONTS "Build the optional HarfBuzz/FreeType text services and font fixtures" OFF)
if(TESSERA_BUILD_FONTS)
    add_subdirectory(modules/fonts)
endif()
if(BUILD_TESTING AND TESSERA_BUILD_TESTING)
    add_executable(tessera_authoring_tests tests/ui/authoring_tests.cpp)
    target_link_libraries(tessera_authoring_tests PRIVATE tessera::core)
    add_test(NAME authoring COMMAND tessera_authoring_tests)
    set_tests_properties(authoring PROPERTIES LABELS contract)
    add_executable(tessera_document_tests tests/serialization/document_tests.cpp)
    target_link_libraries(tessera_document_tests PRIVATE tessera::core)
    target_compile_definitions(tessera_document_tests PRIVATE
        TESSERA_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/serialization/fixtures")
    add_test(NAME document COMMAND tessera_document_tests)
    set_tests_properties(document PROPERTIES LABELS contract)
    foreach(contract IN ITEMS reactive/reactive ui/keyed ui/bindings ui/component ui/component_mount ui/component_tree_mount ui/component_cleanup ui/component_effect ui/component_async ui/component_event ui/component_style ui/component_invocation ui/component_source ui/source_diagnostic ui/async ui/async_allocation ui/property_metadata style/style_sheet style/theme layout/layout_contract layout/layout text/text text/glyph_cache input/event input/text_edit input/pointer input/focus input/scroll commands/commands commands/transactions commands/gestures commands/shortcuts render/draw_list render/paint render/glyph_atlas render/cpu_renderer replay/replay replay/replay_serialization replay/replay_host semantics/semantics semantics/semantic_snapshot inspection/inspection inspection/invariants inspection/offscreen_runner)
        get_filename_component(name ${contract} NAME)
        add_executable(tessera_${name}_tests tests/${contract}_tests.cpp)
        target_link_libraries(tessera_${name}_tests PRIVATE tessera::core)
        add_test(NAME ${name} COMMAND tessera_${name}_tests)
        set_tests_properties(${name} PROPERTIES LABELS contract)
    endforeach()
    set_tests_properties(async_allocation PROPERTIES TIMEOUT 30)
    if(TESSERA_BUILD_EXAMPLES)
        add_test(NAME first_app COMMAND tessera_first_app)
        add_test(NAME settings_starter COMMAND tessera_settings_starter)
        add_test(NAME renderer_diagnostics COMMAND tessera_renderer_diagnostics)
        add_executable(tessera_renderer_diagnostics_tests tests/integrations/renderer_diagnostics_tests.cpp
            examples/renderer-diagnostics/panel.cpp examples/renderer-diagnostics/host.cpp)
        target_link_libraries(tessera_renderer_diagnostics_tests PRIVATE tessera::core)
        add_test(NAME renderer_diagnostics_contract COMMAND tessera_renderer_diagnostics_tests)
        add_test(NAME hello_ui COMMAND tessera_hello_ui)
        add_test(NAME flex_layout COMMAND tessera_flex_layout)
        add_test(NAME pointer_menu COMMAND tessera_pointer_menu)
        add_test(NAME tool_panel COMMAND tessera_tool_panel)
        add_test(NAME editor_prototype COMMAND tessera_editor_prototype)
        add_test(NAME settings COMMAND tessera_settings)
        # Consumer checks retain their complete assertions; labels only select
        # execution tiers. Nested import builds have their own label below.
        set_tests_properties(first_app settings_starter renderer_diagnostics
            renderer_diagnostics_contract hello_ui flex_layout pointer_menu
            tool_panel editor_prototype settings PROPERTIES LABELS smoke)
    endif()
    include(tests/cmake/register.cmake)
endif()
