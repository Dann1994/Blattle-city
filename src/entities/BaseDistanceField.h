#pragma once

#include <array>
#include <vector>

#include "Tank.h"
#include "TileMap.h"

namespace bc {

// Campo de distancias (Dijkstra sobre la grilla) desde el aguila hacia el
// resto del mapa: en vez de que cada enemigo "adivine" hacia donde moverse
// con heuristicas, esto calcula el camino real mas corto una sola vez y
// todos los enemigos simplemente bajan el gradiente (ver RankedDirections).
// El ladrillo cuenta como transitable pero caro (kBrickEntryCost: hay que
// voltearlo a tiros para pasar), asi que el campo prefiere rodearlo si el
// rodeo sale mas barato, pero atraviesa cuando de verdad conviene — de ahi
// que a veces ataque por un costado y otras por arriba, segun corresponda.
// Acero y agua son intransitables (ver EntryCost en el .cpp).
class BaseDistanceField {
public:
    // Recalcula el campo entero (barato: la grilla es chica, ~500 celdas).
    // No hace falta llamarlo cada frame; ver kFieldRecomputeInterval en
    // EnemySystem.cpp para el intervalo real que se usa.
    void Recompute(const TileMap& map, int baseX, int baseY);

    // Distancia (en costo acumulado, no celdas) desde (x,y) hasta el aguila.
    // Infinito si (x,y) esta fuera de mapa o no hay forma de llegar.
    float DistanceAt(int x, int y) const;

    // Las 4 direcciones posibles desde (x,y), ordenadas de la que mas
    // acerca al aguila a la que mas aleja (segun DistanceAt del vecino).
    // Quien llama es responsable de saltear las que esten bloqueadas por
    // algo que el campo no sabe (otro tanque encima, por ejemplo).
    std::array<Direction, 4> RankedDirections(int x, int y) const;

private:
    int Index(int x, int y) const { return y * width_ + x; }

    int width_ = 0;
    int height_ = 0;
    std::vector<float> dist_;
};

} // namespace bc
