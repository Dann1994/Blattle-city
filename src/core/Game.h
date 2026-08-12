#pragma once

#include "TileMap.h"

namespace bc {

class Game {
public:
    void Run();

private:
    void Init();
    void ProcessInput();
    void Update(double fixedDt);
    void Render(double interpolationAlpha);
    void Shutdown();

    TileMap map_;
    int windowWidth_ = 0;
    int windowHeight_ = 0;
};

} // namespace bc
