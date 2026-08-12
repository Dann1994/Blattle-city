# Battle City Clon

Documento de diseño y especificación técnica — para implementación · Agosto 2026

## 1\. Objetivo del documento

Este documento describe, con suficiente detalle técnico, cómo funciona *Battle City* (NES, Namco, 1985) y especifica el diseño de un clon didáctico ampliado: mismo núcleo jugable, pero con soporte para **hasta 4 jugadores simultáneos**, **juego en red local (LAN)** y **modos de juego adicionales** al clásico. Está pensado para entregarse a un agente/equipo de programación como especificación de partida: define mecánicas, datos, formatos y una arquitectura técnica concreta (C\+\+ nativo, sin motor de juego), además de un plan de implementación por fases.

No es un documento de arte ni de negocio: se enfoca en reglas de juego, datos y arquitectura de software.

## 2\. Resumen del proyecto

- **Género:** arena shooter top\-down por tiles, con destrucción de terreno.
- **Base:** clon fiel de *Battle City* (NES/Famicom, 1985) en sus reglas centrales.
- **Ampliaciones sobre el original:**
  - De 2 a **4 jugadores simultáneos** en la misma pantalla (local).
  - **Juego en red LAN** (varias PCs conectadas a la misma red local).
  - **Modos de juego adicionales** más allá de la campaña clásica cooperativa.
  - Soporte de **pantalla panorámica (widescreen)** con escenarios más anchos que el 13×13 clásico (ver [sección 4.1](#grilla) y [sección 12.6](#camara)).
- **Plataforma inicial:** Windows, aplicación nativa **sin motor de juego** (no Unity/Godot/Unreal).
- **Plataforma futura:** Android (se documentan las decisiones técnicas que dejan el camino abierto, ver [sección 15](#android)).
- **Stack recomendado:** C\+\+17/20 \+ [raylib](https://www.raylib.com/) (gráficos/input/audio) \+ [ENet](http://enet.bespin.org/) (networking UDP confiable) \+ CMake (build multiplataforma). Se justifica en la [sección 12](#arquitectura-tecnica).

## 3\. El juego original: contexto

*Battle City* es un juego de arcade de Namco de 1985, versión evolucionada de su propio *Tank Battalion* (1980), portado y popularizado principalmente en la Famicom/NES. El jugador controla un tanque en una arena vista desde arriba, debe destruir tanques enemigos y, sobre todo, **defender su base** (representada por un águila/fénix) de la invasión enemiga. Introdujo terrenos destructibles, power\-ups y un modo cooperativo a 2 jugadores, y fue pionero al incluir un **editor de niveles** ("Construction Mode") integrado en el cartucho. La versión NES incluye 35 etapas predefinidas; al completarlas, el juego reinicia con dificultad creciente.

## 4\. Mecánicas fundamentales

### 4\.1 Grilla y escala

El campo de juego clásico es una **grilla de 13×13 celdas**. En la implementación original cada celda mide 16×16 píxeles (en algunas descripciones técnicas se documenta como 36×36 en re\-implementaciones a mayor resolución; lo importante es la proporción y la subdivisión, no el valor absoluto de píxeles). Cada celda de terreno destructible se subdivide internamente en un **bloque de 2×2 sub\-celdas**, y el daño de un disparo elimina solo la porción de la sub\-celda que impacta según la dirección del disparo (por ejemplo, un disparo desde la izquierda destruye la mitad izquierda del bloque). Esta subdivisión es la que da a *Battle City* su sensación característica de terreno "mordido" en vez de bloques que desaparecen enteros.

**Mapas panorámicos (widescreen):** el motor no debe asumir una grilla cuadrada fija. Se recomienda mantener el **alto en 13 celdas** (fidelidad con el original, cómodo en vertical para todas las resoluciones) y dejar el **ancho como parámetro por nivel**, de forma que los escenarios pensados para pantalla panorámica puedan ser más anchos que altos (por ejemplo 21, 23 o 27 celdas de ancho para relaciones de aspecto 16:9 o más anchas). El mapa clásico de 13×13 sigue siendo válido como caso particular. El manejo en cámara de estos mapas más anchos se detalla en la [sección 12.6](#camara).

Recomendación de representación interna: matriz de `ancho × alto` celdas (no necesariamente cuadrada), cada celda como una **máscara de 2×2 bits** de "material presente", para reproducir la destrucción parcial de forma simple sin importar las dimensiones del mapa.

### 4\.2 Tipos de terreno

| Terreno | Comportamiento |
| --- | --- |
| **Ladrillo (brick)** | Destructible por balas de cualquier tanque, se destruye por sub\-bloques (ver 4.1). Bloquea movimiento y disparos. |
| **Acero (steel)** | Indestructible por balas normales; solo se destruye con el power\-up de granada o, en el original, no se destruye nunca por el jugador — es un obstáculo permanente salvo mecánicas especiales. Bloquea movimiento y disparos. |
| **Agua** | Bloquea el movimiento de tanques, pero **no bloquea las balas** (pasan por encima). Visualmente distinta, sin destrucción. |
| **Arbusto/follaje (trees)** | No bloquea movimiento ni disparos, pero **oculta visualmente** a los tanques que pasan por debajo (capa de render por encima de los sprites). |
| **Hielo** | No bloquea nada, pero vuelve el movimiento del tanque **resbaladizo** (mantiene inercia/dirección tras soltar el input). |
| **Base / Águila** | Objetivo a proteger. Si un enemigo la alcanza y la destruye, la partida (o la vida del equipo) termina inmediatamente, sin importar las vidas restantes de los jugadores. |

### 4\.3 Tanques del jugador

- Movimiento en 4 direcciones (arriba/abajo/izquierda/derecha), **sin diagonales**; el tanque rota su sprite hacia la última dirección presionada incluso si está bloqueado.
- Disparo de un proyectil a la vez por defecto (ver niveles de mejora abajo); el jugador no puede tener dos balas propias en pantalla simultáneamente salvo que haya mejorado su arma.
- **Niveles de mejora vía power\-up "Estrella"** (acumulables, hasta 4 niveles):

| Nivel | Efecto |
| --- | --- |
| 1 (base) | Velocidad de bala estándar, un disparo a la vez. |
| 2 | Bala más rápida. |
| 3 | Doble disparo (dos balas propias simultáneas); a partir de este nivel el tanque también puede destruir muros de **acero**. |
| 4 | Balas de máxima potencia/velocidad, mantiene doble disparo. |

- Al perder una vida, el jugador **reinicia en nivel de mejora base** (regla clásica; puede exponerse como opción configurable).
- Colisión tanque\-tanque: los tanques (propios y enemigos) se bloquean entre sí, no se atraviesan.

### 4\.4 Base / Águila

Ubicada en el borde inferior del mapa, protegida por defecto con muros de ladrillo en forma de "U" invertida. Mecánicas clave:

- Si es destruida → fin de partida inmediato (en modo campaña) independientemente de las vidas restantes de los jugadores.
- El power\-up **"Pala"** convierte temporalmente los muros de ladrillo alrededor de la base en muros de **acero** (protección reforzada), y suele revertir a ladrillo tras un tiempo o al finalizar la oleada.

### 4\.5 Colisión y destrucción

- Las balas viajan en línea recta a velocidad constante hasta chocar con un obstáculo, otro tanque, u otra bala (dos balas que chocan de frente se anulan mutuamente).
- El ladrillo se destruye por sub\-bloque impactado (ver 4.1); el acero requiere power\-up de granada o disparo de nivel 3\+ para destruirse (según se decida en balance, ver [sección 10](#modos)).
- Los arbustos no generan colisión, solo oclusión visual (se dibujan en una capa por encima de tanques/balas).

## 5\. Enemigos

Cada escenario clásico enfrenta al jugador (o equipo) contra **20 tanques enemigos** que aparecen en oleadas desde puntos fijos de spawn (típicamente arriba\-izquierda, arriba\-centro y arriba\-derecha del mapa), con un límite de tanques simultáneos en pantalla (en el original, hasta 4 a la vez) para controlar la dificultad.

| Tipo | Velocidad | Resistencia | Comportamiento |
| --- | --- | --- | --- |
| **Básico** | Lenta | 1 impacto | IA simple, dispara ocasionalmente, poco amenazante. |
| **Rápido** | Alta | 1 impacto | Frágil pero difícil de interceptar por su velocidad; prioriza avanzar hacia la base. |
| **Potencia (power)** | Media\-alta | 1 impacto | Dispara con la cadencia/potencia equivalente al nivel 3 del jugador (doble disparo). |
| **Blindado (armor)** | Muy lenta | 4 impactos | El más resistente; requiere hasta 4 disparos normales. Su lentitud lo hace poco amenazante para la base pero peligroso cuerpo a cuerpo. |

- Uno de cada cierto número de enemigos aparece **parpadeando en colores**\: al destruirlo suelta un power\-up aleatorio en su lugar.
- Dificultad progresiva: a medida que avanzan las etapas aumenta la proporción de tanques rápidos/blindados y la frecuencia de spawns simultáneos.
- La IA recomendada para el clon: máquina de estados simple por tanque — *patrullar hacia un objetivo (base o zona aleatoria) → disparar si hay línea de tiro despejada hacia un jugador o la base → esquivar/rotar si choca contra un obstáculo N frames seguidos*. Es suficiente para reproducir la sensación del original sin pathfinding complejo.

## 6\. Power\-ups

Aparecen en una posición aleatoria del mapa al destruir un tanque "parpadeante", permanecen unos segundos y desaparecen si no se recogen.

| Ícono | Efecto |
| --- | --- |
| **Estrella** | Sube un nivel de mejora del arma del tanque (hasta nivel 4, ver 4.3). |
| **Granada** | Destruye instantáneamente a todos los tanques enemigos en pantalla; otorga puntaje fijo. |
| **Pala** | Refuerza temporalmente los muros de la base (ladrillo → acero). |
| **Reloj** | Congela a todos los tanques enemigos (incluidos los que aparezcan durante el efecto) por un tiempo breve. |
| **Casco** | Otorga invencibilidad temporal al tanque que lo recoge. |
| **Tanque** | Otorga una vida extra. |

Diseño recomendado para el clon: cada power\-up es un componente de datos (`tipo`, `duración`, `efecto`) aplicado a través de un sistema de efectos temporales sobre la entidad tanque, para poder añadir nuevos power\-ups sin tocar el core.

## 7\. Puntaje, vidas y progresión

- Cada jugador comienza con un número fijo de vidas (configurable; el original usa 2\-3 según versión) y gana una vida extra al alcanzar un umbral de puntos (20.000 en el original) o al recoger el power\-up "Tanque".
- Puntaje por tipo de enemigo destruido, proporcional a su dificultad (el blindado otorga más puntos que el básico).
- La campaña clásica se estructura en **etapas numeradas** (35 en el original); cada una define su propio mapa y termina cuando se destruyen los 20 tanques enemigos de esa oleada, sin que la base haya sido destruida.
- Game over si: la base es destruida, o todos los jugadores pierden todas sus vidas (según se configure para cooperativo: puede ser "muerte de equipo" o "revivir mientras quede algún compañero con vidas", a definir como opción de dificultad).

## 8\. Niveles y editor

El original incluye un **"Construction Mode"**\: un editor de niveles integrado que permite colocar cada tipo de terreno celda por celda y guardar el resultado. Para el clon se recomienda:

- **Formato de nivel**\: archivo de texto/JSON con una matriz de `ancho × alto` (13×13 para el estilo clásico; más ancha para escenarios panorámicos, ver [sección 4.1](#grilla)) donde cada celda es un código de terreno (`.` vacío, `B` ladrillo, `S` acero, `W` agua, `T` arbusto, `I` hielo, `E` base/águila) más metadatos del nivel (número de tanques por tipo, puntos de spawn de jugadores y enemigos, modo de juego compatible, relación de aspecto recomendada).
- Ejemplo simplificado de un mapa clásico (13×13):

```json
{
  "width": 13,
  "height": 13,
  "tiles": [
    "BBB..SSS..BBB",
    ".............",
    "....TTT......"
  ],
  "player_spawns": [[4,12],[6,12],[8,12],[10,12]],
  "enemy_spawns": [[0,0],[6,0],[12,0]],
  "enemy_wave": {"basic": 8, "fast": 6, "power": 4, "armor": 2},
  "base_position": [6,12]
}
```

- Ejemplo de un mapa panorámico (23 de ancho × 13 de alto, pensado para 16:9), mismo formato solo con filas más largas y más puntos de spawn distribuidos en el ancho extra:

```json
{
  "width": 23,
  "height": 13,
  "aspect_hint": "16:9",
  "tiles": ["......................." /* 13 filas de 23 caracteres */],
  "player_spawns": [[3,12],[8,12],[14,12],[19,12]],
  "enemy_spawns": [[0,0],[7,0],[15,0],[22,0]],
  "enemy_wave": {"basic": 10, "fast": 8, "power": 6, "armor": 3},
  "base_position": [11,12]
}
```

- **El editor de niveles es una herramienta de desarrollo, no una función del juego final.** Es un ejecutable **separado** (no se empaqueta ni se distribuye con el juego) que lee/escribe este mismo formato JSON, incluyendo mapas de ancho variable. Su único propósito es agilizarle al equipo de desarrollo la creación y el ajuste de los escenarios oficiales (la campaña clásica de 35 etapas y los mapas de los demás modos) sin tener que escribir el JSON a mano. El detalle técnico de esta herramienta está en la [sección 12.7](#editor-herramienta).
- Los mapas de la campaña clásica se generan como una colección de 35\+ archivos en formato 13×13, pudiendo reutilizar/adaptar los patrones históricos de *Battle City* como referencia de diseño; los mapas panorámicos se agregan como campaña o modos alternativos sin afectar la compatibilidad de los clásicos. Todos estos archivos, sea cual sea su origen, se generan con el editor de la [sección 12.7](#editor-herramienta) y se incluyen como datos del juego (no la herramienta en sí).

## 9\. De 2 a 4 jugadores simultáneos

El campo de juego original (13×13 celdas) es pequeño y se ve completo en una sola pantalla sin necesidad de scroll ni de pantalla dividida; el juego ya soportaba 2 jugadores compartiendo la misma vista. Escalar a 4 jugadores mantiene ese mismo enfoque de **vista única compartida** (no split\-screen), con estos ajustes:

- **Spawns de jugador**\: se definen 4 puntos de aparición en el borde inferior del mapa (en vez de 2), separados lo suficiente para evitar colisiones instantáneas al reaparecer.
- **Controles locales**\: soporte simultáneo de hasta 4 mandos (gamepad) vía raylib (`IsGamepadAvailable`, lectura por índice de gamepad 0\-3), más un esquema de teclado compartido de emergencia para 1\-2 jugadores sin mando (ej. WASD\+Ctrl para P1, flechas\+Enter para P2).
- **HUD**\: franja lateral o superior con vidas, nivel de mejora y color identificador de cada jugador conectado; el original ya reserva esa franja lateral para el marcador, se amplía para 4 entradas.
- **Balance de oleadas**\: con más jugadores disparando, la cantidad y velocidad de aparición de enemigos debe escalar (parámetro `enemy_wave` multiplicado según jugadores activos) para mantener la dificultad.
- **Fuego amigo**\: configurable por modo — desactivado en la campaña cooperativa, activado en los modos versus (ver [sección 10](#modos)).
- **Configuración por jugador**\: resolución de pantalla, asignación de qué mando usa cada jugador y mapeo de teclas son todos configurables desde un menú de opciones antes de empezar la partida (ver [sección 12.5](#configuracion)).

## 10\. Modos de juego

Además de la campaña clásica, se documentan modos adicionales pensados para aprovechar el multijugador y la red LAN. Cada modo se implementa como una variación de reglas sobre el mismo motor de simulación (mapa, tanques, colisiones, power\-ups), controlada por un objeto de configuración de modo — evitando duplicar código de motor por modo.

**Formato de mapa por modo:** la **Campaña Clásica** usa exclusivamente los escenarios 13×13 fieles al original (ver [sección 4.1](#grilla) y [sección 8](#niveles)); los demás modos —pensados como modos "modernos" a agregar después de tener el clon clásico funcionando— pueden usar tanto mapas clásicos como los **escenarios panorámicos más anchos** ([sección 12.6](#camara)), según lo que defina cada mapa individual. Ningún modo obliga a usar formato panorámico: es una opción del mapa, no una regla del modo.

| Modo | Jugadores | Objetivo | Notas de diseño |
| --- | --- | --- | --- |
| **Campaña clásica** | 1\-4 cooperativo | Sobrevivir 35 etapas defendiendo la base, como el original. | Mapa: 13×13 clásico fijo. Enemigos controlados por IA; fuego amigo desactivado. |
| **Versus (deathmatch)** | 2\-4 competitivo | Ser el último tanque en pie / más muertes en tiempo límite. | Mapa: clásico o panorámico. Sin tanques IA (o con IA opcional como "relleno"); fuego amigo activado. |
| **Captura la bandera / Rey de la colina** | 2\-4, por equipos o individual | Mantener control de una zona o bandera central el mayor tiempo posible. | Mapa: clásico o panorámico. Sistema de "zona" simple (círculo/celdas marcadas) con temporizador de control. |
| **Supervivencia / Horda** | 1\-4 cooperativo | Resistir oleadas infinitas de enemigos con dificultad creciente. | Mapa: clásico o panorámico. Oleadas procedurales con las mismas tablas de spawn que la campaña, sin límite de 35 etapas. |
| **Boss Rush** | 1\-4 cooperativo | Enfrentar una secuencia de "tanques jefe" (variantes blindadas con más vida/ataques). | Mapa: clásico o panorámico. Reutiliza el componente de tanque con stats ampliados. |
| **Contrarreloj** | 1\-4 | Destruir un número objetivo de enemigos antes de que se acabe el tiempo. | Mapa: clásico o panorámico. Ideal para partidas rápidas en LAN; tabla de mejores tiempos local. |

Todos los modos son compatibles con el editor de niveles de la [sección 8](#niveles) marcando en los metadatos del mapa qué modos soporta y, si corresponde, con qué relación de aspecto está pensado.

## 11\. Juego en red LAN

### 11\.1 Arquitectura general

Se recomienda **cliente\-servidor autoritativo** sobre **ENet** (biblioteca UDP con canales confiables/no confiables opcionales, ligera y pensada para juegos, usada habitualmente junto con raylib para este tipo de proyectos). Uno de los jugadores aloja la partida (servidor embebido en el mismo ejecutable) y hasta 3 más se conectan como clientes desde otras PCs de la misma red local.

- El **servidor** posee el estado autoritativo: posiciones de tanques, balas, terreno, power\-ups, puntaje. Simula el juego a un tick fijo (ej. 30\-60 Hz).
- Los **clientes** envían su input (dirección presionada, disparo) al servidor y reciben snapshots del estado del mundo para renderizar.
- Al ser LAN (latencia típica \<5 ms), no hace falta la complejidad de predicción/reconciliación de un shooter competitivo por internet: alcanza con **interpolación simple** entre snapshots para que el movimiento se vea fluido, sin necesidad de rollback ni lag compensation.

### 11\.2 Descubrimiento de partidas en LAN

- El servidor anuncia la partida por **broadcast UDP** en la red local (puerto fijo, ej. `27015`), enviando periódicamente un paquete con nombre de partida, modo, mapa y jugadores conectados/máximo.
- Los clientes escuchan ese broadcast para mostrar una lista de "partidas encontradas en la red", sin que el usuario tenga que tipear una IP manualmente (con opción manual como respaldo).

### 11\.3 Protocolo de mensajes (resumen)

| Mensaje | Dirección | Contenido | Canal ENet |
| --- | --- | --- | --- |
| `JOIN_REQUEST` | Cliente → Servidor | Nombre de jugador, versión de protocolo. | Confiable |
| `JOIN_ACCEPT` / `JOIN_REJECT` | Servidor → Cliente | ID de jugador asignado, estado inicial del mapa. | Confiable |
| `PLAYER_INPUT` | Cliente → Servidor | Dirección presionada, disparo (bitmask), número de tick. | No confiable (se reenvía cada frame) |
| `WORLD_SNAPSHOT` | Servidor → Clientes | Posiciones/estados de tanques, balas y terreno modificado desde el último snapshot. | No confiable |
| `EVENT` (explosión, power\-up, destrucción de base, fin de partida) | Servidor → Clientes | Tipo de evento \+ datos asociados. | Confiable |
| `DISCONNECT` | Ambos | Motivo. | Confiable |

- Los mensajes de input y snapshot van por canal **no confiable** (se prioriza la última posición conocida antes que reenviar paquetes viejos); los eventos discretos (explosiones, cambios de terreno, fin de partida) van por canal **confiable** para no perder información crítica.

### 11\.4 Multijugador local \+ red combinados

Se recomienda que cada PC conectada pueda aportar **más de un jugador local** (ej. dos personas compartiendo teclado/mandos en la misma PC, conectadas como "cliente" a la partida LAN), de forma que 4 jugadores simultáneos puedan lograrse tanto con 4 PCs en red, como con combinaciones (ej. 2 PCs con 2 jugadores locales cada una). Esto se resuelve enviando **N inputs por cliente** (uno por jugador local en esa PC) en vez de asumir 1 input \= 1 cliente.

## 12\. Arquitectura técnica del software

### 12\.1 Stack elegido

| Componente | Elección | Motivo |
| --- | --- | --- |
| Lenguaje | C\+\+17/20 | Rendimiento nativo, control total, sin runtime de motor. |
| Gráficos/input/audio | [raylib](https://www.raylib.com/) (v5.x) | Biblioteca (no motor): ventana, renderer 2D, teclado/gamepad, audio — todo lo necesario sin editor ni escenas, ideal para fines didácticos. Con soporte oficial de compilación a Android vía NDK. |
| Networking | [ENet](http://enet.bespin.org/) | UDP con canales confiables/no confiables, pensado para juegos en tiempo real, liviano y ampliamente combinado con raylib. |
| Build system | CMake | Multiplataforma (Windows ahora, Linux/Android a futuro), estándar de facto en C\+\+. |
| Formato de niveles | JSON (via una librería header\-only como `nlohmann/json`) | Legible, editable a mano, fácil de versionar. |

### 12\.2 Bucle principal

Bucle de juego a **paso fijo (fixed timestep)** para que la simulación (física, colisiones, red) sea determinística e independiente del framerate de render:

```
acumulador = 0
mientras el juego corra:
    delta = tiempo_del_frame_anterior()
    acumulador += delta
    mientras acumulador >= PASO_FIJO:
        procesar_input_local()
        procesar_mensajes_de_red()
        actualizar_simulacion(PASO_FIJO)   // movimiento, colisiones, IA, power-ups
        acumulador -= PASO_FIJO
    interpolar_y_renderizar(acumulador / PASO_FIJO)
```

### 12\.3 Organización de entidades

Para un proyecto didáctico no hace falta un ECS completo: alcanza con una estructura simple de **entidades por struct \+ listas por tipo** (tanques, balas, power\-ups, tiles), cada una con sus datos y una función `actualizar()` / `renderizar()`. Se recomienda igualmente separar responsabilidades en módulos claros, y sacar la herramienta de edición de niveles del árbol del juego para que quede claro que no forma parte del producto final (ver [sección 12.7](#editor-herramienta)):

```
/src                 -> código del juego (se compila al ejecutable final)
  /core        -> bucle principal, tiempo, configuración
  /world       -> mapa de tiles, colisiones, destrucción de terreno
  /entities    -> tanque, bala, power-up, base
  /ai          -> máquina de estados de enemigos
  /modes       -> reglas de cada modo de juego (campaña, versus, horda, etc.)
  /net         -> cliente/servidor ENet, protocolo, serialización
  /render      -> dibujo con raylib, cámara, HUD
/tools               -> herramientas internas, NO se empaquetan con el juego
  /level_editor -> editor de niveles standalone (ver 12.7)
/shared              -> código común a /src y /tools (formato de nivel, tipos de tile)
/assets              -> sprites, sonidos, fuentes, mapas (JSON)
```

### 12\.4 Input abstraído

Capa de input única que unifica: hasta 4 gamepads locales, teclado (esquema de emergencia), e inputs recibidos por red de clientes remotos — todos vuelcan a la misma estructura `PlayerInput { moverArriba, moverAbajo, moverIzquierda, moverDerecha, disparar }` consumida por la simulación, para que el código de juego no distinga entre origen local o remoto del input.

### 12\.5 Configuración: resolución, asignación de mandos y mapeo de teclas

- **Resolución de pantalla**\: menú de opciones con selección de resolución (lista de resoluciones soportadas por el monitor vía `GetMonitorWidth`/`GetMonitorHeight` de raylib) y modo ventana / pantalla completa / sin bordes (`ToggleFullscreen`, `SetWindowSize`), incluyendo relaciones de aspecto panorámicas (16:9, 21:9). El área de juego se escala manteniendo el aspecto del mapa cargado y usando filtrado *nearest\-neighbor* para conservar el estilo pixel\-art; el HUD se reubica según el espacio sobrante. El manejo detallado de mapas más anchos que altos se describe en [12\.6](#camara).
- **Asignación de mando por jugador**\: pantalla de "lobby local" donde cada jugador (hasta 4) elige su slot y qué mando conectado va a usar (raylib expone los mandos disponibles por índice, `IsGamepadAvailable(i)`); si no hay mando disponible, el jugador puede optar por un esquema de teclado. La asignación se guarda por sesión y admite reconexión en caliente si un mando se desconecta y se vuelve a conectar.
- **Mapeo de teclas (remapping)**\: menú de configuración de controles donde cada acción (mover arriba/abajo/izquierda/derecha, disparar, pausa) se reasigna capturando la siguiente tecla o botón presionado (`GetKeyPressed()` / `GetGamepadButtonPressed()` de raylib). Se admite un esquema independiente por cada jugador que use teclado, para los casos de varios jugadores compartiendo teclado (ver [sección 9](#multijugador-local)).
- **Persistencia**\: toda esta configuración (resolución, mando asignado por jugador, mapeo de teclas) se guarda en un archivo `config.json` en la carpeta de usuario, separado de los archivos de nivel, y se recarga al iniciar el juego con valores por defecto razonables si el archivo no existe.

### 12\.6 Cámara y escenarios panorámicos

- **Sin scroll, mapa siempre visible completo**\: siguiendo el mismo criterio de "vista única compartida" de la [sección 9](#multijugador-local), la cámara no hace scroll ni sigue a un jugador: el mapa completo (clásico 13×13 o panorámico más ancho) se ve siempre entero en pantalla. Esto evita la complejidad de una cámara dinámica y mantiene la lectura táctica simultánea para los 4 jugadores.
- **Escalado a la ventana (`Camera2D` de raylib)**\: el mapa se dibuja a su resolución nativa de tiles y se escala con una cámara ortográfica 2D para ocupar el máximo espacio posible de la ventana, manteniendo siempre la relación de aspecto real del mapa (`ancho_celdas / alto_celdas`) para no deformar los sprites.
- **Pilarboxing / letterboxing automático**\: si la relación de aspecto de la ventana no coincide exactamente con la del mapa (por ejemplo, un mapa clásico 13×13 casi cuadrado en un monitor 16:9), se agregan franjas vacías a los costados (pilarboxing). Esas franjas son el lugar natural donde ubicar el HUD (vidas, puntaje, nivel de mejora por jugador) sin superponerlo al área jugable — en mapas panorámicos anchos, el HUD puede pasar a una franja superior/inferior más angosta en vez de lateral, ya que el mapa ocupa casi todo el ancho.
- **Diseño de escenarios panorámicos**\: al definir mapas más anchos (ver [sección 4.1](#grilla) y [sección 8](#niveles)), se recomienda distribuir los spawns de los 4 jugadores y de los enemigos aprovechando el ancho extra (más puntos de entrada, rutas alternativas) en lugar de simplemente estirar el diseño clásico, para que el formato panorámico aporte variedad táctica y no solo más espacio vacío.

### 12\.7 Herramienta de editor de niveles (uso interno, no se distribuye)

- **Ejecutable separado del juego.** Es un proyecto de herramienta (`level_editor`), con su propio `add_executable` en CMake, que **no forma parte del build de release** del juego (ver estructura de carpetas en [12\.3](#entidades)\: vive en `/tools`, no en `/src`). El script de empaquetado final solo debe tomar el binario del juego y la carpeta `/assets` — nunca el editor.
- **Stack**\: reutiliza raylib para ventana/render/input, sumando [raygui](https://github.com/raysan5/raygui) (extensión de raylib para UI inmediata: botones, paneles, selectores) para la interfaz de la herramienta — paneles y botones que jamás aparecerían en el juego final, por eso no se justifica traerlos como dependencia del ejecutable principal.
- **Código compartido, no duplicado**\: la lectura/escritura del formato JSON de nivel ([sección 8](#niveles)) y el dibujo de los tiles viven en el módulo `/shared` y son usados tanto por el juego como por el editor, para que un mapa se vea en el editor exactamente igual que en el juego real.
- **Funcionalidad mínima esperada**\: grilla editable con selección de terreno por clic (paleta de tiles), ajuste de `width`/`height` del mapa (incluye armar escenarios panorámicos), colocación de spawns de jugadores/enemigos y de la base, edición de la tabla `enemy_wave`, guardado/carga de archivos `.json` de nivel, y una vista previa que renderiza el mapa igual que lo haría el juego (usando el código compartido de `/shared`).
- **No requiere red ni multijugador**\: es una herramienta de un solo usuario, offline, pensada para iterar rápido sobre el diseño de mapas antes de que esos archivos terminen empaquetados como datos del juego.

#### Controles y atajos

El editor trabaja en dos modos alternables, porque pintar terreno y colocar entidades puntuales (spawns, base) son acciones distintas sobre la misma grilla:

| Acción | Control |
| --- | --- |
| Alternar Modo Terreno / Modo Entidades | `Tab` |
| Seleccionar tipo de tile en la paleta (vacío, ladrillo, acero, agua, arbusto, hielo) | Teclas `1`\-`6`, o clic en el panel de paleta (raygui) |
| Pintar el tile seleccionado (Modo Terreno) | Clic izquierdo (mantener y arrastrar para pintar varias celdas seguidas) |
| Borrar tile (volver a vacío) | Clic derecho |
| "Gotero" — tomar el tipo de tile bajo el cursor como tile seleccionado | `Alt` \+ clic izquierdo |
| Colocar spawn de jugador 1\-4 (Modo Entidades) | Teclas `F1`\-`F4` para elegir el slot, luego clic en la celda |
| Colocar spawn de enemigo | Tecla `E`, luego clic en la celda |
| Colocar/mover la base (águila) | Tecla `B`, luego clic en la celda |
| Paneo de la vista | Clic con botón central \+ arrastrar, o `W A S D` |
| Zoom in / out | Rueda del mouse |
| Deshacer / rehacer | `Ctrl+Z` / `Ctrl+Y` |
| Nuevo mapa / abrir / guardar | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` |
| Cambiar ancho o alto del mapa (redimensionar) | Panel raygui con campos numéricos `width`/`height` (no atajo de teclado, para evitar redimensionar por error) |
| Alternar grilla / coordenadas en pantalla | Tecla `G` |
| Vista previa (renderiza el mapa como lo haría el juego, con el código de `/shared`) | Tecla `F5` |

Los atajos de una sola tecla (`1`\-`6`, `E`, `B`, `G`) solo actúan sobre el lienzo del mapa; cuando el foco está en un campo de texto de raygui (por ejemplo escribiendo el ancho del mapa) se desactivan para no interferir con la escritura.

## 13\. Assets necesarios

- **Sprites** en estilo pixel\-art (16×16 o 32×32 px base, escalable con `nearest-neighbor` para mantener el look retro): tanques del jugador (4 colores, 4 direcciones, con/sin power\-up visual), 4 tipos de tanque enemigo (4 direcciones), terrenos (ladrillo, acero, agua animada, arbusto, hielo), base/águila (estado normal y destruida), balas, explosiones (spritesheet de animación), íconos de power\-up (6).
- **Sonido**\: disparo, impacto en ladrillo/acero, explosión de tanque, explosión de base (game over), recolección de power\-up, música de menú/nivel (opcional, loop corto estilo chiptune), sonido de "nueva partida"/"game over".
- **Fuente**\: tipografía pixel monoespaciada para HUD y menús.

## 14\. Plan de implementación por fases

Pensado para construirse incrementalmente, cada fase deja el juego en un estado jugable y probable de verificar antes de continuar.

| Fase | Entregable |
| --- | --- |
| 0 — Setup | Proyecto CMake, ventana raylib, bucle a paso fijo, carga de un mapa de prueba en pantalla. |
| 1 — Movimiento y mapa | Tile map de 13×13, colisión tanque\-terreno, movimiento de un jugador. |
| 2 — Disparo y destrucción | Balas, colisión bala\-terreno con destrucción por sub\-bloque, colisión bala\-tanque. |
| 3 — Enemigos e IA | Los 4 tipos de enemigo, spawn por oleadas, máquina de estados simple, base destructible y game over. |
| 4 — Power\-ups y progresión | Los 6 power\-ups, vidas, puntaje, transición entre etapas (campaña de N mapas). |
| 5 — Multijugador local (2\-4P) | Soporte de hasta 4 gamepads \+ esquema de teclado, HUD multi\-jugador, spawns y balance ajustado. |
| 6 — Editor de niveles (herramienta interna) | Ejecutable standalone en `/tools` ([sección 12.7](#editor-herramienta)) que lee/escribe el formato JSON de la [sección 8](#niveles); se usa para producir los mapas de la campaña clásica y de los demás modos, pero no se empaqueta con el juego. |
| 7 — Red LAN | Servidor embebido, protocolo ENet, descubrimiento por broadcast, sincronización de estado para 2\-4 PCs. |
| 8 — Modos de juego adicionales | Versus, captura de zona, horda, boss rush, contrarreloj, sobre el mismo motor. |
| 9 — Pulido | Audio, animaciones, menús, tabla de puntajes, ajustes de balance final. |
| 10 — Portabilidad a Android | Ver [sección 15](#android). |

## 15\. Consideraciones futuras: portar a Android

Al haber elegido raylib desde el inicio, el camino a Android está previsto sin cambiar la base gráfica:

- raylib ofrece soporte de compilación a Android vía NDK con plantillas de proyecto oficiales.
- Puntos a resolver al portar: capa de **input táctil** (reemplazo de teclado/gamepad por controles en pantalla o soporte de gamepad Bluetooth), adaptación de resolución/aspecto de pantalla, y evaluar si el modo LAN usa Wi\-Fi local del dispositivo (viable, mismo protocolo ENet/UDP funciona sobre Wi\-Fi).
- No se requiere reescribir lógica de juego ni de red: la separación en módulos de la [sección 12.3](#entidades) permite que solo `/render` e `/core` (manejo de input y ventana) necesiten una capa específica de plataforma.

## 16\. Fuentes consultadas

- [Tank Battalion / Battle City – Hardcore Gaming 101](https://www.hardcoregaming101.net/tank-battalion-battle-city/)
- [Battle City \- FAQ \- NES \- By Shirow \- GameFAQs](https://gamefaqs.gamespot.com/nes/562966-battle-city/faqs/15969)
- [CSEE4840 Battle City Project Design Document \- Columbia University](https://www.cs.columbia.edu/~sedwards/classes/2011/4840/designs/Battle-City.pdf)
- [raylib — sitio oficial / repositorio en GitHub](https://github.com/raysan5/raylib)
- [raylib\-extras/networking\_example (raylib \+ ENet)](https://github.com/raylib-extras/networking_example)
- [ENet — Tutorial oficial](http://enet.bespin.org/Tutorial.html)
