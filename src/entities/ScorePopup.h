#pragma once

namespace bc {

// Numero de puntos (100/200/300/400/500) que aparece un instante sobre el
// punto donde se elimino un enemigo o se agarro un item, y desaparece.
struct ScorePopup {
    float x = 0.0f;
    float y = 0.0f;
    int points = 0;
    double timer = 0.0;
};

} // namespace bc
