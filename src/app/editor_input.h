#pragma once

// Teclado, mando y mouse del editor de niveles (con raylib). Lo usan el juego (CONSTRUCCION) y level_editor.exe.
//
//   Mover el cursor ........ flechas / WASD / cruceta
//   Poner ................... Espacio, Z, F / clic izquierdo (mantener y arrastrar pinta) / A
//   Borrar .................. X, Supr / clic derecho / B o X del mando
//   Bloque anterior/siguiente Q y E, RePag y AvPag / rueda del mouse / LB y RB
//   Herramienta ............. Tab / clic en la barra / Y
//   Menu .................... Esc / Start

#include <raylib.h>

#include <algorithm>

#include "editor.h"

namespace app {

// destX/destY/scale: donde se dibuja la pantalla del juego dentro de la ventana (para llevar el mouse a sus pixeles).
inline EditorKeys GatherEditorKeys(float destX, float destY, float scale) {
    EditorKeys k;
    bool padLeft = false, padRight = false, padUp = false, padDown = false;
    bool padA = false, padAHeld = false, padB = false, padBHeld = false, padX = false, padY = false, padLB = false, padRB = false, padStart = false;
    for (int g = 0; g < 4; ++g) {
        if (!IsGamepadAvailable(g)) continue;
        const float ax = GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_Y);
        padLeft = padLeft || IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || ax < -0.5f;
        padRight = padRight || IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax > 0.5f;
        padUp = padUp || IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_UP) || ay < -0.5f;
        padDown = padDown || IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || ay > 0.5f;
        padA = padA || IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        padAHeld = padAHeld || IsGamepadButtonDown(g, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        padB = padB || IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        padBHeld = padBHeld || IsGamepadButtonDown(g, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        padX = padX || IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
        padY = padY || IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        padLB = padLB || IsGamepadButtonPressed(g, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
        padRB = padRB || IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
        padStart = padStart || IsGamepadButtonPressed(g, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    }
    k.left = IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A) || padLeft;
    k.right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D) || padRight;
    k.up = IsKeyDown(KEY_UP) || IsKeyDown(KEY_W) || padUp;
    k.down = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || padDown;
    const bool mouseLeft = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), mouseRight = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    k.place = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_F) || padA || mouseLeft;
    k.placeHeld = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_Z) || IsKeyDown(KEY_F) || padAHeld || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    k.erase = IsKeyPressed(KEY_X) || IsKeyPressed(KEY_DELETE) || padB || padX || mouseRight;
    k.eraseHeld = IsKeyDown(KEY_X) || IsKeyDown(KEY_DELETE) || padBHeld || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    k.prevItem = IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_PAGE_UP) || padLB;
    k.nextItem = IsKeyPressed(KEY_E) || IsKeyPressed(KEY_PAGE_DOWN) || padRB;
    k.tool = IsKeyPressed(KEY_TAB) || padY || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
    k.menu = IsKeyPressed(KEY_ESCAPE) || padStart;
    k.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || padB;
    k.accept = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE) || padA;
    const Vector2 m = GetMousePosition();
    const float cx = (m.x - destX) / scale, cy = (m.y - destY) / scale;
    if (scale > 0 && cx >= 0 && cy >= 0 && cx < kScreenW && cy < kScreenH) {
        k.mx = static_cast<int>(cx);
        k.my = static_cast<int>(cy);
    }
    k.wheel = static_cast<int>(GetMouseWheelMove());
    return k;
}

}  // namespace app
