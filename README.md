# Battle City Clon

Clon didáctico de *Battle City* (NES, 1985) ampliado a 4 jugadores, LAN y modos
adicionales. Ver el documento de diseño completo en
[Documentaciones/Documento_de_Diseno.md](Documentaciones/Documento_de_Diseno.md).

## Stack

- C++17/20
- [raylib](https://www.raylib.com/) 5.x — ventana, render, input, audio
- [ENet](http://enet.bespin.org/) — networking UDP (se agrega en la Fase 7)
- [nlohmann/json](https://github.com/nlohmann/json) — formato de nivel
- CMake ≥ 3.20 (`FetchContent` descarga las dependencias, no hace falta instalarlas a mano)

## Estructura

```
/src           -> código del juego (se compila al ejecutable final)
  /core         -> bucle principal, tiempo, configuración
  /world        -> mapa de tiles, colisiones, destrucción de terreno
  /entities     -> tanque, bala, power-up, base
  /ai           -> máquina de estados de enemigos
  /modes        -> reglas de cada modo de juego
  /net          -> cliente/servidor ENet, protocolo, serialización
  /render       -> dibujo con raylib, cámara, HUD
/tools/level_editor -> editor de niveles standalone (no se empaqueta con el juego)
/shared        -> código común a /src y /tools (formato de nivel, tipos de tile)
/assets        -> sprites, sonidos, fuentes, mapas (JSON)
```

## Build

Usa el mismo toolchain que SuperMarioClone: MSYS2/MinGW64 (`C:\msys64\mingw64\bin`, incluye `cmake`, `ninja` y `g++`), que no está en el PATH del sistema.

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

El ejecutable queda en `build/bin/battlecity.exe`.

## Controles

- Jugador 1: `W A S D` para moverse (sin diagonales), `Ctrl izquierdo` dispara.
- `R`: repite el destello de aparición en la posición actual del tanque (botón de prueba, no es una mecánica del juego final).

## Estado

Fases 0-2 completas (setup, movimiento/colisión, disparo y destrucción de terreno).
Tanque protagonista con niveles de arma 1-4 (power-up Estrella) y destello de
aparición. Ver Documentaciones/Documento_de_Diseno.md sección 14 para el plan
completo por fases.
