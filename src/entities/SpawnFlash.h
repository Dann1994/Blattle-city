#pragma once

namespace bc {

// Destello de aparicion: rebote entre el frame 1 y el 4 (1 2 3 4 3 2 1 2 3 4
// 3 2 1), dos vueltas completas, antes de que el tanque (protagonista o
// enemigo) quede activo en el mapa. Mismo comportamiento para todos los
// tanques (seccion 5 y 13 del documento).
class SpawnFlash {
public:
    void Start(float cellX, float cellY);
    void Update(double dt);

    bool IsActive() const { return active_; }
    int FrameIndex() const { return frameIndex_; } // 0..3

    float X() const { return x_; }
    float Y() const { return y_; }

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    double frameTimer_ = 0.0;
    int frameIndex_ = 0;
    int direction_ = 1;
    int loopsDone_ = 0;
    bool active_ = false;
};

} // namespace bc
