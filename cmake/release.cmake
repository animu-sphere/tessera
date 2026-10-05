# Release metadata check and note extraction; run in script mode:
#   cmake [-DTESSERA_SOURCE_DIR=<repo>] [-DTESSERA_RELEASE_TAG=vX.Y.Z]
#         [-DTESSERA_NOTES_OUTPUT=<file> -DTESSERA_REPOSITORY_URL=<url>] -P cmake/release.cmake
# The source directory defaults to this script's repository and the tag to v<VERSION>.
# The check requires the tag to equal v<VERSION> and CHANGELOG.md to contain a dated
# "## vX.Y.Z — YYYY-MM-DD" section with content.
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED TESSERA_SOURCE_DIR OR TESSERA_SOURCE_DIR STREQUAL "")
    get_filename_component(TESSERA_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
endif()

file(STRINGS ${TESSERA_SOURCE_DIR}/VERSION version LIMIT_COUNT 1)
if(NOT version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain one MAJOR.MINOR.PATCH value, found '${version}'")
endif()
if(NOT DEFINED TESSERA_RELEASE_TAG OR TESSERA_RELEASE_TAG STREQUAL "")
    set(TESSERA_RELEASE_TAG "v${version}")
endif()
if(NOT TESSERA_RELEASE_TAG MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "release tag '${TESSERA_RELEASE_TAG}' must have the form vMAJOR.MINOR.PATCH")
endif()
if(NOT TESSERA_RELEASE_TAG STREQUAL "v${version}")
    message(FATAL_ERROR "release tag ${TESSERA_RELEASE_TAG} does not match VERSION ${version}")
endif()

file(READ ${TESSERA_SOURCE_DIR}/CHANGELOG.md changelog)
string(REPLACE "\r\n" "\n" changelog "${changelog}")
string(REPLACE "." "\\." escaped "${version}")
string(REGEX MATCH "(^|\n)## v${escaped} — [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]\n" heading "${changelog}")
if(heading STREQUAL "")
    message(FATAL_ERROR "CHANGELOG.md has no dated '## v${version} — YYYY-MM-DD' section")
endif()

# The section runs from its heading to the next level-2 heading or the end of the file.
string(FIND "${changelog}" "${heading}" start)
string(LENGTH "${heading}" heading_length)
math(EXPR start "${start} + ${heading_length}")
string(SUBSTRING "${changelog}" ${start} -1 notes)
string(FIND "${notes}" "\n## " next)
if(next GREATER_EQUAL 0)
    string(SUBSTRING "${notes}" 0 ${next} notes)
endif()
string(STRIP "${notes}" notes)
if(notes STREQUAL "")
    message(FATAL_ERROR "CHANGELOG.md section v${version} is empty")
endif()

if(DEFINED TESSERA_NOTES_OUTPUT)
    if(NOT DEFINED TESSERA_REPOSITORY_URL)
        message(FATAL_ERROR "TESSERA_REPOSITORY_URL is required with TESSERA_NOTES_OUTPUT")
    endif()
    # Release pages do not resolve repository-relative links; pin them to the tag.
    set(base "${TESSERA_REPOSITORY_URL}/blob/${TESSERA_RELEASE_TAG}/")
    string(REPLACE "](" "](${base}" notes "${notes}")
    string(REPLACE "](${base}http" "](http" notes "${notes}")
    string(REPLACE "](${base}#" "](#" notes "${notes}")
    file(WRITE ${TESSERA_NOTES_OUTPUT} "${notes}\n")
endif()
message(STATUS "Release metadata agrees: ${TESSERA_RELEASE_TAG}")
