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
    src/input/scrollbar.cpp
    src/input/split_pane.cpp
    src/input/popup.cpp
    src/input/modal.cpp
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
