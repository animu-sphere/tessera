# Owner check for the build-time manifest step (script mode): each fixture here compiles, and its
# reflected Parameters struct must fail manifest generation with the expected message.
#   cmake -DSLANGC=<slangc> -DABI=<backends/vulkan/shaders> -DSCRIPT=<shader_parameters.cmake>
#         -DWORK=<scratch directory> -P check.cmake
set(cases "plain|Parameters.alpha has type float32" "array|Parameters.colors has type array"
    "wide|Parameters declares 17 fields")
file(MAKE_DIRECTORY "${WORK}")
foreach(entry IN LISTS cases)
    string(FIND "${entry}" "|" split)
    string(SUBSTRING "${entry}" 0 ${split} name)
    math(EXPR start "${split} + 1")
    string(SUBSTRING "${entry}" ${start} -1 expected)
    execute_process(COMMAND "${SLANGC}" "${CMAKE_CURRENT_LIST_DIR}/${name}.slang" -I "${ABI}"
            -target spirv -profile spirv_1_3 -entry proceduralMain -stage fragment -fvk-use-entrypoint-name
            -o "${WORK}/${name}.spv" -reflection-json "${WORK}/${name}.reflection.json"
        RESULT_VARIABLE compiled ERROR_VARIABLE compile_output OUTPUT_VARIABLE compile_output)
    if(NOT compiled EQUAL 0)
        message(FATAL_ERROR "${name}.slang must compile: ${compile_output}")
    endif()
    file(REMOVE "${WORK}/${name}.parameters.json")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DREFLECTION=${WORK}/${name}.reflection.json"
            "-DMANIFEST=${WORK}/${name}.parameters.json" -DPROFILE=fill-v1 "-DSOURCE=${name}.slang" -P "${SCRIPT}"
        RESULT_VARIABLE generated ERROR_VARIABLE output OUTPUT_VARIABLE output)
    string(REGEX REPLACE "[ \t\r\n]+" " " output "${output}")
    if(generated EQUAL 0 OR EXISTS "${WORK}/${name}.parameters.json")
        message(FATAL_ERROR "${name}.slang must not produce a parameter manifest.")
    endif()
    string(FIND "${output}" "${name}.slang: ${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${name}.slang reported an unexpected failure: ${output}")
    endif()
endforeach()
message(STATUS "Shader parameter rejection checks passed")
