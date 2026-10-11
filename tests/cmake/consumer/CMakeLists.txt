cmake_minimum_required(VERSION 3.20)
project(TesseraSourceConsumer LANGUAGES CXX)

# A host's tests must stay enabled without pulling in Tessera's development
# targets. Do not set Tessera's test/example options: check embedded defaults.
include(CTest)
if(NOT BUILD_TESTING)
    message(FATAL_ERROR "This fixture requires the host's BUILD_TESTING=ON.")
endif()
set(TESSERA_SOURCE_DIR "" CACHE PATH "Existing Tessera checkout (no network acquisition)")
set(TESSERA_IMPORT_MODE "add_subdirectory" CACHE STRING "add_subdirectory or FetchContent")
option(CONSUMER_WITH_VULKAN "Link the optional Vulkan backend" OFF)
set(TESSERA_BUILD_VULKAN ${CONSUMER_WITH_VULKAN} CACHE BOOL "" FORCE)
set(TESSERA_BUILD_FONTS OFF CACHE BOOL "" FORCE)
if(NOT EXISTS "${TESSERA_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set TESSERA_SOURCE_DIR to an existing Tessera checkout.")
endif()

if(TESSERA_IMPORT_MODE STREQUAL "add_subdirectory")
    add_subdirectory("${TESSERA_SOURCE_DIR}" tessera)
elseif(TESSERA_IMPORT_MODE STREQUAL "FetchContent")
    include(FetchContent)
    FetchContent_Declare(tessera SOURCE_DIR "${TESSERA_SOURCE_DIR}")
    FetchContent_MakeAvailable(tessera)
else()
    message(FATAL_ERROR "Use add_subdirectory or FetchContent for TESSERA_IMPORT_MODE.")
endif()

if(NOT BUILD_TESTING OR NOT CMAKE_PROJECT_NAME STREQUAL "TesseraSourceConsumer")
    message(FATAL_ERROR "Tessera changed the host's testing or project ownership.")
endif()
if(TESSERA_BUILD_TESTING OR TESSERA_BUILD_EXAMPLES OR
   TARGET tessera_document_tests OR TARGET tessera_hello_ui OR TARGET tessera_vulkan_tests OR
   TARGET tessera_vulkan_menu OR TARGET tessera_fonts)
    message(FATAL_ERROR "Embedded defaults must exclude Tessera tests/examples and fonts.")
endif()
if(NOT CONSUMER_WITH_VULKAN AND (TARGET tessera::vulkan OR TARGET Vulkan::Vulkan OR TESSERA_SLANGC))
    message(FATAL_ERROR "Core-only consumption must perform no Vulkan/Slang discovery.")
endif()

add_executable(source_consumer main.cpp)
# Deliberately no manual include directories, compiler features or dependencies.
# The public alias must carry every compile and link requirement.
target_link_libraries(source_consumer PRIVATE tessera::core)
add_test(NAME source_consumer COMMAND source_consumer)
if(CONSUMER_WITH_VULKAN)
    add_executable(vulkan_consumer vulkan.cpp)
    target_link_libraries(vulkan_consumer PRIVATE tessera::vulkan)
    add_test(NAME vulkan_consumer COMMAND vulkan_consumer)
endif()
