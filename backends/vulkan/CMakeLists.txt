# No dependency discovery or shader compilation occurs in a core-only configuration.
if(CMAKE_VERSION VERSION_LESS 3.23)
    message(FATAL_ERROR "The optional Vulkan module requires CMake 3.23 for SDK version discovery.")
endif()
find_package(Vulkan 1.3 REQUIRED)
find_program(TESSERA_SLANGC NAMES slangc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT TESSERA_SLANGC)
    message(FATAL_ERROR "Vulkan requires slangc 2026.8; set TESSERA_SLANGC to the installed compiler.")
endif()
execute_process(COMMAND "${TESSERA_SLANGC}" -version
    OUTPUT_VARIABLE slang_version ERROR_VARIABLE slang_error RESULT_VARIABLE slang_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(STRIP "${slang_version}${slang_error}" slang_version)
if(NOT slang_result EQUAL 0 OR NOT slang_version STREQUAL "2026.8")
    message(FATAL_ERROR "Expected slangc 2026.8, got '${slang_version}' ${slang_error}")
endif()
set(shader_outputs)
foreach(stage IN ITEMS vertex batch fragment image)
    set(shader_stage ${stage})
    if(stage STREQUAL "image")
        set(shader_stage fragment)
    elseif(stage STREQUAL "batch")
        set(shader_stage vertex)
    endif()
    set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/primitive.${stage}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${TESSERA_SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/primitive.slang"
            -target spirv -profile spirv_1_3 -entry ${stage}Main -stage ${shader_stage}
            -fvk-use-entrypoint-name
            -o "${output}"
        DEPENDS shaders/primitive.slang VERBATIM)
    list(APPEND shader_outputs "${output}")
endforeach()
add_custom_target(tessera_vulkan_shaders DEPENDS ${shader_outputs})
add_library(tessera_vulkan renderer.cpp)
add_library(tessera::vulkan ALIAS tessera_vulkan)
target_include_directories(tessera_vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tessera_vulkan PUBLIC tessera::core Vulkan::Vulkan)
add_dependencies(tessera_vulkan tessera_vulkan_shaders)
if(MSVC)
    target_compile_options(tessera_vulkan PRIVATE /W4 /permissive-)
else()
    target_compile_options(tessera_vulkan PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
    add_executable(tessera_vulkan_tests ../../tests/render/vulkan_tests.cpp)
    target_link_libraries(tessera_vulkan_tests PRIVATE tessera::vulkan)
    target_compile_definitions(tessera_vulkan_tests PRIVATE
        TESSERA_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
        TESSERA_GPU_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    add_test(NAME vulkan COMMAND tessera_vulkan_tests)
endif()
# Win32 example host: window, OS input, DPI, swapchain and presentation stay outside the backend.
if(WIN32 AND TESSERA_BUILD_EXAMPLES)
    add_executable(tessera_vulkan_menu ../../examples/vulkan-menu/main.cpp)
    target_link_libraries(tessera_vulkan_menu PRIVATE tessera::vulkan xinput)
    target_compile_definitions(tessera_vulkan_menu PRIVATE
        TESSERA_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
        TESSERA_GPU_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    if(MSVC)
        target_compile_options(tessera_vulkan_menu PRIVATE /W4 /permissive-)
    endif()
    if(BUILD_TESTING)
        add_test(NAME vulkan_menu_smoke COMMAND tessera_vulkan_menu --smoke)
    endif()
endif()
