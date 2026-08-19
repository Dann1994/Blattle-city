cmake_minimum_required(VERSION 3.20)
project(BattleCityClone LANGUAGES CXX)

if (WIN32)
    enable_language(RC)
endif()

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

if (WIN32)
    list(APPEND BC_SOURCES ${CMAKE_SOURCE_DIR}/assets/icon/app.rc)
endif()

add_executable(battlecity ${BC_SOURCES})

target_include_directories(battlecity PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/ai
    ${CMAKE_SOURCE_DIR}/src/modes
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/render
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(battlecity PRIVATE raylib nlohmann_json::nlohmann_json)

target_compile_definitions(battlecity PRIVATE
    BC_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/"
)

if (WIN32)
    target_compile_definitions(battlecity PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Editor de niveles (Fase 6, seccion 12.7 del documento de diseno): herramienta
# interna de desarrollo, ejecutable APARTE de "battlecity" (no se empaqueta con
# el juego). Solo usa /shared (formato de nivel) y los structs header-only de
# terreno de src/world (BrickUnit/SteelUnit/TileTypes) para verse igual que el
# juego real; no depende de nada de /src/core, /entities, /ai, etc.
add_executable(level_editor
    tools/level_editor/main.cpp
    shared/LevelFormat.cpp
)

target_include_directories(level_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/src/world
)

target_link_libraries(level_editor PRIVATE raylib nlohmann_json::nlohmann_json)

target_compile_definitions(level_editor PRIVATE
    BC_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/"
)

if (WIN32)
    target_compile_definitions(level_editor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
