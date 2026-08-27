#include "BulletSystem.h"

#include <algorithm>
#include <cmath>

#include "BulletImpactSystem.h"
#include "SpecialExplosionSystem.h"

namespace bc {

namespace {
constexpr float kBulletHitRadius = 0.2f;     // celdas, para la anulacion bala-contra-bala (seccion 4.5)
constexpr float kSpecialBulletSpeed = 24.0f; // el triple de la bala normal de nivel 1 (8.0)

// Ancho real del sprite de la bala (bullet_*.png, ~3-4px en una celda de
// 16px), para el margen de "roce" perpendicular a la direccion de viaje:
// tanto a nivel de celda (ver el nudge en Update) como a nivel de unidad
// minima de ladrillo (ver FindAliveBrickUnit).
constexpr float kBulletHitHalfWidth = 0.09f;
}

bool BulletSystem::TryShoot(int ownerId, float muzzleX, float muzzleY, Direction direction, float speed, int weaponLevel, int maxPerOwner) {
    int aliveCount = 0;
    for (const Bullet& b : bullets_) {
        if (b.alive && b.ownerId == ownerId) {
            ++aliveCount;
        }
    }
    if (aliveCount >= maxPerOwner) {
        return false;
    }

    Bullet bullet;
    bullet.x = muzzleX;
    bullet.y = muzzleY;
    bullet.direction = direction;
    bullet.ownerId = ownerId;
    bullet.alive = true;
    bullet.speed = speed;
    bullet.weaponLevel = weaponLevel;
    bullet.health = BulletHealthForLevel(weaponLevel);
    bullets_.push_back(bullet);
    return true;
}

bool BulletSystem::HasAliveBullet(int ownerId) const {
    for (const Bullet& b : bullets_) {
        if (b.alive && b.ownerId == ownerId) {
            return true;
        }
    }
    return false;
}

int BulletSystem::AliveBulletCount(int ownerId) const {
    int count = 0;
    for (const Bullet& b : bullets_) {
        if (b.alive && b.ownerId == ownerId) {
            ++count;
        }
    }
    return count;
}

bool BulletSystem::TryShootSpecial(int ownerId, float muzzleX, float muzzleY, Direction direction) {
    Bullet bullet;
    bullet.x = muzzleX;
    bullet.y = muzzleY;
    bullet.direction = direction;
    bullet.ownerId = ownerId;
    bullet.alive = true;
    bullet.speed = kSpecialBulletSpeed;
    bullet.weaponLevel = 4;
    bullet.isSpecial = true;
    bullet.health = kSpecialBulletHealth;
    bullets_.push_back(bullet);
    return true;
}

namespace {

// Destruye las unidades indicadas por [firstIndex, firstIndex+count) a lo
// largo del eje variable (columna si stepIsColumn, fila si no), todas en la
// fila/columna fija "fixedIndex". Devuelve true si destruyo algo vivo.
bool DestroyUnitSpan(Cell& cell, int fixedIndex, bool fixedIsRow, int firstIndex, int count) {
    bool destroyedAny = false;
    for (int i = firstIndex; i < firstIndex + count; ++i) {
        const int row = fixedIsRow ? fixedIndex : i;
        const int col = fixedIsRow ? i : fixedIndex;
        BrickUnit& unit = cell.brickUnits[row * kBrickGridSize + col];
        if (unit.alive) {
            unit.alive = false;
            destroyedAny = true;
        }
    }
    return destroyedAny;
}

// Le pega a la mitad de esa capa mas cercana a la costura compartida con la
// celda que realmente recibio el impacto, en la celda vecina (neighborX,
// neighborY) — igual que un golpe de esquina normal, pero del lado de afuera.
// Para hierro (grilla 2x2) esa "mitad" ya es 1 sola unidad; para ladrillo
// (grilla 4x4) son 2, asi que entre el golpe de esquina del lado propio (2
// unidades) y este graze del vecino (2 unidades) se completa una capa de 4
// unidades repartida entre ambos bloques. "along" es la posicion (0..1) a lo
// largo del eje compartido con esa vecina (fracY si la vecina esta a
// izq/der, fracX si esta arriba/abajo); neighborRowIsAlong indica si esa
// posicion cae en la fila o la columna de la vecina; nearIsMaxIndex indica
// si su borde pegado a la costura es el ultimo indice de su grilla
// (derecha/abajo) o el primero (izquierda/arriba).
void GrazeNeighborCorner(TileMap& map, int neighborX, int neighborY, float along, bool neighborRowIsAlong, bool nearIsMaxIndex, int steelDamage) {
    if (!map.InBounds(neighborX, neighborY)) {
        return;
    }
    Cell& cell = map.At(neighborX, neighborY);
    if (cell.type == TileType::Brick) {
        const int alongIndex = std::clamp(static_cast<int>(along * kBrickGridSize), 0, kBrickGridSize - 1);
        constexpr int kHalf = kBrickGridSize / 2;
        const int firstNear = nearIsMaxIndex ? kHalf : 0;
        bool destroyedAny = false;
        for (int i = firstNear; i < firstNear + kHalf; ++i) {
            const int row = neighborRowIsAlong ? alongIndex : i;
            const int col = neighborRowIsAlong ? i : alongIndex;
            BrickUnit& unit = cell.brickUnits[row * kBrickGridSize + col];
            if (unit.alive) {
                unit.alive = false;
                destroyedAny = true;
            }
        }
        if (destroyedAny && cell.BrickFullyDestroyed()) {
            cell.type = TileType::Empty;
        }
    } else if (cell.type == TileType::Steel) {
        const int alongIndex = std::clamp(static_cast<int>(along * kSteelGridSize), 0, kSteelGridSize - 1);
        const int nearIndex = nearIsMaxIndex ? (kSteelGridSize - 1) : 0;
        SteelUnit& unit = cell.steelUnits[(neighborRowIsAlong ? alongIndex : nearIndex) * kSteelGridSize + (neighborRowIsAlong ? nearIndex : alongIndex)];
        if (unit.alive) {
            unit.hp -= steelDamage;
            if (unit.hp <= 0) {
                unit.alive = false;
            }
            if (cell.SteelFullyDestroyed()) {
                cell.type = TileType::Empty;
            }
        }
    }
}

// Camina "count" unidades en linea recta desde (cellX,cellY,row,col) sumando
// (rowStep,colStep) en cada paso (uno de los dos siempre 0: la cadena es
// siempre horizontal o siempre vertical, nunca diagonal), destruyendo cada
// una; cruza a la celda de al lado (en X o en Y, la que corresponda) cuantas
// veces haga falta si el camino se sale de la grilla 4x4 de la celda de
// origen. Se corta antes si algun paso se sale del mapa o encuentra una
// celda que no es ladrillo (nada que romper ahi, no hay como "saltarla" para
// seguir del otro lado). Deja como Empty cualquier celda que haya quedado
// sin unidades vivas.
void DestroyBrickChain(TileMap& map, int cellX, int cellY, int row, int col, int rowStep, int colStep, int count) {
    int curCellX = cellX;
    int curCellY = cellY;
    int curRow = row;
    int curCol = col;
    for (int i = 0; i < count; ++i) {
        curRow += rowStep;
        curCol += colStep;
        if (curCol < 0) {
            curCellX -= 1;
            curCol = kBrickGridSize - 1;
        } else if (curCol >= kBrickGridSize) {
            curCellX += 1;
            curCol = 0;
        }
        if (curRow < 0) {
            curCellY -= 1;
            curRow = kBrickGridSize - 1;
        } else if (curRow >= kBrickGridSize) {
            curCellY += 1;
            curRow = 0;
        }

        if (!map.InBounds(curCellX, curCellY)) {
            return; // borde del mapa: no hay mas nada que romper de este lado
        }
        Cell& curCell = map.At(curCellX, curCellY);
        if (curCell.type != TileType::Brick) {
            return; // no es ladrillo (vacia, hierro, etc.): se corta la cadena aca
        }
        curCell.brickUnits[curRow * kBrickGridSize + curCol].alive = false;
        if (curCell.BrickFullyDestroyed()) {
            curCell.type = TileType::Empty;
        }
    }
}

// Destruye la unidad de ladrillo (cellX,cellY,row,col) -asume que quien
// llama ya confirmo que esta viva- mas 3 en cadena: 2 hacia el lado del
// impacto y 1 mas hacia el lado contrario (pedido explicitamente: "rompe la
// principal, dos de un lado y una del otro"). La cadena es horizontal
// (misma fila, columna variable) si el disparo viaja vertical -Up/Down,
// pega de frente en la cara de arriba/abajo, y ahi la posicion que varia de
// forma util con el punto de impacto es la horizontal (izquierda/derecha)-,
// o vertical (misma columna, fila variable) si el disparo viaja horizontal
// -Left/Right, pega en la cara izq/der, y ahi es la posicion vertical
// (arriba/abajo) la que varia de forma util- (pedido explicitamente: mismo
// comportamiento tambien para disparos horizontales). towardNegative indica
// si el lado del impacto fue el de indice menor (izquierda en columnas,
// arriba en filas) o el de indice mayor.
void DestroyBrickUnitAndNeighbors(TileMap& map, int cellX, int cellY, int row, int col, bool chainHorizontal, bool towardNegative) {
    Cell& cell = map.At(cellX, cellY);
    cell.brickUnits[row * kBrickGridSize + col].alive = false;
    if (cell.BrickFullyDestroyed()) {
        cell.type = TileType::Empty;
    }

    const int primaryStep = towardNegative ? -1 : 1;
    const int rowStep = chainHorizontal ? 0 : primaryStep;
    const int colStep = chainHorizontal ? primaryStep : 0;
    DestroyBrickChain(map, cellX, cellY, row, col, rowStep, colStep, 2);
    DestroyBrickChain(map, cellX, cellY, row, col, -rowStep, -colStep, 1);
}

// Si (x,y) cae dentro de una celda de ladrillo (en el mapa) y la unidad
// minima que le corresponde esta viva, la devuelve. false si esta afuera
// del mapa, la celda no es ladrillo, o esa unidad exacta ya esta destruida.
bool TryGetAliveBrickUnitAt(TileMap& map, float x, float y, int& outCellX, int& outCellY, int& outRow, int& outCol) {
    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));
    if (!map.InBounds(cx, cy)) {
        return false;
    }
    Cell& cell = map.At(cx, cy);
    if (cell.type != TileType::Brick) {
        return false;
    }
    const float fracX = x - static_cast<float>(cx);
    const float fracY = y - static_cast<float>(cy);
    const int col = std::clamp(static_cast<int>(fracX * kBrickGridSize), 0, kBrickGridSize - 1);
    const int row = std::clamp(static_cast<int>(fracY * kBrickGridSize), 0, kBrickGridSize - 1);
    if (!cell.brickUnits[row * kBrickGridSize + col].alive) {
        return false;
    }
    outCellX = cx;
    outCellY = cy;
    outRow = row;
    outCol = col;
    return true;
}

// Ubica la unidad de ladrillo viva mas cercana al punto de impacto real
// (hitX,hitY): primero prueba el punto exacto: si esa unidad especifica ya
// estaba destruida (muy comun ahora que un impacto rompe una cadena de
// varias, dejando unidades sueltas todavia vivas justo al lado de otras ya
// muertas), prueba tambien el margen del ancho real del sprite de la bala
// (kBulletHitHalfWidth) en el eje perpendicular a su direccion de viaje -el
// eje a lo largo del viaje ya esta cubierto por los sub-pasos de Update,
// mas angostos que una unidad minima- incluso si ese margen cae en la celda
// vecina (pedido explicitamente: "asegurate que bala que toque, lo destruya
// ya sea que un pixel solo toque el ladrillo"). outX/outY reciben el punto
// exacto que encontro la unidad viva (para el criterio de que mitad de esa
// unidad se toco). Devuelve false si no hay ninguna unidad viva en todo el
// rango (recien ahi la bala sigue de largo en silencio, como antes).
bool FindAliveBrickUnit(TileMap& map, float hitX, float hitY, Direction direction, int& outCellX, int& outCellY, int& outRow, int& outCol, float& outX, float& outY) {
    if (TryGetAliveBrickUnitAt(map, hitX, hitY, outCellX, outCellY, outRow, outCol)) {
        outX = hitX;
        outY = hitY;
        return true;
    }

    const bool travelVertical = (direction == Direction::Up || direction == Direction::Down);
    const float lowX = travelVertical ? hitX - kBulletHitHalfWidth : hitX;
    const float lowY = travelVertical ? hitY : hitY - kBulletHitHalfWidth;
    const float highX = travelVertical ? hitX + kBulletHitHalfWidth : hitX;
    const float highY = travelVertical ? hitY : hitY + kBulletHitHalfWidth;

    if (TryGetAliveBrickUnitAt(map, lowX, lowY, outCellX, outCellY, outRow, outCol)) {
        outX = lowX;
        outY = lowY;
        return true;
    }
    if (TryGetAliveBrickUnitAt(map, highX, highY, outCellX, outCellY, outRow, outCol)) {
        outX = highX;
        outY = highY;
        return true;
    }
    return false;
}

// Bloques pegados uno al lado del otro: si el impacto cae en la franja
// angosta pegada al borde compartido con la celda vecina (no toda la mitad
// "esquina", solo bien cerca de la costura) y esa vecina tambien es
// destructible, tambien le pega a su unidad mas cercana a la costura.
void CheckSeamGraze(TileMap& map, int cellX, int cellY, bool horizontalHit, float fracX, float fracY, int power) {
    constexpr float kSeamBand = 0.15f;
    const int steelDamage = power;
    if (horizontalHit) {
        // La bala penetro por la cara izq/der: la costura relevante es
        // arriba/abajo, decidida por fracY. La vecina comparte columna, y
        // "along" (que fila de la vecina) es fracX.
        if (fracY < kSeamBand) {
            GrazeNeighborCorner(map, cellX, cellY - 1, fracX, /*neighborRowIsAlong=*/false, /*nearIsMaxIndex=*/true, steelDamage);
        } else if (fracY > 1.0f - kSeamBand) {
            GrazeNeighborCorner(map, cellX, cellY + 1, fracX, /*neighborRowIsAlong=*/false, /*nearIsMaxIndex=*/false, steelDamage);
        }
    } else {
        // La bala penetro por la cara de arriba/abajo: la costura relevante
        // es izq/der, decidida por fracX. La vecina comparte fila, y
        // "along" (que columna de la vecina) es fracY.
        if (fracX < kSeamBand) {
            GrazeNeighborCorner(map, cellX - 1, cellY, fracY, /*neighborRowIsAlong=*/true, /*nearIsMaxIndex=*/true, steelDamage);
        } else if (fracX > 1.0f - kSeamBand) {
            GrazeNeighborCorner(map, cellX + 1, cellY, fracY, /*neighborRowIsAlong=*/true, /*nearIsMaxIndex=*/false, steelDamage);
        }
    }
}

} // namespace

bool BulletSystem::HandleBrickHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, int power) {
    Cell& cell = map.At(cellX, cellY);

    const float fracX = hitX - static_cast<float>(cellX);
    const float fracY = hitY - static_cast<float>(cellY);
    const int col = std::clamp(static_cast<int>(fracX * kBrickGridSize), 0, kBrickGridSize - 1);
    const int row = std::clamp(static_cast<int>(fracY * kBrickGridSize), 0, kBrickGridSize - 1);

    constexpr int kHalf = kBrickGridSize / 2;
    const bool horizontalHit = (hitFrom == Direction::Left || hitFrom == Direction::Right);
    bool destroyedAny = false;

    // Destruye la capa "layerIndex" (columna si horizontalHit, fila si no).
    // La fila/columna fija decide cuanto de esa capa: centro = capa entera
    // (4 unidades); esquina = mitad de esa capa, del lado donde pego.
    // Devuelve si esa capa en particular tenia algo vivo.
    auto destroyLayer = [&](int layerIndex) {
        if (layerIndex < 0 || layerIndex >= kBrickGridSize) {
            return false;
        }
        bool did = false;
        if (horizontalHit) {
            if (row == 1 || row == 2) {
                did = DestroyUnitSpan(cell, layerIndex, /*fixedIsRow=*/false, 0, kBrickGridSize);
            } else {
                const int firstRow = (row == 0) ? 0 : kHalf;
                did = DestroyUnitSpan(cell, layerIndex, /*fixedIsRow=*/false, firstRow, kHalf);
            }
        } else {
            if (col == 1 || col == 2) {
                did = DestroyUnitSpan(cell, layerIndex, /*fixedIsRow=*/true, 0, kBrickGridSize);
            } else {
                const int firstCol = (col == 0) ? 0 : kHalf;
                did = DestroyUnitSpan(cell, layerIndex, /*fixedIsRow=*/true, firstCol, kHalf);
            }
        }
        if (did) {
            destroyedAny = true;
        }
        return did;
    };

    // Siempre escanea desde la capa de ENTRADA (la mas cercana a por donde
    // vino la bala) hasta la capa de llegada, nunca desde la posicion previa
    // de la bala (pedido explicitamente: con el tanque pegado al bloque, el
    // cano dispara ya practicamente adentro de la celda, y estimar la capa
    // de arranque con esa posicion podia saltearse la unica capa que
    // quedaba viva mas cerca de la entrada, dejando pasar la bala de largo).
    // Escanear las hasta 4 capas es barato y encuentra la primera viva sin
    // importar cuantas ya esten vacias antes.
    const int layerFrom = (hitFrom == Direction::Right || hitFrom == Direction::Down) ? 0 : (kBrickGridSize - 1);
    const int layerTo = horizontalHit ? col : row;
    const int travelStep = (layerTo >= layerFrom) ? 1 : -1;

    int stoppedAtLayer = -1;
    for (int layer = layerFrom; ; layer += travelStep) {
        if (destroyLayer(layer)) {
            stoppedAtLayer = layer;
            break;
        }
        if (layer == layerTo) {
            break;
        }
    }

    if (stoppedAtLayer < 0) {
        return false; // todo el tramo ya estaba vacio: no frena la bala
    }

    // Nota: antes, nivel 4 rompia ademas la capa siguiente en un solo
    // llamado (doubleLayer). Ya no hace falta: con el sistema de
    // potencia/salud de bala (ver Bullet::health en Update), una bala que le
    // sobra salud despues de este impacto sigue de largo sola, y el
    // sub-paso siguiente (a 0.1 celdas, mas angosto que una unidad minima de
    // 0.25) vuelve a llamar a esta funcion, encontrando y rompiendo la
    // proxima capa viva por su cuenta — logra lo mismo de forma mas general
    // (tambien atraviesa tanques, no solo capas de un mismo bloque).

    if (cell.BrickFullyDestroyed()) {
        cell.type = TileType::Empty;
    }

    CheckSeamGraze(map, cellX, cellY, horizontalHit, fracX, fracY, power);
    return true;
}

bool BulletSystem::HandleSteelHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, int power, int& outDealt, bool& outPrimaryUnitDestroyed) {
    Cell& cell = map.At(cellX, cellY);

    const float fracX = hitX - static_cast<float>(cellX);
    const float fracY = hitY - static_cast<float>(cellY);
    const bool horizontalHit = (hitFrom == Direction::Left || hitFrom == Direction::Right);

    // El otro eje decide centro (capa entera, 2 unidades) vs punta (1 unidad).
    const float otherFrac = horizontalHit ? fracY : fracX;
    constexpr float kCenterBandMin = 0.25f;
    constexpr float kCenterBandMax = 0.75f;
    const bool centerHit = otherFrac >= kCenterBandMin && otherFrac <= kCenterBandMax;
    const int tipIndex = (otherFrac < 0.5f) ? 0 : 1;

    bool hitAliveUnit = false;
    outDealt = 0; // cuanta salud de la unidad PRIMARIA (la primera viva que encuentra) se le resta a la bala, ver Update
    outPrimaryUnitDestroyed = false; // si esa misma unidad PRIMARIA quedo destruida con este golpe (para el sonido de "resiste" vs "se rompe", ver Update)

    auto damageUnit = [&](int row, int col) {
        SteelUnit& unit = cell.steelUnits[row * kSteelGridSize + col];
        if (!unit.alive) {
            return;
        }
        if (!hitAliveUnit) {
            outDealt = std::min(power, unit.hp); // la primera unidad viva que toca esta llamada es la "primaria" para el sistema de resistencia de la bala
        }
        const bool isPrimary = !hitAliveUnit;
        hitAliveUnit = true;
        unit.hp -= power;
        if (unit.hp <= 0) {
            unit.alive = false;
            if (isPrimary) {
                outPrimaryUnitDestroyed = true;
            }
        }
    };

    auto tryLayer = [&](int layer) {
        if (centerHit) {
            for (int i = 0; i < kSteelGridSize; ++i) {
                damageUnit(horizontalHit ? i : layer, horizontalHit ? layer : i);
            }
        } else {
            damageUnit(horizontalHit ? tipIndex : layer, horizontalHit ? layer : tipIndex);
        }
    };

    // Prueba primero la capa de ENTRADA (la mas cercana a por donde vino la
    // bala); si ahi ya no quedaba nada, prueba la otra antes de dejarla
    // pasar (pedido explicitamente: con el tanque pegado al bloque no hay
    // que esperar a un frame futuro para que la posicion "caiga sola" en la
    // capa de al lado — ver el mismo motivo en HandleBrickHit).
    const int entryLayer = (hitFrom == Direction::Right || hitFrom == Direction::Down) ? 0 : (kSteelGridSize - 1);
    tryLayer(entryLayer);
    if (!hitAliveUnit) {
        tryLayer(kSteelGridSize - 1 - entryLayer);
    }

    if (!hitAliveUnit) {
        return false; // las 2 capas ya estaban vacias: no frena la bala
    }

    if (cell.SteelFullyDestroyed()) {
        cell.type = TileType::Empty;
    }

    CheckSeamGraze(map, cellX, cellY, horizontalHit, fracX, fracY, power);
    return true;
}

void BulletSystem::TriggerSpecialExplosion(TileMap& map, float x, float y, int ownerId, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents, Sound explosionSound, int directHitOwnerId) {
    const int minX = std::max(0, static_cast<int>(std::floor(x - kSpecialExplosionRadius)));
    const int maxX = std::min(map.Width() - 1, static_cast<int>(std::floor(x + kSpecialExplosionRadius)));
    const int minY = std::max(0, static_cast<int>(std::floor(y - kSpecialExplosionRadius)));
    const int maxY = std::min(map.Height() - 1, static_cast<int>(std::floor(y + kSpecialExplosionRadius)));

    for (int cy = minY; cy <= maxY; ++cy) {
        for (int cx = minX; cx <= maxX; ++cx) {
            const float cellDx = (static_cast<float>(cx) + 0.5f) - x;
            const float cellDy = (static_cast<float>(cy) + 0.5f) - y;
            if (cellDx * cellDx + cellDy * cellDy > kSpecialExplosionRadius * kSpecialExplosionRadius) {
                continue; // fuera del radio circular de la explosion
            }

            Cell& cell = map.At(cx, cy);
            if (cell.type == TileType::Brick) {
                for (BrickUnit& unit : cell.brickUnits) {
                    unit.alive = false;
                }
                cell.type = TileType::Empty;
            } else if (cell.type == TileType::Steel) {
                cell.type = TileType::Empty;
            } else if (cell.type == TileType::Base) {
                // Pedido explicitamente: queda con su frame de "destruida"
                // (ver TileType::BaseDestroyed) en vez de desaparecer.
                cell.type = TileType::BaseDestroyed;
            }
        }
    }

    specialExplosions.Spawn(x, y);
    explosionEvents.push_back(SpecialExplosionEvent{x, y, kSpecialExplosionRadius, ownerId, directHitOwnerId});
    PlaySound(explosionSound);
}

void BulletSystem::Update(double dt, TileMap& map, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents, const std::vector<TankCombatState>& tanks, std::vector<SpecialDirectKillEvent>& directKillEvents, Sound brickBreakSound, Sound blockResistSound, Sound explosionSound) {
    explosionEvents.clear();
    directKillEvents.clear();

    // Posicion antes de moverse este frame, para la deteccion de cruce
    // bala-contra-bala de mas abajo (ver comentario ahi: a velocidades
    // altas dos balas pueden atravesarse en un solo frame sin quedar nunca
    // dentro del radio de choque al final del paso).
    std::vector<float> preMoveX(bullets_.size());
    std::vector<float> preMoveY(bullets_.size());
    for (size_t i = 0; i < bullets_.size(); ++i) {
        preMoveX[i] = bullets_[i].x;
        preMoveY[i] = bullets_[i].y;
    }

    // Tope de cuanto se mueve una bala entre cada chequeo de colision
    // (pedido explicitamente: "la bala es mas rapida y tiene problemas para
    // detectar las colisiones", niveles 2 y 4 de ataque). Antes cada bala
    // se movia toda su distancia del frame de un solo salto y RECIEN AHI se
    // fijaba si habia algo solido en el punto de llegada: a las velocidades
    // mas altas (16 y 14 celdas/seg, ver kBulletSpeedByLevel) ese salto
    // podia terminar del otro lado de una celda vecina angosta sin que
    // ningun chequeo intermedio la haya visto nunca, dejando pasar la bala
    // de largo. Ahora, si la distancia de este frame es mayor a este tope,
    // se parte en varios sub-pasos mas cortos (cada uno con su propio
    // chequeo completo de colision, exactamente igual al de antes), asi
    // ninguna celda/unidad angosta en el medio del camino queda sin
    // revisar. kMaxCollisionStep queda bien por debajo del ancho de la
    // unidad minima de ladrillo (0.25 celdas, ver kBrickGridSize) para que
    // ni la bala mas rapida (especial, 24 celdas/seg) pueda saltearse una.
    constexpr float kMaxCollisionStep = 0.1f;

    for (Bullet& bullet : bullets_) {
        if (!bullet.alive) {
            continue;
        }

        float dx = 0.0f, dy = 0.0f;
        DirectionVector(bullet.direction, dx, dy);

        const float totalDistance = static_cast<float>(bullet.speed * dt);
        const int numSteps = std::max(1, static_cast<int>(std::ceil(totalDistance / kMaxCollisionStep)));
        const float stepDistance = totalDistance / static_cast<float>(numSteps);

        for (int step = 0; step < numSteps; ++step) {
        const float newX = bullet.x + dx * stepDistance;
        const float newY = bullet.y + dy * stepDistance;
        int cellX = static_cast<int>(std::floor(newX));
        int cellY = static_cast<int>(std::floor(newY));

        // El sprite de la bala tiene un poco de ancho real (bullet_*.png,
        // ~3-4px en una celda de 16px) pero la colision solo miraba el punto
        // central: si la bala pasaba muy pegada al borde de un bloque, el
        // sprite lo rozaba visualmente sin que el punto central llegara a
        // entrar en esa celda, y el impacto nunca se registraba. Si la celda
        // central esta vacia pero el margen del sprite (perpendicular al
        // movimiento) roza la celda de al lado y esa si bloquea, se usa esa
        // celda en su lugar.
        // Antes tenia +1px de margen extra (pedido explicitamente en su
        // momento para no dejar pasar rozes reales), pero se paso de largo:
        // el margen quedaba mas ancho que el sprite y rompia bloques que la
        // bala ni tocaba visualmente. Se achica de vuelta al ancho real del
        // sprite nada mas (pedido explicitamente). kBulletHitHalfWidth vive
        // ahora al principio del archivo: la reusa tambien FindAliveBrickUnit.
        const bool centerBlocks = map.InBounds(cellX, cellY) && TileBlocksShots(map.At(cellX, cellY).type);
        if (!centerBlocks && dx != 0.0f) {
            const int cellYLow = static_cast<int>(std::floor(newY - kBulletHitHalfWidth));
            const int cellYHigh = static_cast<int>(std::floor(newY + kBulletHitHalfWidth));
            if (cellYLow != cellY && map.InBounds(cellX, cellYLow) && TileBlocksShots(map.At(cellX, cellYLow).type)) {
                cellY = cellYLow;
            } else if (cellYHigh != cellY && map.InBounds(cellX, cellYHigh) && TileBlocksShots(map.At(cellX, cellYHigh).type)) {
                cellY = cellYHigh;
            }
        } else if (!centerBlocks && dy != 0.0f) {
            const int cellXLow = static_cast<int>(std::floor(newX - kBulletHitHalfWidth));
            const int cellXHigh = static_cast<int>(std::floor(newX + kBulletHitHalfWidth));
            if (cellXLow != cellX && map.InBounds(cellXLow, cellY) && TileBlocksShots(map.At(cellXLow, cellY).type)) {
                cellX = cellXLow;
            } else if (cellXHigh != cellX && map.InBounds(cellXHigh, cellY) && TileBlocksShots(map.At(cellXHigh, cellY).type)) {
                cellX = cellXHigh;
            }
        }

        // hitX/hitY son la posicion real de llegada de este sub-paso, SIEMPRE
        // la posicion real de la bala (newX/newY) sin forzar nada, incluso
        // cuando el chequeo de arriba redirigio cellX/cellY a la celda
        // vecina (un roce). Antes se forzaba al centro de la celda
        // vecina en el eje perpendicular (pensado para el viejo sistema de
        // destruir toda una fila/columna, evitaba un patron dentado), pero
        // con el sistema actual (una unidad minima exacta por impacto, ver
        // el rediseño del ladrillo) eso hacia que TODO roce contra una
        // pared, sin importar donde pegara, apuntara siempre a la MISMA
        // unidad fija — la primera vez la rompia, y como esa unidad ya
        // quedaba muerta, cualquier roce siguiente no encontraba nada ahi y
        // pasaba de largo en silencio (el bug reportado: "a veces rompe, a
        // veces no"). Usando la posicion real, el clamp de abajo (fracX/
        // fracY * grilla) ya cae solo en la unidad correcta pegada al borde
        // por donde entro.
        const float hitX = newX;
        const float hitY = newY;

        if (!map.InBounds(cellX, cellY)) {
            // Borde del escenario: no hay tile ahi, pero igual es un impacto.
            const float edgeX = std::clamp(newX, 0.0f, static_cast<float>(map.Width()));
            const float edgeY = std::clamp(newY, 0.0f, static_cast<float>(map.Height()));
            if (bullet.isSpecial) {
                TriggerSpecialExplosion(map, edgeX, edgeY, bullet.ownerId, specialExplosions, explosionEvents, explosionSound);
            } else {
                impacts.Spawn(edgeX, edgeY, bullet.weaponLevel == 4);
                if (bullet.ownerId < kEnemyOwnerIdBase) {
                    // Pedido explicitamente: tambien cuenta como "pega en
                    // una pared y no la rompe" — las paredes del nivel
                    // (borde del mapa), solo para balas de jugador.
                    PlaySound(blockResistSound);
                }
            }
            bullet.alive = false;
            break;
        }

        const TileType hitType = map.At(cellX, cellY).type;

        if (bullet.isSpecial) {
            // Choque directo contra un tanque (que no sea el propio dueño):
            // nivel 1-3 sin escudo se destruye directo y la bala sigue de
            // largo (consume 1 punto de su propia salud); con escudo o
            // nivel 4 (asi es como esta lista fuerza que el Power - que no
            // tiene el mismo concepto de "nivel" que un jugador - tambien
            // caiga en este caso, ver como se arma "tanks" mas arriba en
            // Game.cpp), la bala explota entera.
            const TankCombatState* hitTank = nullptr;
            for (const TankCombatState& t : tanks) {
                if (t.ownerId == bullet.ownerId) {
                    continue;
                }
                if (newX >= t.left && newX <= t.right && newY >= t.top && newY <= t.bottom) {
                    hitTank = &t;
                    break;
                }
            }

            if (hitTank != nullptr && !hitTank->shielded && hitTank->weaponLevel != 4) {
                directKillEvents.push_back(SpecialDirectKillEvent{hitTank->ownerId, bullet.ownerId});
                bullet.health -= 1;
                if (bullet.health <= 0) {
                    TriggerSpecialExplosion(map, newX, newY, bullet.ownerId, specialExplosions, explosionEvents, explosionSound, hitTank->ownerId);
                    bullet.alive = false;
                    break;
                }
                bullet.x = newX;
                bullet.y = newY;
                continue;
            }

            if (hitTank != nullptr) {
                // Escudo o nivel 4: fuerza la explosion.
                TriggerSpecialExplosion(map, newX, newY, bullet.ownerId, specialExplosions, explosionEvents, explosionSound, hitTank->ownerId);
                bullet.alive = false;
                break;
            }

            if (hitType == TileType::Base) {
                // El aguila: siempre destruye y explota de una, sin
                // consumir salud (el fin de la partida no espera a que se
                // agote). Pedido explicitamente: queda con su frame de
                // "destruida" (ver TileType::BaseDestroyed) en vez de
                // desaparecer. Quien llama (Game::Update) detecta que la
                // celda dejo de ser Base y termina la partida.
                map.At(cellX, cellY).type = TileType::BaseDestroyed;
                TriggerSpecialExplosion(map, hitX, hitY, bullet.ownerId, specialExplosions, explosionEvents, explosionSound);
                bullet.alive = false;
                break;
            }

            if (hitType == TileType::Brick) {
                // Ladrillo rediseñado desde cero: cada unidad minima de la
                // grilla 4x4 tiene salud 1 — la bala destruye la unidad viva
                // mas cercana al punto de impacto (el punto exacto, o si esa
                // unidad ya estaba muerta, el margen del ancho real del
                // sprite -ver FindAliveBrickUnit, pedido explicitamente para
                // que ningun roce real quede sin romper nada) mas su cadena
                // de vecinas pegadas (ver DestroyBrickUnitAndNeighbors), y
                // siendo especial, explota ahi mismo (mismo criterio que al
                // tocar hierro u otro obstaculo solido).
                int unitCellX = 0, unitCellY = 0, unitRow = 0, unitCol = 0;
                float unitX = 0.0f, unitY = 0.0f;
                if (FindAliveBrickUnit(map, hitX, hitY, bullet.direction, unitCellX, unitCellY, unitRow, unitCol, unitX, unitY)) {
                    const bool chainHorizontal = (bullet.direction == Direction::Up || bullet.direction == Direction::Down);
                    const float unitFracX = unitX - static_cast<float>(unitCellX);
                    const float unitFracY = unitY - static_cast<float>(unitCellY);
                    const bool towardNegative = chainHorizontal
                        ? (unitFracX * kBrickGridSize - static_cast<float>(unitCol)) < 0.5f
                        : (unitFracY * kBrickGridSize - static_cast<float>(unitRow)) < 0.5f;
                    DestroyBrickUnitAndNeighbors(map, unitCellX, unitCellY, unitRow, unitCol, chainHorizontal, towardNegative);
                    TriggerSpecialExplosion(map, hitX, hitY, bullet.ownerId, specialExplosions, explosionEvents, explosionSound);
                    bullet.alive = false;
                    break;
                }
                // No hay ninguna unidad viva ni en el punto exacto ni en el margen del sprite: la bala sigue de largo.
                bullet.x = newX;
                bullet.y = newY;
                continue;
            }

            if (hitType == TileType::Steel) {
                int dealt = 0;
                bool unitDestroyed = false;
                if (HandleSteelHit(map, cellX, cellY, hitX, hitY, bullet.direction, kSpecialBulletPower, dealt, unitDestroyed)) {
                    bullet.health -= dealt;
                    if (bullet.health <= 0) {
                        TriggerSpecialExplosion(map, hitX, hitY, bullet.ownerId, specialExplosions, explosionEvents, explosionSound);
                        bullet.alive = false;
                        break;
                    }
                    bullet.x = newX;
                    bullet.y = newY;
                    continue;
                }
                bullet.x = newX;
                bullet.y = newY;
                continue;
            }

            if (TileBlocksShots(hitType)) {
                // Obstaculo generico sin nocion de salud (p.ej. tile no
                // destructible fuera de ladrillo/hierro/base): siempre
                // explota entero, como antes.
                TriggerSpecialExplosion(map, hitX, hitY, bullet.ownerId, specialExplosions, explosionEvents, explosionSound);
                bullet.alive = false;
                break;
            }
            bullet.x = newX;
            bullet.y = newY;
            continue;
        }

        if (hitType == TileType::Brick) {
            // Ladrillo rediseñado desde cero: cada unidad minima de la
            // grilla 4x4 tiene salud 1 — un impacto destruye la unidad viva
            // mas cercana al punto de impacto (el punto exacto, o si esa
            // unidad ya estaba muerta, el margen del ancho real del sprite
            // -ver FindAliveBrickUnit, pedido explicitamente para que ningun
            // roce real quede sin romper nada) mas su cadena de vecinas
            // pegadas (ver DestroyBrickUnitAndNeighbors; aplica igual a
            // balas de jugador y enemigo, ver el chequeo de ownerId mas
            // abajo que solo afecta al sonido), suena el sonido ya definido
            // (solo balas de jugador) y la bala se frena ahi.
            int unitCellX = 0, unitCellY = 0, unitRow = 0, unitCol = 0;
            float unitX = 0.0f, unitY = 0.0f;
            if (FindAliveBrickUnit(map, hitX, hitY, bullet.direction, unitCellX, unitCellY, unitRow, unitCol, unitX, unitY)) {
                const bool chainHorizontal = (bullet.direction == Direction::Up || bullet.direction == Direction::Down);
                const float unitFracX = unitX - static_cast<float>(unitCellX);
                const float unitFracY = unitY - static_cast<float>(unitCellY);
                const bool towardNegative = chainHorizontal
                    ? (unitFracX * kBrickGridSize - static_cast<float>(unitCol)) < 0.5f
                    : (unitFracY * kBrickGridSize - static_cast<float>(unitRow)) < 0.5f;
                DestroyBrickUnitAndNeighbors(map, unitCellX, unitCellY, unitRow, unitCol, chainHorizontal, towardNegative);
                if (bullet.ownerId < kEnemyOwnerIdBase) {
                    PlaySound(brickBreakSound); // pedido explicitamente: solo bala de jugador
                }
                impacts.Spawn(hitX, hitY, bullet.weaponLevel == 4);
                bullet.alive = false;
                break;
            }
            // No hay ninguna unidad viva ni en el punto exacto ni en el margen del sprite: la bala sigue de largo.
            bullet.x = newX;
            bullet.y = newY;
            continue;
        }

        if (hitType == TileType::Steel) {
            const int power = BulletPower(bullet);
            int dealt = 0;
            bool unitDestroyed = false;
            if (HandleSteelHit(map, cellX, cellY, hitX, hitY, bullet.direction, power, dealt, unitDestroyed)) {
                if (!unitDestroyed && bullet.ownerId < kEnemyOwnerIdBase) {
                    // Pedido explicitamente: sonido de "pega y no lo rompe", solo bala de jugador.
                    PlaySound(blockResistSound);
                }
                bullet.health -= dealt;
                if (bullet.health <= 0) {
                    impacts.Spawn(hitX, hitY, bullet.weaponLevel == 4);
                    bullet.alive = false;
                    break;
                }
                bullet.x = newX;
                bullet.y = newY;
                continue;
            }
            // La unidad exacta del impacto ya estaba destruida: la bala sigue de largo.
            bullet.x = newX;
            bullet.y = newY;
            continue;
        }

        if (hitType == TileType::Base) {
            // El aguila: un solo impacto (de cualquier potencia) la
            // destruye, sin capas ni salud como ladrillo/hierro, y siempre
            // frena a la bala aunque le quedara salud (el fin de la partida
            // no espera a que se agote). Pedido explicitamente: la misma
            // animacion chica de explosion que usa Game::DestroyTank para
            // un tanque, centrada en la celda; el frame de "destruida" (ver
            // TileType::BaseDestroyed) ya queda debajo desde este mismo
            // instante, asi que es lo unico que se ve una vez que la
            // explosion termina de reproducirse. Quien llama (Game::Update)
            // detecta que la celda dejo de ser Base y termina la partida.
            map.At(cellX, cellY).type = TileType::BaseDestroyed;
            specialExplosions.Spawn(static_cast<float>(cellX) + 0.5f, static_cast<float>(cellY) + 0.5f, /*nativeScale=*/true);
            PlaySound(explosionSound);
            impacts.Spawn(hitX, hitY, bullet.weaponLevel == 4);
            bullet.alive = false;
            break;
        }

        if (TileBlocksShots(hitType)) {
            // Obstaculo generico sin nocion de salud: siempre frena del
            // todo, como antes.
            if (bullet.ownerId < kEnemyOwnerIdBase) {
                PlaySound(blockResistSound); // pedido explicitamente: solo bala de jugador
            }
            impacts.Spawn(hitX, hitY, bullet.weaponLevel == 4);
            bullet.alive = false;
            break;
        }

        bullet.x = newX;
        bullet.y = newY;
        } // fin de los sub-pasos de esta bala
    }

    // Se solaparon en algun momento de este frame en un eje: o terminaron
    // cerca, o venian de lados opuestos y se cruzaron en el medio (a
    // velocidades altas, p.ej. dos especiales a 24 celdas/seg cerrando a 48
    // combinadas, pueden pasar de largo una a la otra sin que al final del
    // paso queden nunca dentro del radio de choque).
    auto axisCrossedOrClose = [](float o1, float n1, float o2, float n2, float radius) {
        const float rel0 = o1 - o2;
        const float rel1 = n1 - n2;
        if ((rel0 > 0.0f) != (rel1 > 0.0f)) {
            return true; // la distancia relativa cambio de signo: paso por 0
        }
        return std::min(std::fabs(rel0), std::fabs(rel1)) <= radius;
    };

    // Dos balas que chocan de frente se anulan mutuamente (seccion 4.5). El
    // disparo especial es distinto: las balas de nivel 1-2 no lo frenan (se
    // destruyen ellas solas, el especial sigue de largo); solo las de nivel
    // 3-4 lo detienen (ahi si se destruyen ambas y el especial explota).
    for (size_t i = 0; i < bullets_.size(); ++i) {
        if (!bullets_[i].alive) {
            continue;
        }
        for (size_t j = i + 1; j < bullets_.size(); ++j) {
            if (!bullets_[j].alive) {
                continue;
            }
            if (!axisCrossedOrClose(preMoveX[i], bullets_[i].x, preMoveX[j], bullets_[j].x, kBulletHitRadius) ||
                !axisCrossedOrClose(preMoveY[i], bullets_[i].y, preMoveY[j], bullets_[j].y, kBulletHitRadius)) {
                continue;
            }

            Bullet& a = bullets_[i];
            Bullet& b = bullets_[j];
            const float midX = (a.x + b.x) * 0.5f;
            const float midY = (a.y + b.y) * 0.5f;

            if (a.isSpecial != b.isSpecial) {
                Bullet& special = a.isSpecial ? a : b;
                Bullet& normal = a.isSpecial ? b : a;
                if (normal.weaponLevel >= 3) {
                    // La detiene: se destruyen las dos y el especial explota ahi.
                    TriggerSpecialExplosion(map, midX, midY, special.ownerId, specialExplosions, explosionEvents, explosionSound);
                    normal.alive = false;
                    special.alive = false;
                } else {
                    // Nivel 1-2: se destruye solo ella, el especial sigue de largo.
                    impacts.Spawn(normal.x, normal.y, normal.weaponLevel == 4);
                    normal.alive = false;
                }
                continue;
            }

            if (a.isSpecial && b.isSpecial) {
                // Caso raro (cada jugador solo tiene un especial a la vez):
                // si chocan entre si, explotan las dos, cada una con su
                // propio dueño para el dano directo que corresponda.
                TriggerSpecialExplosion(map, midX, midY, a.ownerId, specialExplosions, explosionEvents, explosionSound);
                TriggerSpecialExplosion(map, midX, midY, b.ownerId, specialExplosions, explosionEvents, explosionSound);
                a.alive = false;
                b.alive = false;
                continue;
            }

            const bool bigExplosion = a.weaponLevel == 4 || b.weaponLevel == 4;
            impacts.Spawn(midX, midY, bigExplosion);
            a.alive = false;
            b.alive = false;
        }
    }

    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Bullet& b) { return !b.alive; }), bullets_.end());
}

namespace {
// Resta a la salud propia de la bala lo que "resolve" diga que el objetivo
// absorbio de este impacto: si se agota, la bala se destruye (recien ahi se
// dispara la animacion de impacto, pedido explicitamente: solo cuando la
// bala se destruye de verdad, no en cada golpe que sobrevive); si le queda
// salud, sigue de largo, reubicada justo pasando el borde de la caja del
// tanque en su direccion de avance para no volver a chocar contra el MISMO
// tanque al cuadro siguiente mientras sigan solapados.
void ResolveBulletVsTankBox(Bullet& bullet, int dealt, float left, float right, float top, float bottom, BulletImpactSystem& impacts) {
    bullet.health -= dealt;
    if (bullet.health <= 0) {
        impacts.Spawn(bullet.x, bullet.y, bullet.weaponLevel == 4);
        bullet.alive = false;
        return;
    }
    switch (bullet.direction) {
        case Direction::Right: bullet.x = right + 0.01f; break;
        case Direction::Left:  bullet.x = left - 0.01f;  break;
        case Direction::Down:  bullet.y = bottom + 0.01f; break;
        case Direction::Up:    bullet.y = top - 0.01f;   break;
    }
}
} // namespace

bool BulletSystem::KillBulletsHittingBox(int excludeOwnerId, float left, float right, float top, float bottom, BulletImpactSystem& impacts, const TankHitResolver& resolve) {
    bool hitAny = false;
    for (Bullet& bullet : bullets_) {
        // Solo balas de OTRO JUGADOR (pedido explicitamente: el fuego amigo
        // es entre jugadores nada mas). Antes solo excluia al propio dueño,
        // asi que de paso agarraba tambien balas enemigas que pasaban por
        // esta misma caja, aplicandoles el modo de fuego amigo (paralizar/
        // danar segun friendlyFireMode_) en vez de dejarlas para el camino
        // dedicado a enemigos (dano fijo, ver KillEnemyBulletsHittingBox).
        if (!bullet.alive || bullet.isSpecial || bullet.ownerId == excludeOwnerId || bullet.ownerId >= kEnemyOwnerIdBase) {
            continue;
        }
        if (bullet.x >= left && bullet.x <= right && bullet.y >= top && bullet.y <= bottom) {
            const int dealt = resolve(bullet.weaponLevel, bullet.isSpecial);
            ResolveBulletVsTankBox(bullet, dealt, left, right, top, bottom, impacts);
            hitAny = true;
        }
    }
    return hitAny;
}

bool BulletSystem::KillEnemyBulletsHittingBox(float left, float right, float top, float bottom, BulletImpactSystem& impacts, const TankHitResolver& resolve) {
    bool hitAny = false;
    for (Bullet& bullet : bullets_) {
        if (!bullet.alive || bullet.isSpecial || bullet.ownerId < kEnemyOwnerIdBase) {
            continue;
        }
        if (bullet.x >= left && bullet.x <= right && bullet.y >= top && bullet.y <= bottom) {
            const int dealt = resolve(bullet.weaponLevel, bullet.isSpecial);
            ResolveBulletVsTankBox(bullet, dealt, left, right, top, bottom, impacts);
            hitAny = true;
        }
    }
    return hitAny;
}

bool BulletSystem::KillPlayerBulletsHittingBox(float left, float right, float top, float bottom, BulletImpactSystem& impacts, std::vector<int>& outShooterOwnerIds, const std::function<int(int weaponLevel, bool isSpecial, int shooterOwnerId)>& resolve) {
    bool hitAny = false;
    for (Bullet& bullet : bullets_) {
        if (!bullet.alive || bullet.isSpecial || bullet.ownerId >= kEnemyOwnerIdBase) {
            continue;
        }
        if (bullet.x >= left && bullet.x <= right && bullet.y >= top && bullet.y <= bottom) {
            outShooterOwnerIds.push_back(bullet.ownerId);
            const int dealt = resolve(bullet.weaponLevel, bullet.isSpecial, bullet.ownerId);
            ResolveBulletVsTankBox(bullet, dealt, left, right, top, bottom, impacts);
            hitAny = true;
        }
    }
    return hitAny;
}

} // namespace bc
