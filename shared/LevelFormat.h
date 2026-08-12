#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace bc {

// Formato de nivel descrito en la seccion 8 del documento de diseno.
struct LevelData {
    int width = 13;
    int height = 13;
    std::vector<std::string> tiles;                 // `height` filas de `width` caracteres
    std::vector<std::array<int, 2>> player_spawns;   // [x, y]
    std::vector<std::array<int, 2>> enemy_spawns;    // [x, y]
    std::map<std::string, int> enemy_wave;           // "basic", "fast", "power", "armor"
    std::array<int, 2> base_position{0, 0};
    std::string aspect_hint;                         // opcional, ej. "16:9"
};

LevelData LoadLevel(const std::string& path);
void SaveLevel(const std::string& path, const LevelData& level);

} // namespace bc
