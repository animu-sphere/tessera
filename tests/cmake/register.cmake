# Each CTest entry configures a separate host project. Its enabled host tests
# exercise embedded defaults, avoiding recursion into Tessera's own checks.
configure_file("${CMAKE_CURRENT_LIST_DIR}/consumer-settings.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake" @ONLY)
foreach(mode IN ITEMS add_subdirectory FetchContent)
    add_test(NAME source_consumer_${mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
            "-DMODE=${mode}" "-DCONFIG=$<CONFIG>" -DWITH_VULKAN=OFF
            -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
    set_tests_properties(source_consumer_${mode} PROPERTIES TIMEOUT 600)
    if(TESSERA_BUILD_VULKAN)
        add_test(NAME source_consumer_vulkan_${mode}
            COMMAND "${CMAKE_COMMAND}"
                "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer-settings.cmake"
                "-DMODE=${mode}" "-DCONFIG=$<CONFIG>" -DWITH_VULKAN=ON
                -P "${CMAKE_CURRENT_LIST_DIR}/run-consumer.cmake")
        set_tests_properties(source_consumer_vulkan_${mode} PROPERTIES TIMEOUT 600)
    endif()
endforeach()
