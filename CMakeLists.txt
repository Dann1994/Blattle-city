cmake_minimum_required(VERSION 3.20)
project(BattleCityNES LANGUAGES CXX)
if (WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if (ANDROID)
    add_compile_options(-fsigned-char)      # el mismo char con signo que en la PC: la simulacion tiene que dar lo mismo en todos lados
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
# raylib 5.5 viene incluido en third_party/raylib (el proyecto no necesita red ni carpetas externas)
if (EXISTS ${CMAKE_SOURCE_DIR}/third_party/raylib/CMakeLists.txt)
    set(FETCHCONTENT_SOURCE_DIR_RAYLIB ${CMAKE_SOURCE_DIR}/third_party/raylib)
endif()
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# Port de la logica del juego original (sin dependencias de ventana/audio).
add_library(nes_core STATIC src/nes/battle.cpp src/nes/sound_driver.cpp)
target_include_directories(nes_core PUBLIC ${CMAKE_SOURCE_DIR}/src/nes)

# Renderer por software (fondo + sprites desde el CHR original).
add_library(nes_render STATIC src/app/editor.cpp src/app/renderer.cpp src/app/levels.cpp src/app/flow.cpp src/app/lobby.cpp src/app/config.cpp)
target_link_libraries(nes_render PUBLIC nes_core)

# Emulacion de la APU (audio de la NES), sin dependencias de ventana.
add_library(nes_audio STATIC src/app/apu.cpp)

# Modo LAN (sockets UDP; en Windows usa Winsock)
add_library(nes_lan STATIC src/app/lan.cpp src/app/nat.cpp src/app/voice.cpp)
if (ANDROID)
    target_sources(nes_lan PRIVATE src/app/mic_android.cpp)
    target_include_directories(nes_lan PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
else()
    target_sources(nes_lan PRIVATE src/app/mic_win.cpp)
endif()
if (WIN32)
    target_link_libraries(nes_lan PUBLIC ws2_32 iphlpapi winmm)
endif()

# Buscador de actualizaciones (releases de GitHub): WinINet en Windows, JNI en Android
if (ANDROID)
    add_library(nes_update STATIC src/app/updater.cpp src/app/updater_android.cpp)
    target_include_directories(nes_update PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
else()
    add_library(nes_update STATIC src/app/updater.cpp)
endif()
if (WIN32)
    target_link_libraries(nes_update PUBLIC wininet shell32)
endif()

if (ANDROID)
    # APK: la libreria nativa "main" (la carga NativeActivity). Se empaqueta con android/build_apk.ps1
    add_library(main SHARED src/app/main.cpp src/app/samples.cpp)
    target_link_libraries(main PRIVATE nes_render nes_audio nes_lan nes_update raylib log android)
    target_include_directories(main PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
    target_link_options(main PRIVATE -u ANativeActivity_onCreate)
else()
set(BCNES_SOURCES src/app/main.cpp src/app/samples.cpp)
if (WIN32)
    # icono del ejecutable (el tanque amarillo)
    file(WRITE ${CMAKE_BINARY_DIR}/app_icon.rc "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/assets/icon.ico\"\n")
    list(APPEND BCNES_SOURCES ${CMAKE_BINARY_DIR}/app_icon.rc)
endif()
add_executable(battlecitynes ${BCNES_SOURCES})
target_link_libraries(battlecitynes PRIVATE nes_render nes_audio nes_lan nes_update raylib)
target_compile_definitions(battlecitynes PRIVATE BCNES_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/")
if (WIN32)
    target_compile_definitions(battlecitynes PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_options(battlecitynes PRIVATE -mwindows)      # sin ventana de consola
endif()
if (MINGW)
    target_link_options(battlecitynes PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# Pruebas de la logica portada (sin ventana).
# Editor de niveles aparte (agrega niveles al modo principal)
add_executable(level_editor src/app/editor_main.cpp)
target_link_libraries(level_editor PRIVATE nes_render raylib)
target_compile_definitions(level_editor PRIVATE BCNES_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/")
if (WIN32)
    target_compile_definitions(level_editor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_options(level_editor PRIVATE -mwindows)
endif()
if (MINGW)
    target_link_options(level_editor PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

add_executable(nes_tests tests/nes_tests.cpp)
target_link_libraries(nes_tests PRIVATE nes_render nes_lan nes_update)
if (MINGW)
    target_link_options(nes_tests PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# Prueba diferencial contra la ROM real (interprete 6502). Necesita la ROM
# (variable BCNES_ROM); sin ella se saltea.
add_executable(rom_oracle_tests tests/rom_oracle_tests.cpp)
target_link_libraries(rom_oracle_tests PRIVATE nes_core)
if (MINGW)
    target_link_options(rom_oracle_tests PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
endif()
