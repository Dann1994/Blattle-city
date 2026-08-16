#include "BulletSystem.h"

#include <algorithm>
#include <cmath>

#include "BulletImpactSystem.h"

namespace bc {

namespace {
constexpr float kBulletHitRadius = 0.2f; // celdas, para la anulacion bala-contra-bala (seccion 4.5)
}

bool BulletSystem::TryShoot(int ownerId, float muzzleX, float muzzleY, Direction direction, float speed, bool canDestroySteel, int maxPerOwner) {
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
    bullet.canDestroySteel = canDestroySteel;
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

bool BulletSystem::HandleBrickHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom) {
    Cell& cell = map.At(cellX, cellY);

    const float fracX = hitX - static_cast<float>(cellX);
    const float fracY = hitY - static_cast<float>(cellY);
    const int col = std::clamp(static_cast<int>(fracX * kBrickGridSize), 0, kBrickGridSize - 1);
    const int row = std::clamp(static_cast<int>(fracY * kBrickGridSize), 0, kBrickGridSize - 1);

    constexpr int kHalf = kBrickGridSize / 2;
    bool destroyedAny = false;

    if (hitFrom == Direction::Left || hitFrom == Direction::Right) {
        // La "capa" golpeada es la columna en la que esta la bala en este
        // instante (col ya refleja cuanto penetro: si la capa de entrada
        // esta vacia, la bala avanza y la siguiente llamada cae en la
        // columna de al lado, nunca se queda pegada a la capa de entrada).
        // La fila decide cuanto de esa capa se destruye: centro (filas 1,2)
        // = capa entera (4 unidades); esquina (filas 0,3) = mitad de esa
        // capa, del lado (arriba/abajo) donde pego.
        if (row == 1 || row == 2) {
            destroyedAny = DestroyUnitSpan(cell, col, /*fixedIsRow=*/false, 0, kBrickGridSize);
        } else {
            const int firstRow = (row == 0) ? 0 : kHalf;
            destroyedAny = DestroyUnitSpan(cell, col, /*fixedIsRow=*/false, firstRow, kHalf);
        }
    } else {
        // Simetrico: la capa es la fila en la que esta la bala ahora mismo.
        if (col == 1 || col == 2) {
            destroyedAny = DestroyUnitSpan(cell, row, /*fixedIsRow=*/true, 0, kBrickGridSize);
        } else {
            const int firstCol = (col == 0) ? 0 : kHalf;
            destroyedAny = DestroyUnitSpan(cell, row, /*fixedIsRow=*/true, firstCol, kHalf);
        }
    }

    if (!destroyedAny) {
        return false; // ahi ya estaba todo destruido: no frena la bala
    }

    if (cell.BrickFullyDestroyed()) {
        cell.type = TileType::Empty;
    }
    return true;
}

void BulletSystem::Update(double dt, TileMap& map, BulletImpactSystem& impacts) {
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
            bullet.alive = false;
            continue;
        }

        const TileType hitType = map.At(cellX, cellY).type;
        if (hitType == TileType::Brick) {
            if (HandleBrickHit(map, cellX, cellY, newX, newY, bullet.direction)) {
                impacts.Spawn(newX, newY);
                bullet.alive = false;
            } else {
                // La unidad exacta del impacto ya estaba destruida: la bala sigue de largo.
                bullet.x = newX;
                bullet.y = newY;
            }
            continue;
        }

        if (TileBlocksShots(hitType)) {
            if (hitType == TileType::Steel && bullet.canDestroySteel) {
                map.At(cellX, cellY).type = TileType::Empty;
            }
            impacts.Spawn(newX, newY);
            bullet.alive = false;
            continue;
        }

        bullet.x = newX;
        bullet.y = newY;
    }

    // Dos balas que chocan de frente se anulan mutuamente (seccion 4.5).
    for (size_t i = 0; i < bullets_.size(); ++i) {
        if (!bullets_[i].alive) {
            continue;
        }
        for (size_t j = i + 1; j < bullets_.size(); ++j) {
            if (!bullets_[j].alive) {
                continue;
            }
            const float dx = bullets_[i].x - bullets_[j].x;
            const float dy = bullets_[i].y - bullets_[j].y;
            if (std::fabs(dx) <= kBulletHitRadius && std::fabs(dy) <= kBulletHitRadius) {
                impacts.Spawn((bullets_[i].x + bullets_[j].x) * 0.5f, (bullets_[i].y + bullets_[j].y) * 0.5f);
                bullets_[i].alive = false;
                bullets_[j].alive = false;
            }
        }
    }

    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Bullet& b) { return !b.alive; }), bullets_.end());
}

} // namespace bc
