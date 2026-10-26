# Each CTest entry configures a separate host project. Its enabled host tests
# exercise embedded defaults, avoiding recursion into Tessera's own checks.
configure_file("${CMAKE_CURRENT_LIST_DIR}/consumer-settings.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake" @ONLY)
foreach(mode IN ITEMS add_subdirectory FetchContent)
    add_test(NAME renderer_diagnostics_consumer_${mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
            "-DMODE=${mode}" "-DCONFIG=$<CONFIG>" -DRENDERER_DIAGNOSTICS=ON -DWITH_VULKAN=OFF
            -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
    set_tests_properties(renderer_diagnostics_consumer_${mode} PROPERTIES TIMEOUT 600 LABELS import)
    add_test(NAME settings_starter_consumer_${mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
            "-DMODE=${mode}" "-DCONFIG=$<CONFIG>" -DSETTINGS_STARTER=ON -DWITH_VULKAN=OFF
            -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
    set_tests_properties(settings_starter_consumer_${mode} PROPERTIES TIMEOUT 600 LABELS import)
    add_test(NAME first_app_consumer_${mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
            "-DMODE=${mode}" "-DCONFIG=$<CONFIG>" -DFIRST_APP=ON -DWITH_VULKAN=OFF
            -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
    set_tests_properties(first_app_consumer_${mode} PROPERTIES TIMEOUT 600 LABELS import)
    add_test(NAME source_consumer_${mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
            "-DMODE=${mode}" "-DCONFIG=$<CONFIG>" -DWITH_VULKAN=OFF
            -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
    set_tests_properties(source_consumer_${mode} PROPERTIES TIMEOUT 600 LABELS import)
    if(TESSERA_BUILD_VULKAN)
        add_test(NAME source_consumer_vulkan_${mode}
            COMMAND "${CMAKE_COMMAND}"
                "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
                "-DMODE=${mode}" "-DCONFIG=$<CONFIG>" -DWITH_VULKAN=ON
                -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
        set_tests_properties(source_consumer_vulkan_${mode} PROPERTIES TIMEOUT 600 LABELS import)
    endif()
endforeach()
