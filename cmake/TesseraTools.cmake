# Development tooling above tessera::core: snapshot bundles and the tessera_snapshot CLI.
# Core never depends on these targets; hosts link tessera::snapshot explicitly.
add_library(tessera_snapshot
    tools/snapshot/src/json.cpp
    tools/snapshot/src/png.cpp
    tools/snapshot/src/snapshot_bundle.cpp
    tools/snapshot/src/snapshot_diff.cpp
    tools/snapshot/src/snapshot_load.cpp
)
add_library(tessera::snapshot ALIAS tessera_snapshot)
target_include_directories(tessera_snapshot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tools/snapshot/include)
target_link_libraries(tessera_snapshot PUBLIC tessera::core)
set_target_properties(tessera_snapshot PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(tessera_snapshot PRIVATE /W4 /permissive-)
else()
    target_compile_options(tessera_snapshot PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(tessera_snapshot_cli tools/snapshot/src/main.cpp)
set_target_properties(tessera_snapshot_cli PROPERTIES OUTPUT_NAME tessera_snapshot)
target_link_libraries(tessera_snapshot_cli PRIVATE tessera::snapshot)
