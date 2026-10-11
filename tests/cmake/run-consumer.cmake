cmake_minimum_required(VERSION 3.20)
include("${SETTINGS}")
if(NOT MODE STREQUAL "add_subdirectory" AND NOT MODE STREQUAL "FetchContent")
    message(FATAL_ERROR "Supply a supported consumer import mode.")
endif()
set(consumer_binary "${consumer_root}/${MODE}")
if(WITH_VULKAN)
    string(APPEND consumer_binary "-vulkan")
endif()
# Separate configurations also keep logs intact after Debug/Release verification.
string(APPEND consumer_binary "/${CONFIG}")
file(MAKE_DIRECTORY "${consumer_binary}")
set(configure_args
    -S "${tessera_source}/tests/cmake/consumer" -B "${consumer_binary}"
    -G "${consumer_generator}"
    "-DTESSERA_SOURCE_DIR:PATH=${tessera_source}" "-DTESSERA_IMPORT_MODE:STRING=${MODE}"
    "-DCONSUMER_WITH_VULKAN:BOOL=${WITH_VULKAN}" -DBUILD_SHARED_LIBS:BOOL=OFF
    -DBUILD_TESTING:BOOL=ON)
if(NOT consumer_configurations)
    list(APPEND configure_args "-DCMAKE_BUILD_TYPE:STRING=${CONFIG}")
endif()
if(consumer_platform)
    list(APPEND configure_args -A "${consumer_platform}")
endif()
if(consumer_toolset)
    list(APPEND configure_args -T "${consumer_toolset}")
endif()
if(consumer_instance)
    list(APPEND configure_args "-DCMAKE_GENERATOR_INSTANCE:INTERNAL=${consumer_instance}")
endif()
if(consumer_toolchain)
    list(APPEND configure_args "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${consumer_toolchain}")
endif()
if(NOT consumer_generator MATCHES "^Visual Studio" AND NOT consumer_generator STREQUAL "Xcode")
    list(APPEND configure_args "-DCMAKE_CXX_COMPILER:FILEPATH=${consumer_cxx}"
        "-DCMAKE_MAKE_PROGRAM:FILEPATH=${consumer_make_program}")
endif()
if(WITH_VULKAN)
    list(APPEND configure_args "-DVulkan_INCLUDE_DIR:PATH=${consumer_vulkan_include}"
        "-DVulkan_LIBRARY:FILEPATH=${consumer_vulkan_library}"
        "-DTESSERA_SLANGC:FILEPATH=${consumer_slangc}")
endif()

function(consumer_step phase)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
        OUTPUT_FILE "${consumer_binary}/${phase}.log"
        ERROR_FILE "${consumer_binary}/${phase}.log" TIMEOUT 480)
    if(NOT result EQUAL 0)
        file(READ "${consumer_binary}/${phase}.log" output)
        message(FATAL_ERROR "Consumer ${phase} failed (${result}).\n${output}")
    endif()
endfunction()
consumer_step(configure "${CMAKE_COMMAND}" ${configure_args})
set(build_args --build "${consumer_binary}" --config "${CONFIG}" --parallel 2)
if(consumer_generator MATCHES "^Visual Studio")
    # Reusable MSBuild workers must not outlive the nested build invocation.
    list(APPEND build_args -- /nodeReuse:false)
endif()
consumer_step(build "${CMAKE_COMMAND}" ${build_args})
consumer_step(test "${consumer_ctest}" --test-dir "${consumer_binary}" -C "${CONFIG}" --output-on-failure --no-tests=error)
message(STATUS "${MODE} consumer passed (Vulkan=${WITH_VULKAN}, ${CONFIG}); logs: ${consumer_binary}")
