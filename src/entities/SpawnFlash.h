#pragma once

namespace bc {

// Destello de aparicion: 4 frames crecientes que se reproducen 2 veces antes
// de que el tanque (protagonista o enemigo) quede activo en el mapa. Mismo
// comportamiento para todos los tanques (seccion 5 y 13 del documento).
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
    int loopsDone_ = 0;
    bool active_ = false;
};

} // namespace bc
