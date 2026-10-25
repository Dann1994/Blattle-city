#pragma once

// Teclado, mando y mouse del editor de niveles (con raylib). Lo usan el juego (CONSTRUCCION) y level_editor.exe.
//
//   Mover el cursor ........ el mouse / cruceta del mando
//   Variante del bloque ..... flechas o WASD (entero, mitades y puntas)
//   Barra de bloques ........ llevar el mouse al borde derecho de la pantalla
//   Poner ................... Espacio, Z, F / clic izquierdo (mantener y arrastrar pinta) / A
//   Borrar .................. X, Supr / clic derecho / B o X del mando
//   Bloque anterior/siguiente Q y E, RePag y AvPag / rueda del mouse / LB y RB
//   Herramienta ............. Tab / clic en la barra / Y
//   Menu .................... Esc / Start

#include <raylib.h>

#include <algorithm>

#include "editor.h"

namespace app {

// Botones extra (controles en pantalla tactil): se mezclan con los del mando. Con esto el mouse/touch de raylib no se usa.
struct EditorExtra {
    bool left = false, right = false, up = false, down = false;
    bool a = false, aHeld = false, b = false, bHeld = false, x = false, y = false, lb = false, rb = false, start = false;
};

// destX/destY/scale: donde se dibuja la pantalla del juego dentro de la ventana (para llevar el mouse a sus pixeles).
inline EditorKeys GatherEditorKeys(float destX, float destY, float scale, const EditorExtra* extra = nullptr) {
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
    if (extra) {
        padLeft = padLeft || extra->left;
        padRight = padRight || extra->right;
        padUp = padUp || extra->up;
        padDown = padDown || extra->down;
        padA = padA || extra->a;
        padAHeld = padAHeld || extra->aHeld;
        padB = padB || extra->b;
        padBHeld = padBHeld || extra->bHeld;
        padX = padX || extra->x;
        padY = padY || extra->y;
        padLB = padLB || extra->lb;
        padRB = padRB || extra->rb;
        padStart = padStart || extra->start;
    }
    const bool useMouse = extra == nullptr;
    // con el mouse, las flechas y WASD cambian la variante del bloque (entero, mitades, puntas); el cursor lo mueve el mouse (o la cruceta del mando)
    k.left = (!useMouse && (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))) || padLeft;
    k.right = (!useMouse && (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))) || padRight;
    k.up = (!useMouse && (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))) || padUp;
    k.down = (!useMouse && (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))) || padDown;
    if (useMouse) {
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_D) || IsKeyPressed(KEY_S)) k.variant = 1;
        else if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_A) || IsKeyPressed(KEY_W)) k.variant = -1;
    }
    const bool mouseLeft = useMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT), mouseRight = useMouse && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    k.place = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_F) || padA || mouseLeft;
    k.placeHeld = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_Z) || IsKeyDown(KEY_F) || padAHeld || (useMouse && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    k.erase = IsKeyPressed(KEY_X) || IsKeyPressed(KEY_DELETE) || padB || padX || mouseRight;
    k.eraseHeld = IsKeyDown(KEY_X) || IsKeyDown(KEY_DELETE) || padBHeld || (useMouse && IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
    k.prevItem = IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_PAGE_UP) || padLB;
    k.nextItem = IsKeyPressed(KEY_E) || IsKeyPressed(KEY_PAGE_DOWN) || padRB;
    k.tool = IsKeyPressed(KEY_TAB) || padY || (useMouse && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE));
    k.menu = IsKeyPressed(KEY_ESCAPE) || padStart;
    k.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_BACK) || padB;
    k.accept = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE) || padA;
    const Vector2 m = GetMousePosition();
    const float cx = (m.x - destX) / scale, cy = (m.y - destY) / scale;
    if (useMouse && scale > 0 && cx >= 0 && cy >= 0 && cx < kScreenW && cy < kScreenH) {
        k.mx = static_cast<int>(cx);
        k.my = static_cast<int>(cy);
    }
    k.mouseRight = mouseRight;
    if (!useMouse && scale > 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {            // pantalla tactil: un toque elige en los menus del editor
        const Vector2 tp = GetMousePosition();
        const float tx = (tp.x - destX) / scale, ty = (tp.y - destY) / scale;
        if (tx >= 0 && ty >= 0 && tx < kScreenW && ty < kScreenH) { k.tap = true; k.mx = static_cast<int>(tx); k.my = static_cast<int>(ty); }
    }
    k.wheel = useMouse ? static_cast<int>(GetMouseWheelMove()) : 0;
    return k;
}

}  // namespace app
