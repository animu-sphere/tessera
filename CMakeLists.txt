cmake_minimum_required(VERSION 3.20)
project(Tessera VERSION 0.0.0 LANGUAGES CXX)

add_library(tessera_core
    src/ui/document.cpp
    src/ui/tree.cpp
    src/ui/serialization.cpp
    src/ui/property_metadata.cpp
    src/style/resolved_style.cpp
    src/layout/layout_box.cpp
    src/layout/layout_snapshot.cpp
    src/layout/flex_layout.cpp
    src/text/placeholder_text.cpp
    src/input/event.cpp
    src/input/pointer.cpp
    src/render/draw_list.cpp
    src/render/paint.cpp
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
if(BUILD_TESTING)
    add_executable(tessera_document_tests tests/serialization/document_tests.cpp)
    target_link_libraries(tessera_document_tests PRIVATE tessera::core)
    target_compile_definitions(tessera_document_tests PRIVATE
        TESSERA_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/serialization/fixtures")
    add_test(NAME document COMMAND tessera_document_tests)
    foreach(contract IN ITEMS ui/property_metadata layout/layout_contract layout/layout text/text input/event input/pointer render/draw_list render/paint)
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
