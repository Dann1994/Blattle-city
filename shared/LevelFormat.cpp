#include "LevelFormat.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace bc {

using json = nlohmann::json;

LevelData LoadLevel(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("No se pudo abrir el nivel: " + path);
    }

    json j;
    file >> j;

    LevelData level;
    level.width = j.at("width").get<int>();
    level.height = j.at("height").get<int>();
    level.tiles = j.at("tiles").get<std::vector<std::string>>();
    if (j.contains("block_shapes")) {
        level.block_shapes = j.at("block_shapes").get<std::vector<std::string>>();
    }

    if (j.contains("player_spawns")) {
        for (const auto& spawn : j.at("player_spawns")) {
            level.player_spawns.push_back({spawn[0].get<int>(), spawn[1].get<int>()});
        }
    }
    if (j.contains("enemy_spawns")) {
        for (const auto& spawn : j.at("enemy_spawns")) {
            level.enemy_spawns.push_back({spawn[0].get<int>(), spawn[1].get<int>()});
        }
    }
    if (j.contains("enemy_wave")) {
        level.enemy_wave = j.at("enemy_wave").get<std::map<std::string, int>>();
    }
    if (j.contains("base_position")) {
        const auto& base = j.at("base_position");
        level.base_position = {base[0].get<int>(), base[1].get<int>()};
    }
    if (j.contains("aspect_hint")) {
        level.aspect_hint = j.at("aspect_hint").get<std::string>();
    }

    return level;
}

void SaveLevel(const std::string& path, const LevelData& level) {
    json j;
    j["width"] = level.width;
    j["height"] = level.height;
    j["tiles"] = level.tiles;
    if (!level.block_shapes.empty()) {
        j["block_shapes"] = level.block_shapes;
    }

    for (const auto& spawn : level.player_spawns) {
        j["player_spawns"].push_back({spawn[0], spawn[1]});
    }
    for (const auto& spawn : level.enemy_spawns) {
        j["enemy_spawns"].push_back({spawn[0], spawn[1]});
    }
    j["enemy_wave"] = level.enemy_wave;
    j["base_position"] = {level.base_position[0], level.base_position[1]};
    if (!level.aspect_hint.empty()) {
        j["aspect_hint"] = level.aspect_hint;
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("No se pudo guardar el nivel: " + path);
    }
    file << j.dump(2);
}

} // namespace bc
