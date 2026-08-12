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

Requiere un compilador de C++ y CMake instalados (no vienen incluidos en este repo).

```bash
cmake -B build -S .
cmake --build build --config Release
```

El ejecutable queda en `build/bin/`.

## Estado

Fase 0 en curso — ver Documentaciones/Documento_de_Diseno.md sección 14 para el plan completo por fases.
