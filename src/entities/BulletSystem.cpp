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

} // namespace

bool BulletSystem::HandleBrickHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, bool doubleLayer) {
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
    auto destroyLayer = [&](int layerIndex) {
        if (layerIndex < 0 || layerIndex >= kBrickGridSize) {
            return;
        }
        if (horizontalHit) {
            if (row == 1 || row == 2) {
                destroyedAny |= DestroyUnitSpan(cell, layerIndex, /*fixedIsRow=*/false, 0, kBrickGridSize);
            } else {
                const int firstRow = (row == 0) ? 0 : kHalf;
                destroyedAny |= DestroyUnitSpan(cell, layerIndex, /*fixedIsRow=*/false, firstRow, kHalf);
            }
        } else {
            if (col == 1 || col == 2) {
                destroyedAny |= DestroyUnitSpan(cell, layerIndex, /*fixedIsRow=*/true, 0, kBrickGridSize);
            } else {
                const int firstCol = (col == 0) ? 0 : kHalf;
                destroyedAny |= DestroyUnitSpan(cell, layerIndex, /*fixedIsRow=*/true, firstCol, kHalf);
            }
        }
    };

    // La capa de entrada es la columna/fila en la que esta la bala en este
    // instante (col/row ya reflejan cuanto penetro: si la capa de entrada
    // esta vacia, la bala avanza y la siguiente llamada cae en la capa de al
    // lado, nunca se queda pegada a la ya destruida).
    const int entryLayer = horizontalHit ? col : row;
    destroyLayer(entryLayer);

    if (!destroyedAny) {
        return false; // ahi ya estaba todo destruido: no frena la bala
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
        destroyLayer(entryLayer + step);
    }

    if (cell.BrickFullyDestroyed()) {
        cell.type = TileType::Empty;
    }
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
    return true;
}

void BulletSystem::TriggerSpecialExplosion(TileMap& map, float x, float y, int ownerId, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents) {
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
    explosionEvents.push_back(SpecialExplosionEvent{x, y, kSpecialExplosionRadius, ownerId});
}

void BulletSystem::Update(double dt, TileMap& map, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents) {
    explosionEvents.clear();

    for (Bullet& bullet : bullets_) {
        if (!bullet.alive) {
            continue;
        }

        float dx = 0.0f, dy = 0.0f;
        DirectionVector(bullet.direction, dx, dy);

        const float distance = static_cast<float>(bullet.speed * dt);
        const float newX = bullet.x + dx * distance;
        const float newY = bullet.y + dy * distance;
        const int cellX = static_cast<int>(std::floor(newX));
        const int cellY = static_cast<int>(std::floor(newY));

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
            // El especial no distingue capas ni tipo de bloqueo: cualquier
            // cosa solida lo hace explotar y la explosion se lleva todo.
            if (TileBlocksShots(hitType)) {
                TriggerSpecialExplosion(map, newX, newY, bullet.ownerId, specialExplosions, explosionEvents);
                bullet.alive = false;
            } else {
                bullet.x = newX;
                bullet.y = newY;
            }
            continue;
        }

        if (hitType == TileType::Brick) {
            if (HandleBrickHit(map, cellX, cellY, newX, newY, bullet.direction, bullet.weaponLevel == 4)) {
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
            if (HandleSteelHit(map, cellX, cellY, newX, newY, bullet.direction, bullet.weaponLevel)) {
                impacts.Spawn(newX, newY, bullet.weaponLevel == 4);
                bullet.alive = false;
            } else {
                // La unidad exacta del impacto ya estaba destruida: la bala sigue de largo.
                bullet.x = newX;
                bullet.y = newY;
            }
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

    // Dos balas que chocan de frente se anulan mutuamente (seccion 4.5). El
    // disparo especial no participa: detona solo por choque directo.
    for (size_t i = 0; i < bullets_.size(); ++i) {
        if (!bullets_[i].alive || bullets_[i].isSpecial) {
            continue;
        }
        for (size_t j = i + 1; j < bullets_.size(); ++j) {
            if (!bullets_[j].alive || bullets_[j].isSpecial) {
                continue;
            }
            const float dx = bullets_[i].x - bullets_[j].x;
            const float dy = bullets_[i].y - bullets_[j].y;
            if (std::fabs(dx) <= kBulletHitRadius && std::fabs(dy) <= kBulletHitRadius) {
                const bool bigExplosion = bullets_[i].weaponLevel == 4 || bullets_[j].weaponLevel == 4;
                impacts.Spawn((bullets_[i].x + bullets_[j].x) * 0.5f, (bullets_[i].y + bullets_[j].y) * 0.5f, bigExplosion);
                bullets_[i].alive = false;
                bullets_[j].alive = false;
            }
        }
    }

    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Bullet& b) { return !b.alive; }), bullets_.end());
}

} // namespace bc
