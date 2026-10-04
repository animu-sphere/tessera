cmake_minimum_required(VERSION 3.20)
project(Tessera VERSION 0.0.0 LANGUAGES CXX)

add_library(tessera_core
    src/ui/document.cpp
    src/ui/tree.cpp
    src/ui/serialization.cpp
    src/style/resolved_style.cpp
    src/layout/layout_box.cpp
    src/text/placeholder_text.cpp
    src/input/event.cpp
    src/render/draw_list.cpp
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

option(TESSERA_BUILD_EXAMPLES "Build the core-only document example" ON)
if(TESSERA_BUILD_EXAMPLES)
    add_executable(tessera_hello_ui examples/hello-ui/main.cpp)
    target_link_libraries(tessera_hello_ui PRIVATE tessera::core)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(tessera_document_tests tests/serialization/document_tests.cpp)
    target_link_libraries(tessera_document_tests PRIVATE tessera::core)
    target_compile_definitions(tessera_document_tests PRIVATE
        TESSERA_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/serialization/fixtures")
    add_test(NAME document COMMAND tessera_document_tests)
    foreach(contract IN ITEMS layout/layout_contract text/text input/event render/draw_list)
        get_filename_component(name ${contract} NAME)
        add_executable(tessera_${name}_tests tests/${contract}_tests.cpp)
        target_link_libraries(tessera_${name}_tests PRIVATE tessera::core)
        add_test(NAME ${name} COMMAND tessera_${name}_tests)
    endforeach()
    if(TESSERA_BUILD_EXAMPLES)
        add_test(NAME hello_ui COMMAND tessera_hello_ui)
    endif()
endif()
