#pragma once

// Controles en pantalla tactil (Android; en la PC se prueban con --touch usando el mouse como dedo).
//
//   Izquierda: cruceta (se desliza el dedo).   Derecha: A = disparo, B = disparo 2 / volver atras en los menus.
//   Arriba al centro: START (pausa / empezar).   Extras para el editor: X, Y, L y R.
//   Al escribir un nombre o un codigo aparece un teclado en pantalla (letras, numeros, borrar y listo).
//
// Todo se calcula con la altura de la pantalla, asi que sirve para cualquier telefono o tablet.

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "../nes/battle.h"

namespace app {

class TouchControls {
public:
    enum Btn { kA, kB, kX, kY, kL, kR, kStart, kCount };

    void SetEnabled(bool on) { enabled_ = on; }
    bool Enabled() const { return enabled_; }

    // Una vez por cuadro, antes de leer nada. keyboard = hay que mostrar el teclado de letras.
    void Update(int sw, int sh, bool keyboard) {
        sw_ = sw;
        sh_ = sh;
        keyboardMode_ = keyboard;
        std::copy(std::begin(down_), std::end(down_), std::begin(prev_));
        std::fill(std::begin(down_), std::end(down_), false);
        dirX_ = dirY_ = 0;
        typed_.clear();
        enter_ = backspace_ = back_ = false;
        const bool wasDown = anyTouch_;
        anyTouch_ = false;
        if (!enabled_) return;
        const int n = GetTouchPointCount();
        std::vector<Vector2> pts;
        for (int i = 0; i < n && i < 10; ++i) pts.push_back(GetTouchPosition(i));
        anyTouch_ = !pts.empty();
        const float u = static_cast<float>(sh);
        if (keyboard) {
            // el teclado: un toque nuevo sobre una tecla escribe esa letra
            std::vector<Vector2> fresh;
            for (const Vector2& p : pts) {
                bool known = false;
                for (const Vector2& q : lastPts_) known = known || (std::fabs(p.x - q.x) < u * 0.12f && std::fabs(p.y - q.y) < u * 0.12f);
                if (!known || !wasDown) fresh.push_back(p);
            }
            for (const Vector2& p : fresh) PressKey(p);
            lastPts_ = pts;
            return;
        }
        lastPts_ = pts;
        for (const Vector2& p : pts) {
            // cruceta: el eje que mas se aparta del centro manda
            const Vector2 c = DpadCenter();
            const float r = DpadRadius();
            const float dx = p.x - c.x, dy = p.y - c.y;
            if (dx * dx + dy * dy <= r * r * 1.5f * 1.5f && dx * dx + dy * dy > r * r * 0.08f) {
                if (std::fabs(dx) > std::fabs(dy)) dirX_ = dx < 0 ? -1 : 1;
                else dirY_ = dy < 0 ? -1 : 1;
            }
            for (int b = 0; b < kCount; ++b) {
                if (!Visible(b)) continue;
                const Vector2 bc = BtnCenter(b);
                const float br = BtnRadius(b) * 1.15f;
                if ((p.x - bc.x) * (p.x - bc.x) + (p.y - bc.y) * (p.y - bc.y) <= br * br) down_[b] = true;
            }
        }
    }

    // Botones del control 1 en el formato de la NES (para mezclar con el teclado y el mando).
    uint8_t Bits() const {
        uint8_t v = 0;
        if (dirY_ < 0) v |= nes::con_btn_Up;
        if (dirY_ > 0) v |= nes::con_btn_Down;
        if (dirX_ < 0) v |= nes::con_btn_Left;
        if (dirX_ > 0) v |= nes::con_btn_Right;
        if (down_[kA]) v |= nes::con_btn_A;
        if (down_[kB]) v |= nes::con_btn_B;
        return v;
    }
    bool Pressed(Btn b) const { return down_[b] && !prev_[b]; }
    bool Held(Btn b) const { return down_[b]; }
    int DirX() const { return dirX_; }
    int DirY() const { return dirY_; }
    const std::vector<int>& Typed() const { return typed_; }
    bool EnterPressed() const { return enter_; }
    bool BackspacePressed() const { return backspace_; }
    bool BackKeyPressed() const { return back_; }              // la tecla VOLVER del teclado en pantalla

    // El editor muestra los botones extra (X, Y, L, R) y el resto del tiempo se esconden.
    void SetExtras(bool on) { extras_ = on; }

    void Draw() const {
        if (!enabled_) return;
        if (keyboardMode_) {
            DrawKeyboard();
            return;
        }
        const Color base{255, 255, 255, 46}, active{255, 255, 255, 120}, line{255, 255, 255, 90};
        // cruceta
        const Vector2 c = DpadCenter();
        const float r = DpadRadius();
        DrawCircleV(c, r, Color{255, 255, 255, 26});
        DrawCircleLinesV(c, r, line);
        auto arrow = [&](float ax, float ay, bool on) {
            const float s = r * 0.32f;
            Vector2 p1, p2, p3;
            if (ax != 0) {
                p1 = {c.x + ax * r * 0.82f, c.y};
                p2 = {c.x + ax * r * 0.42f, c.y - s};
                p3 = {c.x + ax * r * 0.42f, c.y + s};
                if (ax < 0) std::swap(p2, p3);
            } else {
                p1 = {c.x, c.y + ay * r * 0.82f};
                p2 = {c.x + s, c.y + ay * r * 0.42f};
                p3 = {c.x - s, c.y + ay * r * 0.42f};
                if (ay < 0) std::swap(p2, p3);
            }
            DrawTriangle(p1, p2, p3, on ? active : base);
        };
        arrow(0, -1, dirY_ < 0);
        arrow(0, 1, dirY_ > 0);
        arrow(-1, 0, dirX_ < 0);
        arrow(1, 0, dirX_ > 0);
        // botones
        static const char* const kLabels[kCount] = {"A", "B", "X", "Y", "L", "R", "START"};
        for (int b = 0; b < kCount; ++b) {
            if (!Visible(b)) continue;
            const Vector2 bc = BtnCenter(b);
            const float br = BtnRadius(b);
            if (b == kStart) {
                const Rectangle rc{bc.x - br * 1.9f, bc.y - br * 0.75f, br * 3.8f, br * 1.5f};
                DrawRectangleRounded(rc, 0.5f, 8, down_[b] ? active : base);
                DrawRectangleRoundedLines(rc, 0.5f, 8, line);
            } else {
                DrawCircleV(bc, br, down_[b] ? active : base);
                DrawCircleLinesV(bc, br, line);
            }
            const int fs = static_cast<int>(br * (b == kStart ? 0.62f : 1.0f));
            const int tw = MeasureText(kLabels[b], fs);
            DrawText(kLabels[b], static_cast<int>(bc.x) - tw / 2, static_cast<int>(bc.y) - fs / 2, fs, Color{255, 255, 255, 190});
        }
    }

private:
    bool Visible(int b) const {
        if (b == kX || b == kY || b == kL || b == kR) return extras_;
        return true;
    }
    float U() const { return static_cast<float>(sh_); }
    Vector2 DpadCenter() const { return {U() * 0.25f, U() * 0.74f}; }
    float DpadRadius() const { return U() * 0.20f; }
    Vector2 BtnCenter(int b) const {
        const float u = U(), w = static_cast<float>(sw_);
        switch (b) {
            case kA: return {w - u * 0.17f, u * 0.72f};
            case kB: return {w - u * 0.37f, u * 0.86f};
            case kX: return {w - u * 0.37f, u * 0.60f};
            case kY: return {w - u * 0.17f, u * 0.48f};
            case kL: return {u * 0.09f, u * 0.10f};
            case kR: return {w - u * 0.09f, u * 0.22f};
            default: return {w - u * 0.17f, u * 0.055f};
        }
    }
    float BtnRadius(int b) const {
        const float u = U();
        switch (b) {
            case kA: return u * 0.125f;
            case kB: return u * 0.095f;
            case kX: case kY: return u * 0.07f;
            case kL: case kR: return u * 0.06f;
            default: return u * 0.04f;
        }
    }

    // ---- teclado de letras ----
    struct Key { int code; Rectangle r; std::string label; };
    std::vector<Key> Keys() const {
        std::vector<Key> v;
        static const char* const rows[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_."};
        const float w = static_cast<float>(sw_), u = U();
        const float kw = std::min(w / 11.5f, u * 0.13f), kh = u * 0.088f;
        const float y0 = u - 5.5f * kh;
        for (int r = 0; r < 4; ++r) {
            const int n = static_cast<int>(std::string(rows[r]).size());
            const float x0 = (w - n * kw) / 2;
            for (int i = 0; i < n; ++i) v.push_back({rows[r][i], {x0 + i * kw, y0 + r * kh * 1.08f, kw * 0.94f, kh}, std::string(1, rows[r][i])});
        }
        const float yb = y0 + 4 * kh * 1.08f;
        v.push_back({-3, {w / 2 - kw * 5.6f, yb, kw * 3.4f, kh}, "VOLVER"});
        v.push_back({-1, {w / 2 - kw * 1.7f, yb, kw * 3.4f, kh}, "BORRAR"});
        v.push_back({-2, {w / 2 + kw * 2.2f, yb, kw * 3.4f, kh}, "LISTO"});
        return v;
    }
    void PressKey(const Vector2& p) {
        for (const Key& k : Keys()) {
            if (p.x >= k.r.x && p.x < k.r.x + k.r.width && p.y >= k.r.y && p.y < k.r.y + k.r.height) {
                if (k.code == -3) back_ = true;
                else if (k.code == -1) backspace_ = true;
                else if (k.code == -2) enter_ = true;
                else typed_.push_back(k.code);
                return;
            }
        }
    }
    void DrawKeyboard() const {
        DrawRectangle(0, static_cast<int>(U() - 5.65f * U() * 0.088f), sw_, sh_, Color{0, 0, 0, 170});
        for (const Key& k : Keys()) {
            DrawRectangleRounded(k.r, 0.25f, 6, Color{255, 255, 255, 60});
            DrawRectangleRoundedLines(k.r, 0.25f, 6, Color{255, 255, 255, 150});
            const int fs = static_cast<int>(k.r.height * (k.code < 0 ? 0.42f : 0.55f));
            const int tw = MeasureText(k.label.c_str(), fs);
            DrawText(k.label.c_str(), static_cast<int>(k.r.x + k.r.width / 2) - tw / 2, static_cast<int>(k.r.y + k.r.height / 2) - fs / 2, fs, WHITE);
        }
    }

    bool enabled_ = false, extras_ = false, keyboardMode_ = false, anyTouch_ = false, enter_ = false, backspace_ = false, back_ = false;
    int sw_ = 1280, sh_ = 720, dirX_ = 0, dirY_ = 0;
    bool down_[kCount] = {}, prev_[kCount] = {};
    std::vector<Vector2> lastPts_;
    std::vector<int> typed_;
};

}  // namespace app
