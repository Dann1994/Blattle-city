cmake_minimum_required(VERSION 3.20)
project(BattleCityClone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(raylib json)

file(GLOB_RECURSE BC_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/shared/*.cpp
)

add_executable(battlecity ${BC_SOURCES})

target_include_directories(battlecity PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(battlecity PRIVATE raylib nlohmann_json::nlohmann_json)

target_compile_definitions(battlecity PRIVATE
    BC_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/"
)

if (WIN32)
    target_compile_definitions(battlecity PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# tools/level_editor se agrega en la Fase 6 (herramienta interna, no forma parte del build de release)
