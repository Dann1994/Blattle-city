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

# Red LAN (Fase 7, seccion 11 del documento de diseno): UDP con canales
# confiables/no confiables, liviano, pensado para juegos en tiempo real.
set(ENET_STATIC ON CACHE BOOL "" FORCE)
set(ENET_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ENET_INSTALL OFF CACHE BOOL "" FORCE)
set(ENET_INSTALL_HEADERS OFF CACHE BOOL "" FORCE)
set(ENET_INSTALL_LIB OFF CACHE BOOL "" FORCE)
set(ENET_INSTALL_PKGCONFIG OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(raylib json enet)

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

target_link_libraries(battlecity PRIVATE raylib nlohmann_json::nlohmann_json enet)
# ENet usa include_directories() a la vieja usanza en su propio CMakeLists
# (no target_include_directories), asi que no propaga solo via
# target_link_libraries como raylib/json: hay que sumarlo a mano.
target_include_directories(battlecity PRIVATE ${enet_SOURCE_DIR}/include)
if (WIN32)
    # ENet en Windows necesita Winsock + winmm (timeGetTime) directo, aparte
    # de las libs de sistema que ya se linkeaban.
    target_link_libraries(battlecity PRIVATE ws2_32 winmm)
endif()

target_compile_definitions(battlecity PRIVATE
    BC_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/"
)

if (WIN32)
    target_compile_definitions(battlecity PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Enlace estatico del runtime de MinGW: sin esto, el .exe depende en tiempo de
# ejecucion de libgcc_s_seh-1.dll/libstdc++-6.dll/libwinpthread-1.dll, que
# solo estan en el PATH dentro del entorno de MSYS2 (por eso compilaba y
# corria bien desde ahi) pero no en un doble clic normal en Windows, donde
# tira "no se encontro libgcc_s_seh-1.dll". Con esto el .exe queda
# autocontenido.
if (MINGW)
    target_link_options(battlecity PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# Entorno de pruebas de gameplay (pedido explicitamente): mismo codigo fuente
# que "battlecity", ejecutable APARTE para que el juego principal quede
# limpio (kDebugFeaturesEnabled en false ahi, ver Game.cpp). Este define
# BC_DEBUG_BUILD, que prende kDebugFeaturesEnabled (botones/indicadores de
# prueba F1-F12, +/-, etc.) y hace arrancar la partida en el "escenario 0"
# (ResetState arranca currentLevel_ en 0 en vez de 1: al no existir
# level_00.json, LevelFilePath cae sola en el mapa de prueba de siempre,
# levels/test_map.json).
add_executable(battlecity_debug ${BC_SOURCES})

target_include_directories(battlecity_debug PRIVATE
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

target_link_libraries(battlecity_debug PRIVATE raylib nlohmann_json::nlohmann_json enet)
target_include_directories(battlecity_debug PRIVATE ${enet_SOURCE_DIR}/include)
if (WIN32)
    target_link_libraries(battlecity_debug PRIVATE ws2_32 winmm)
endif()

target_compile_definitions(battlecity_debug PRIVATE
    BC_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/"
    BC_DEBUG_BUILD
)

if (WIN32)
    target_compile_definitions(battlecity_debug PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if (MINGW)
    target_link_options(battlecity_debug PRIVATE -static-libgcc -static-libstdc++ -static)
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

# Ver el comentario junto al mismo flag en el target "battlecity": sin esto
# tambien le faltarian las DLL del runtime de MinGW al ejecutarlo con doble
# clic fuera de MSYS2.
if (MINGW)
    target_link_options(level_editor PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
