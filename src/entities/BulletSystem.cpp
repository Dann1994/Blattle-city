#include "BulletSystem.h"

#include <algorithm>
#include <cmath>

#include "BulletImpactSystem.h"
#include "SpecialExplosionSystem.h"

namespace bc {

namespace {
constexpr float kBulletHitRadius = 0.2f;     // celdas, para la anulacion bala-contra-bala (seccion 4.5)
constexpr float kSpecialBulletSpeed = 24.0f; // el triple de la bala normal de nivel 1 (8.0)

// Dano por disparo contra hierro, segun el nivel de quien dispara. Con
// kSteelUnitMaxHp = 75 (ver SteelUnit.h), esto hace que romper el bloque
// entero a puro impacto central tome 50/76/10/2 disparos en nivel 1/2/3/4.
constexpr int kSteelDamageByLevel[4] = {3, 2, 15, 75};
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

// Bloques pegados uno al lado del otro: si el impacto cae en la franja
// angosta pegada al borde compartido con la celda vecina (no toda la mitad
// "esquina", solo bien cerca de la costura) y esa vecina tambien es
// destructible, tambien le pega a su unidad mas cercana a la costura.
void CheckSeamGraze(TileMap& map, int cellX, int cellY, bool horizontalHit, float fracX, float fracY, int weaponLevel) {
    constexpr float kSeamBand = 0.15f;
    const int steelDamage = kSteelDamageByLevel[weaponLevel - 1];
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

bool BulletSystem::HandleBrickHit(TileMap& map, int cellX, int cellY, float oldX, float oldY, float hitX, float hitY, Direction hitFrom, bool doubleLayer, int weaponLevel) {
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

    // Capas que la bala atraveso este frame, desde donde estaba hasta donde
    // penetro (no solo la de llegada): con velocidades altas (nivel 2, cuyo
    // paso por frame es mas ancho que una capa de 0.25) un solo frame puede
    // saltearse una capa entera de otro modo, y si esa capa ya no vuelve a
    // chequearse queda indestructible para siempre. Se para en la primera
    // capa viva que encuentra en ese rango; si estaban todas vacias, la bala
    // sigue de largo.
    const float travelOld = std::clamp(horizontalHit ? (oldX - static_cast<float>(cellX)) : (oldY - static_cast<float>(cellY)), 0.0f, 0.999f);
    const int layerFrom = std::clamp(static_cast<int>(travelOld * kBrickGridSize), 0, kBrickGridSize - 1);
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

    if (doubleLayer) {
        // Nivel 4: rompe tambien la siguiente capa en la direccion de avance.
        int step = 0;
        switch (hitFrom) {
            case Direction::Right: step = 1; break;
            case Direction::Left:  step = -1; break;
            case Direction::Down:  step = 1; break;
            case Direction::Up:    step = -1; break;
        }
        destroyLayer(stoppedAtLayer + step);
    }

    if (cell.BrickFullyDestroyed()) {
        cell.type = TileType::Empty;
    }

    CheckSeamGraze(map, cellX, cellY, horizontalHit, fracX, fracY, weaponLevel);
    return true;
}

bool BulletSystem::HandleSteelHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, int weaponLevel) {
    Cell& cell = map.At(cellX, cellY);

    const float fracX = hitX - static_cast<float>(cellX);
    const float fracY = hitY - static_cast<float>(cellY);
    const bool horizontalHit = (hitFrom == Direction::Left || hitFrom == Direction::Right);

    // Capa de entrada real (0 o 1), calculada con la posicion actual de la
    // bala (igual que en ladrillo): si esa capa ya esta vacia, la siguiente
    // llamada cae sola en la capa de al lado.
    const int entryLayer = std::clamp(static_cast<int>((horizontalHit ? fracX : fracY) * kSteelGridSize), 0, kSteelGridSize - 1);

    // El otro eje decide centro (capa entera, 2 unidades) vs punta (1 unidad).
    const float otherFrac = horizontalHit ? fracY : fracX;
    constexpr float kCenterBandMin = 0.25f;
    constexpr float kCenterBandMax = 0.75f;
    const bool centerHit = otherFrac >= kCenterBandMin && otherFrac <= kCenterBandMax;

    const int damage = kSteelDamageByLevel[weaponLevel - 1];
    bool hitAliveUnit = false;

    auto damageUnit = [&](int row, int col) {
        SteelUnit& unit = cell.steelUnits[row * kSteelGridSize + col];
        if (!unit.alive) {
            return;
        }
        hitAliveUnit = true;
        unit.hp -= damage;
        if (unit.hp <= 0) {
            unit.alive = false;
        }
    };

    if (centerHit) {
        for (int i = 0; i < kSteelGridSize; ++i) {
            damageUnit(horizontalHit ? i : entryLayer, horizontalHit ? entryLayer : i);
        }
    } else {
        const int tipIndex = (otherFrac < 0.5f) ? 0 : 1;
        damageUnit(horizontalHit ? tipIndex : entryLayer, horizontalHit ? entryLayer : tipIndex);
    }

    if (!hitAliveUnit) {
        return false; // ahi ya estaba vacio: no frena la bala
    }

    if (cell.SteelFullyDestroyed()) {
        cell.type = TileType::Empty;
    }

    CheckSeamGraze(map, cellX, cellY, horizontalHit, fracX, fracY, weaponLevel);
    return true;
}

void BulletSystem::TriggerSpecialExplosion(TileMap& map, float x, float y, int ownerId, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents, int directHitOwnerId) {
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
            } else if (cell.type == TileType::Steel || cell.type == TileType::Base) {
                cell.type = TileType::Empty;
            }
        }
    }

    specialExplosions.Spawn(x, y);
    explosionEvents.push_back(SpecialExplosionEvent{x, y, kSpecialExplosionRadius, ownerId, directHitOwnerId});
}

void BulletSystem::Update(double dt, TileMap& map, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents, const std::vector<TankCombatState>& tanks, std::vector<SpecialDirectKillEvent>& directKillEvents) {
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

    for (Bullet& bullet : bullets_) {
        if (!bullet.alive) {
            continue;
        }

        float dx = 0.0f, dy = 0.0f;
        DirectionVector(bullet.direction, dx, dy);

        const float distance = static_cast<float>(bullet.speed * dt);
        const float newX = bullet.x + dx * distance;
        const float newY = bullet.y + dy * distance;
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
        constexpr float kBulletHitHalfWidth = 0.09f;
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

        // Si la bala recien entra a esta celda (la posicion de ANTES de
        // este paso todavia no habia cruzado el borde de entrada), la capa
        // que "toca" es siempre la de entrada, nunca el punto al que
        // sobrepaso el movimiento de este frame. Con velocidades altas
        // (p.ej. nivel 2) un solo paso puede ser mas ancho que una capa
        // entera, y disparando pegado al bloque la bala ya arranca en el
        // borde mismo (mismo indice de celda desde el primer frame), asi
        // que comparar indices de celda no alcanza: hay que comparar contra
        // el borde real. hitX/hitY son solo para decidir la capa/unidad de
        // ladrillo o hierro; newX/newY (la posicion real) se siguen usando
        // para todo lo demas.
        float hitX = newX;
        float hitY = newY;
        if (dx > 0.0f && bullet.x <= static_cast<float>(cellX)) {
            hitX = static_cast<float>(cellX);
        } else if (dx < 0.0f && bullet.x >= static_cast<float>(cellX) + 1.0f) {
            hitX = static_cast<float>(cellX) + 0.999f;
        }
        if (dy > 0.0f && bullet.y <= static_cast<float>(cellY)) {
            hitY = static_cast<float>(cellY);
        } else if (dy < 0.0f && bullet.y >= static_cast<float>(cellY) + 1.0f) {
            hitY = static_cast<float>(cellY) + 0.999f;
        }

        if (!map.InBounds(cellX, cellY)) {
            // Borde del escenario: no hay tile ahi, pero igual es un impacto.
            const float edgeX = std::clamp(newX, 0.0f, static_cast<float>(map.Width()));
            const float edgeY = std::clamp(newY, 0.0f, static_cast<float>(map.Height()));
            if (bullet.isSpecial) {
                TriggerSpecialExplosion(map, edgeX, edgeY, bullet.ownerId, specialExplosions, explosionEvents);
            } else {
                impacts.Spawn(edgeX, edgeY, bullet.weaponLevel == 4);
            }
            bullet.alive = false;
            continue;
        }

        const TileType hitType = map.At(cellX, cellY).type;

        if (bullet.isSpecial) {
            // Choque directo contra un tanque (que no sea el propio dueño):
            // nivel 1-3 sin escudo se destruye directo y la bala sigue de
            // largo (no explota); con escudo o nivel 4, la bala explota.
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
                directKillEvents.push_back(SpecialDirectKillEvent{hitTank->ownerId});
                bullet.x = newX;
                bullet.y = newY;
                continue;
            }

            // El especial no distingue capas ni tipo de bloqueo: cualquier
            // cosa solida lo hace explotar y la explosion se lleva todo.
            if (hitTank != nullptr || TileBlocksShots(hitType)) {
                const int directHitOwnerId = (hitTank != nullptr) ? hitTank->ownerId : -1;
                TriggerSpecialExplosion(map, newX, newY, bullet.ownerId, specialExplosions, explosionEvents, directHitOwnerId);
                bullet.alive = false;
            } else {
                bullet.x = newX;
                bullet.y = newY;
            }
            continue;
        }

        if (hitType == TileType::Brick) {
            if (HandleBrickHit(map, cellX, cellY, bullet.x, bullet.y, hitX, hitY, bullet.direction, bullet.weaponLevel == 4, bullet.weaponLevel)) {
                impacts.Spawn(newX, newY, bullet.weaponLevel == 4);
                bullet.alive = false;
            } else {
                // La unidad exacta del impacto ya estaba destruida: la bala sigue de largo.
                bullet.x = newX;
                bullet.y = newY;
            }
            continue;
        }

        if (hitType == TileType::Steel) {
            if (HandleSteelHit(map, cellX, cellY, hitX, hitY, bullet.direction, bullet.weaponLevel)) {
                impacts.Spawn(newX, newY, bullet.weaponLevel == 4);
                bullet.alive = false;
            } else {
                // La unidad exacta del impacto ya estaba destruida: la bala sigue de largo.
                bullet.x = newX;
                bullet.y = newY;
            }
            continue;
        }

        if (hitType == TileType::Base) {
            // El aguila: un solo impacto (de cualquier nivel de arma) la
            // destruye, sin capas ni HP como ladrillo/hierro. Quien llama
            // (Game::Update) detecta que la celda dejo de ser Base y termina
            // la partida.
            map.At(cellX, cellY).type = TileType::Empty;
            impacts.Spawn(newX, newY, bullet.weaponLevel == 4);
            bullet.alive = false;
            continue;
        }

        if (TileBlocksShots(hitType)) {
            impacts.Spawn(newX, newY, bullet.weaponLevel == 4);
            bullet.alive = false;
            continue;
        }

        bullet.x = newX;
        bullet.y = newY;
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
                    TriggerSpecialExplosion(map, midX, midY, special.ownerId, specialExplosions, explosionEvents);
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
                TriggerSpecialExplosion(map, midX, midY, a.ownerId, specialExplosions, explosionEvents);
                TriggerSpecialExplosion(map, midX, midY, b.ownerId, specialExplosions, explosionEvents);
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

bool BulletSystem::KillBulletsHittingBox(int excludeOwnerId, float left, float right, float top, float bottom, BulletImpactSystem& impacts, std::vector<int>& outShooterWeaponLevels) {
    bool hitAny = false;
    for (Bullet& bullet : bullets_) {
        if (!bullet.alive || bullet.isSpecial || bullet.ownerId == excludeOwnerId) {
            continue;
        }
        if (bullet.x >= left && bullet.x <= right && bullet.y >= top && bullet.y <= bottom) {
            impacts.Spawn(bullet.x, bullet.y, bullet.weaponLevel == 4);
            outShooterWeaponLevels.push_back(bullet.weaponLevel);
            bullet.alive = false;
            hitAny = true;
        }
    }
    return hitAny;
}

bool BulletSystem::KillEnemyBulletsHittingBox(float left, float right, float top, float bottom, BulletImpactSystem& impacts) {
    bool hitAny = false;
    for (Bullet& bullet : bullets_) {
        if (!bullet.alive || bullet.isSpecial || bullet.ownerId < kEnemyOwnerIdBase) {
            continue;
        }
        if (bullet.x >= left && bullet.x <= right && bullet.y >= top && bullet.y <= bottom) {
            impacts.Spawn(bullet.x, bullet.y, bullet.weaponLevel == 4);
            bullet.alive = false;
            hitAny = true;
        }
    }
    return hitAny;
}

} // namespace bc
