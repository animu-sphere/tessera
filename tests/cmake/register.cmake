# Each CTest entry configures a separate host project. Its enabled host tests
# exercise embedded defaults, avoiding recursion into Tessera's own checks.
# Automatic checks use add_subdirectory only; FetchContent remains a manual path.
# Nested builds run serially regardless of the CTest worker count.
configure_file("${CMAKE_CURRENT_LIST_DIR}/consumer-settings.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake" @ONLY)
set(import_properties TIMEOUT 600 LABELS import RUN_SERIAL TRUE)
add_test(NAME renderer_diagnostics_consumer_add_subdirectory
    COMMAND "${CMAKE_COMMAND}"
        "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
        -DMODE=add_subdirectory "-DCONFIG=$<CONFIG>" -DRENDERER_DIAGNOSTICS=ON -DWITH_VULKAN=OFF
        -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
set_tests_properties(renderer_diagnostics_consumer_add_subdirectory PROPERTIES ${import_properties})
add_test(NAME settings_starter_consumer_add_subdirectory
    COMMAND "${CMAKE_COMMAND}"
        "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
        -DMODE=add_subdirectory "-DCONFIG=$<CONFIG>" -DSETTINGS_STARTER=ON -DWITH_VULKAN=OFF
        -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
set_tests_properties(settings_starter_consumer_add_subdirectory PROPERTIES ${import_properties})
add_test(NAME first_app_consumer_add_subdirectory
    COMMAND "${CMAKE_COMMAND}"
        "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
        -DMODE=add_subdirectory "-DCONFIG=$<CONFIG>" -DFIRST_APP=ON -DWITH_VULKAN=OFF
        -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
set_tests_properties(first_app_consumer_add_subdirectory PROPERTIES ${import_properties})
add_test(NAME source_consumer_add_subdirectory
    COMMAND "${CMAKE_COMMAND}"
        "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
        -DMODE=add_subdirectory "-DCONFIG=$<CONFIG>" -DWITH_VULKAN=OFF
        -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
set_tests_properties(source_consumer_add_subdirectory PROPERTIES ${import_properties})
if(TESSERA_BUILD_VULKAN)
    add_test(NAME source_consumer_vulkan_add_subdirectory
        COMMAND "${CMAKE_COMMAND}"
            "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
            -DMODE=add_subdirectory "-DCONFIG=$<CONFIG>" -DWITH_VULKAN=ON
            -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
    set_tests_properties(source_consumer_vulkan_add_subdirectory PROPERTIES ${import_properties})
endif()
