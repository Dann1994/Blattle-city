#pragma once

#include <array>
#include <string>
#include <vector>

#include <raylib.h>

#include "ArmorEnemySystem.h"
#include "BulletImpactSprites.h"
#include "Camera.h"
#include "BulletImpactSystem.h"
#include "BulletSprites.h"
#include "BulletSystem.h"
#include "EnemySprites.h"
#include "EnemySystem.h"
#include "FastEnemySystem.h"
#include "LanDiscovery.h"
#include "LevelFormat.h"
#include "NetHost.h"
#include "PowerEnemySystem.h"
#include "PowerUpSystem.h"
#include "ScoreEvent.h"
#include "ScorePopupSprites.h"
#include "ScorePopupSystem.h"
#include "ShieldSprites.h"
#include "SpawnFlash.h"
#include "SpawnFlashSprites.h"
#include "SpecialExplosionSprites.h"
#include "SpecialExplosionSystem.h"
#include "Tank.h"
#include "TankSprites.h"
#include "TileMap.h"

namespace bc {

// Fuego amigo entre P1 y P2 (seccion custom, configurable con F3):
// Off: las balas de un jugador atraviesan al otro, sin ningun efecto.
// Paralyze (nivel 1): la bala aliada se frena en el tanque y lo paraliza
//   parpadeando 5 segundos (reusa Freeze/IsFrozen), sin quitarle nada mas.
// Damage (nivel 2): sistema de potencia/salud unificado (ver ResolveTankHit)
//   — la potencia de quien disparo se resta directo del nivel de arma
//   actual del objetivo (su "salud"); si eso lo agota, queda destruido:
//   explota con la animacion del disparo especial y respawnea como si
//   hubiera perdido una vida. El escudo absorbe el golpe entero mientras le
//   quede salud propia (ver Tank::AbsorbShieldHit).
enum class FriendlyFireMode { Off, Paralyze, Damage };

// Pantalla de titulo (seccion custom, pedida explicitamente con un arbol de
// opciones concreto): Menu = en algun lugar del menu (ver MenuScreen para
// cual exactamente), Playing = la partida en curso (Update/Render de
// siempre), CharacterSelect = pantalla de "cuantos jugadores y de que color"
// entre elegir Arcade > Local y que arranque el nivel (ver EnterCharacterSelect/
// ProcessCharacterSelectInput/RenderCharacterSelect, pedida explicitamente).
enum class AppState { Menu, Playing, CharacterSelect, LevelBuilder };

// Los niveles del arbol de menu. Options solo se entra desde la pantalla
// principal (Main), asi que Esc siempre vuelve ahi. ControlMapping = lista
// Jugador 1-4; PlayerActions = las 7 acciones reasignables de un jugador
// puntual, con la tecla actual al lado; CapturingKey = el prompt "Presione
// una tecla" al reasignar una.
// PauseMain/PauseEndChoice/PauseConfirmEnd son el menu de pausa (ver
// paused_ mas abajo): PauseMain = la lista Continuar/Guardar/Cargar/
// Opciones/Terminar partida; PauseEndChoice = Menu principal/Cerrar el
// juego (al elegir "Terminar partida"); PauseConfirmEnd = confirmacion Si/No
// de esa eleccion. Options/ControlMapping/PlayerActions/CapturingKey se
// REUSAN tal cual desde la pausa (mismo codigo que desde el titulo, ver
// RenderMenuScreenBody/optionsBackTarget_), asi que ese submenu se ve y se
// maneja identico este en pantalla de titulo o en medio de una partida.
// ControlDeviceChoice (pedido explicitamente, entre ControlMapping y
// PlayerActions): Teclado/Mando, que dispositivo se va a configurar para
// ese jugador. PlayerActions/CapturingKey pasan a leer/escribir
// playerGamepadBindings_ en vez de playerBindings_ si se eligio Mando (ver
// mappingGamepad_), mostrando solo Disparar/Especial/Inicio/Pausa (el
// movimiento con mando es fijo: D-pad + analogico, no se remapea).
enum class MenuScreen { Main, ModeSubmenu, Options, ControlMapping, ControlDeviceChoice, PlayerActions, CapturingKey, PauseMain, PauseEndChoice, PauseConfirmEnd, MainConfirmExit, LanModeChoice, LanHosting, LanBrowsing, LanConnectIp, LanConnecting };

// Transicion de nivel (seccion custom, calcada del original): None = juego
// normal; ShowingText = pantalla gris solida con "NIVEL N" en el centro
// durante un tiempo fijo; Curtain = esa misma pantalla gris partida al
// medio, una mitad se va para arriba y la otra para abajo (como un telon
// que se abre) dejando ver el nivel de fondo. Mientras no sea None,
// Update() congela todo el juego (nada se mueve/dispara/spawnea) y solo
// avanza el temporizador de la transicion (ver BeginLevelTransition).
enum class LevelTransitionPhase { None, ShowingText, Curtain };

// Secuencia de fin de partida (pedida explicitamente, ver TriggerGameOver):
// None = partida en curso normal; Rising = el sprite "GAME OVER" (Tanques.png)
// sube desde abajo de la pantalla hasta el centro, mientras el resto del
// juego (enemigos, balas, spawns) sigue andando y SOLO los jugadores quedan
// congelados (ver gameOver_, que se usa como ese freeze puntual); Holding =
// ya centrado, se queda quieto kGameOverHoldDuration o hasta que se apriete
// el boton de Pausa/Enter; NameEntry = si el mejor puntaje de la partida
// supero matchStartHighScore_, pide el nombre de ese jugador antes de volver
// al menu (ver ProcessGameOverInput) — a partir de esta fase si se congela
// todo (ya no hay "espectaculo" de fondo que mostrar).
enum class GameOverPhase { None, Rising, Holding, NameEntry };

// Pantalla de puntuacion de fin de nivel (seccion custom, calcada del
// original): None = no se muestra; Counting = fondo negro con una fila por
// jugador activo (icono : [4 iconos de enemigo, cada uno con su conteo,
// revelandose uno a la vez de izquierda a derecha] TOTAL <suma>), animando
// el conteo de cada fila en paralelo; Holding = todo ya revelado, pausa fija
// antes de pasar a la transicion del siguiente nivel (ver BeginScoreTally/
// TickScoreTally/RenderScoreTally en Game.cpp). Se dispara cuando termina la
// espera de kLevelClearDelay (ver CheckLevelCompletion), y mientras no sea
// None Update() congela el juego igual que LevelTransitionPhase.
enum class ScoreTallyPhase { None, Counting, Holding };

// Las 8 acciones de juego que se pueden reasignar por jugador (ver
// PlayerKeyBindings). Start es la tecla que une/saca a ese jugador de la
// partida (antes fija en las teclas 1/2/3/4, ver ProcessInput). Pause abre
// el menu de pausa (ver paused_/MenuScreen::PauseMain) — CUALQUIER jugador
// (activo o no) puede pausar con la suya; ademas, fija y sin importar el
// mapeo de nadie, la tecla Enter del teclado tambien pausa (pedido
// explicitamente, ver ProcessInput).
enum class InputAction { Up, Down, Left, Right, Shoot, Special, Start, Pause };
constexpr int kInputActionCount = 8;

struct PlayerKeyBindings {
    int keys[kInputActionCount] = {};
};

// Botones de mando por jugador (pedido explicitamente): mismos indices que
// PlayerKeyBindings (InputAction) para poder reusar el mismo codigo de
// render/captura, pero solo Shoot/Special/Start/Pause tienen sentido aca
// (Up/Down/Left/Right quedan sin usar: el movimiento con mando es siempre
// D-pad + analogico, no una asignacion de un boton por direccion).
struct PlayerGamepadBindings {
    int buttons[kInputActionCount] = {};
};

class Game {
public:
    void Run();

private:
    void Init();
    void ProcessInput();
    void Update(double fixedDt);
    void Render(double interpolationAlpha);
    void Shutdown();

    // Musica/ambiente (pedido explicitamente): se llama una vez por frame
    // real (no por paso fijo, ver Run) para alimentar el stream de audio.
    // battleLoopMusic_ suena en bucle infinito mientras haya enemigos por
    // eliminar en el nivel actual (ver EnemiesRemainingThisLevel) Y la
    // partida este realmente en curso (no en el menu, pausa, transicion de
    // nivel, puntuacion o fin de partida) — se corta y arranca de nuevo
    // desde el principio cada vez que esa condicion cambia, no queda
    // pausada a mitad de pista.
    void TickAudio();

    // Entrada del menu (flechas/Enter/Esc) mientras appState_ != Playing O
    // mientras paused_ (ver MenuScreen::PauseMain y siguientes): reemplaza a
    // ProcessInput() por completo ese frame (el menu no necesita el resto:
    // input de jugador, teclas de prueba, etc.). Options/ControlMapping/
    // PlayerActions/CapturingKey se manejan identico venga de titulo o de
    // pausa; solo "back" en Options usa optionsBackTarget_ para saber a cual
    // volver.
    void ProcessMenuInput();

    // El boton de Pausa de CUALQUIER jugador (activo o no) en su teclado o
    // en SU mando asignado, o la tecla Enter del teclado (fija, no
    // configurable): true en el frame exacto en que se aprieta. Se reusa
    // tanto para ABRIR el menu de pausa como, pedido explicitamente, para
    // CERRARLO de nuevo si se lo vuelve a apretar estando ya en la pantalla
    // principal de pausa (ver ProcessInput).
    bool IsPauseButtonPressed() const;

    // Dibuja la pantalla de titulo/menu completa (logo, puntaje maximo, la
    // lista que corresponda a menuScreen_, y el texto de ayuda de abajo).
    // Reemplaza a Render() por completo mientras appState_ != Playing.
    void RenderMenu();

    // El cuerpo de la lista segun menuScreen_ (Main/ModeSubmenu/Options/
    // ControlMapping/PlayerActions+CapturingKey), extraido de RenderMenu
    // para poder reusarlo tal cual desde RenderPauseOverlay (Options y todo
    // lo que cuelga de ahi se ve identico en el menu de pausa). listTop es
    // la altura en Y donde arranca la lista (RenderMenu la calcula debajo
    // del logo; RenderPauseOverlay usa una fija, mas arriba, sin logo).
    // hintText sale llena con el texto de ayuda de abajo, si corresponde.
    void RenderMenuScreenBody(float listTop, std::string& hintText);

    // Pie de pagina comun (texto de ayuda + leyenda de controles, mas el
    // cuadro "Presione una tecla" si menuScreen_ es CapturingKey), extraido
    // de RenderMenu por la misma razon que RenderMenuScreenBody.
    void RenderMenuFooter(const std::string& hintText);

    // Encima de la escena de juego ya congelada (ver paused_), sin arrancar
    // su propio BeginDrawing/ClearBackground como RenderMenu (se dibuja
    // DENTRO del mismo frame de Render()): atenua el fondo y dibuja la
    // pantalla de pausa que corresponda a menuScreen_ (lista principal,
    // Terminar partida, la confirmacion Si/No, o -reusando
    // RenderMenuScreenBody/RenderMenuFooter- Opciones y lo que cuelga de
    // ahi).
    void RenderPauseOverlay();

    // Una linea de una lista del menu, centrada, con el tanque selector
    // (menuSelectorTexture_) a la izquierda si esta resaltada; en gris si
    // enabled es false (opciones marcadas "todavia no disponible").
    void DrawMenuLine(const char* text, float y, bool selected, bool enabled);

    // Arranca la partida de verdad: recarga el mapa/estado (ResetState) y
    // pasa a appState_ = Playing. Por ahora es el unico modo/submenu que
    // hace algo real (Arcade > Local); los demas todavia no tienen
    // gameplay propio (ver ProcessMenuInput).
    void StartArcadeLocal();

    // Pantalla de seleccion de personaje (pedida explicitamente), entre
    // elegir Arcade > Local y que arranque el nivel: 4 recuadros en fila,
    // "JUGADOR 1".."JUGADOR 4" arriba de cada uno. El jugador 1 siempre esta
    // presente (arranca en el primer color); los jugadores 2-4 arrancan en
    // "PULSE DISPARAR" parpadeando y se unen apretando Disparar (se les
    // asigna el primer color libre, ver FirstAvailableColor) o se van
    // apretando Especial. Cualquier jugador YA UNIDO (el 1 siempre lo esta)
    // puede cambiar su propio color con izquierda/derecha en cualquier
    // momento, pedido explicitamente — nunca puede repetirse un color entre
    // 2 jugadores presentes a la vez (ver CycleLobbyColor). Cualquier
    // jugador presente puede apretar Pausa/Start para confirmar y arrancar
    // la partida (ver StartArcadeLocal), o Esc para volver al menu.
    void EnterCharacterSelect();
    void ProcessCharacterSelectInput();
    void RenderCharacterSelect();

    // Dibuja un marco decorativo de ladrillos (reusa brickUnitTextures_[0])
    // alrededor de box, expandido hacia afuera thickness ladrillos de
    // tileSize px de lado — para la pantalla de seleccion de personaje, que
    // no usa el viewport del mapa (ver RenderCharacterSelect).
    void DrawBrickBorder(Rectangle box, float tileSize, int thickness);

    // El color de cada jugador (0-3, ver playerColorIndex_) es independiente
    // de su numero de jugador desde que existe la pantalla de seleccion de
    // personaje: estas funciones traducen ese indice de color al set de
    // sprites o color de HUD que ya existian por jugador (playerNSprites_/
    // kPlayerNHudColor), para poder usar CUALQUIERA de los 4 en cualquier
    // jugador.
    TankSpriteSet& SpriteSetForColor(int colorIndex);
    Color HudColorForColor(int colorIndex) const;

    // Boton de una accion (Disparar/Especial) o direccion fija de mando
    // (Izquierda/Derecha, no configurable con mando — ver ApplyGamepadInput)
    // para el slot indicado (0-3), en su teclado o en el mando que le
    // corresponda por orden de conexion (mismo criterio que
    // IsPauseButtonPressed/GamepadIdForSlot). Pensado para la pantalla de
    // seleccion de personaje, que necesita leer input por jugador ANTES de
    // que existan tanques/PlayerInput reales.
    int GamepadIdForSlot(int slot) const;
    bool IsSlotShootPressed(int slot) const;
    bool IsSlotSpecialPressed(int slot) const;
    bool IsSlotLeftPressed(int slot) const;
    bool IsSlotRightPressed(int slot) const;

    // Le cambia el color al slot indicado, salteando los colores que ya
    // tenga tomados OTRO jugador activo (para que nunca haya dos con el
    // mismo) — direction +1/-1. Ver ProcessCharacterSelectInput.
    void CycleLobbyColor(int slot, int direction);

    // El primer color (0-3) que ningun jugador activo tenga puesto ahora
    // mismo; 0 como ultimo recurso si los 4 ya estan tomados (no deberia
    // pasar, solo hay 4 slots). Usado al unirse un jugador nuevo, desde la
    // pantalla de seleccion de personaje o mid-partida (ver SetPlayerActive).
    int FirstAvailableColor() const;

    // Puntaje maximo historico (no de esta partida sino de todas), mostrado
    // arriba de todo en el menu, y su nombre (ver highScoreHolderName_, se
    // pide en GameOverPhase::NameEntry). Se guarda en un archivo de texto en
    // /assets junto a los niveles (mismo criterio que usa el editor): primer
    // renglon el numero, segundo el nombre (vacio/ausente en partidas viejas
    // de antes de que el nombre existiera, se lee como "").
    void LoadHighScore();
    void SaveHighScore();

    // Persistencia de controles (pedido explicitamente: la configuracion de
    // teclado/mando de cada jugador debe sobrevivir a cerrar el juego).
    // Mismo criterio que el highscore: un archivo de texto en /assets con
    // los 4x8 codigos de tecla (playerBindings_) y despues los 4x4 codigos
    // de boton de mando (playerGamepadBindings_), todos separados por
    // espacios/saltos de linea. LoadControls() se llama una sola vez desde
    // Init(), DESPUES de InitDefaultKeyBindings/InitDefaultGamepadBindings
    // (para que el archivo pise los valores por defecto si existe, y si no
    // existe o esta incompleto los valores por defecto ya cargados quedan
    // como estaban). SaveControls() se llama cada vez que se confirma una
    // reasignacion (ver MenuScreen::CapturingKey).
    void LoadControls();
    void SaveControls();

    // Si candidateScore supera al maximo historico, lo actualiza (y lo
    // persiste) y recuerda a ownerId como quien lo tiene ahora (ver
    // highScoreHolderId_), para mostrar su nombre junto al puntaje maximo en
    // la pantalla de puntuacion de fin de nivel. Ojo: esto es el numero nada
    // mas, en caliente durante la partida — el NOMBRE del record historico
    // (highScoreHolderName_) solo se pide una vez, al final de la partida
    // (ver FinishGameOverHolding/matchStartHighScore_), no ac.
    void MaybeUpdateHighScore(int candidateScore, int ownerId);

    // Fin de partida (pedido explicitamente, ver GameOverPhase): arranca la
    // secuencia (Rising) la primera vez que se cumple alguna de las 2
    // condiciones (aguila destruida o todos los jugadores activos sin
    // vidas); si ya estaba en curso (gameOver_ ya en true) no hace nada, asi
    // que da lo mismo llamarla desde los 2 lugares sin chequear antes cual
    // disparo primero.
    void TriggerGameOver();

    // Entrada durante GameOverPhase != None (reemplaza a ProcessInput por
    // completo ese frame, ver ProcessInput): en Holding, el boton de Pausa
    // de cualquier jugador o Enter saltea la espera; en NameEntry, tipeo de
    // texto (letras/numeros, Backspace para borrar, Enter para confirmar
    // con al menos 1 caracter). En Rising no hace nada, la animacion corre
    // sola.
    void ProcessGameOverInput();

    // Avanza el temporizador de Rising/Holding (ver GameOverPhase) y hace
    // las transiciones de fase que dependen del tiempo (no del boton de
    // salteo, eso lo maneja ProcessGameOverInput). NameEntry no tiene nada
    // que tickear aca: es enteramente por input.
    void TickGameOverSequence(double dt);

    // Termina la espera de Holding (por tiempo o por boton, ver
    // ProcessGameOverInput/TickGameOverSequence): busca al jugador activo
    // con mas puntaje: si supera matchStartHighScore_ (el record que habia
    // ANTES de esta partida, no el que pueda haberse ido actualizando en
    // caliente durante ella, ver MaybeUpdateHighScore) pasa a NameEntry;
    // si no, termina la secuencia directo (ver FinishGameOverSequence).
    void FinishGameOverHolding();

    // Guarda gameOverNameEntry_ como highScoreHolderName_ (el puntaje en si,
    // highScore_, ya esta al dia por MaybeUpdateHighScore) y termina la
    // secuencia.
    void ConfirmGameOverNameEntry();

    // Ultimo paso de GameOverPhase, con o sin NameEntry de por medio: vuelve
    // al menu principal, mismo criterio que Esc/Terminar partida durante la
    // partida (sin ResetState: se arma todo de nuevo recien cuando se elija
    // Arcade > Local otra vez).
    void FinishGameOverSequence();

    // Dibuja la secuencia de fin de partida (ver GameOverPhase) encima de
    // todo lo demas: el sprite "GAME OVER" en su posicion segun la fase
    // (Rising/Holding), o el cuadro de pedido de nombre (NameEntry). No hace
    // nada si gameOverPhase_ es None.
    void RenderGameOverOverlay();

    // Esquema de teclado por defecto de cada jugador (el mismo que antes
    // estaba fijo en ProcessInput: WASD/flechas/IJKL/numpad + Ctrl-Espacio/
    // Ctrl-Shift/UO/KP0-KPEnter, mas las teclas 1-4 para unirse). Se llama
    // una sola vez desde Init(); de ahi en mas playerBindings_ se edita
    // desde el menu (ver MenuScreen::CapturingKey).
    void InitDefaultKeyBindings();

    // Esquema de mando por defecto de cada jugador (pedido explicitamente,
    // pensado para un control de Xbox): Disparar = A, Especial = B, Inicio
    // y Pausa = el boton central derecho ("Menu"/Play, ver
    // GamepadButtonName). Se llama una sola vez desde Init(); de ahi en mas
    // playerGamepadBindings_ se edita desde el menu (Opciones > Mapeo de
    // controles > Jugador N > Mando).
    void InitDefaultGamepadBindings();

    // Suma el mando gamepadId al input del jugador playerIndex (0-3, para
    // leer sus botones configurados, ver playerGamepadBindings_) — no
    // reemplaza el teclado, cualquiera de los dos mueve/dispara. D-pad +
    // stick izquierdo mueven (fijo, no configurable); disparo/especial usan
    // los botones asignados a ESE jugador. No hace nada si ese mando no
    // esta conectado.
    void ApplyGamepadInput(PlayerInput& input, int gamepadId, int playerIndex) const;

    // Recarga el mapa y reinicia tanque/balas/power-ups a estado inicial,
    // empezando siempre en el nivel 1 (ver Game.cpp). Llamada desde Init()
    // y desde StartArcadeLocal() (menu > Arcade > Local).
    void ResetState();

    // Ruta del archivo de nivel para levelNumber: si existe
    // levels/level_NN.json (creado con el editor) se usa ese; si no,
    // levels/test_map.json como plantilla de respaldo (para los niveles que
    // todavia no se disenaron a mano).
    std::string LevelFilePath(int levelNumber) const;

    // Carga el mapa/spawns/base de LevelFilePath(currentLevel_): pisa map_,
    // basePositionX_/Y_ (+ el anillo de fortificacion alrededor), y los
    // spawns de jugador/enemigo. No toca tanques/enemigos/balas existentes;
    // eso lo maneja quien llama (ResetState arranca todo de cero,
    // CheckLevelCompletion solo limpia lo que no tiene sentido llevarse al
    // mapa nuevo).
    void LoadCurrentLevelMap();

    // Reutilizable: vuelve a ubicar al jugador 1/2/3 en su punto de spawn
    // inicial y dispara el destello ahi. Pensado para reusarse al iniciar un
    // nivel o cuando el jugador pierde una vida (Fase 3/4), ademas del boton
    // de prueba (R) y de la destruccion por la propia explosion especial.
    void RespawnPlayer1();
    void RespawnPlayer2();
    void RespawnPlayer3();
    void RespawnPlayer4();
    void RespawnByOwnerId(int ownerId);

    // Pedido explicitamente: un jugador activo pero sin vidas (Eliminate()d)
    // puede volver a la partida robandole 1 vida al jugador con MAS vidas,
    // con solo apretar Disparar (ver el chequeo en ProcessInput). El
    // donante tiene que quedarse con al menos 1 vida despues de cederla (o
    // sea, necesita 2 o mas antes); si hay empate entre varios en la
    // cantidad maxima de vidas, se la saca al de mayor puntaje de esos. Si
    // no hay ningun donante valido (nadie tiene 2+ vidas), no hace nada. Al
    // robar: el donante pierde 1 vida, el que pidio recupera 1 (queda en 1),
    // deja de estar eliminado y respawnea como cualquier otro (mismo
    // destello + escudo de siempre, ver RespawnByOwnerId).
    void TryStealLife(int requesterSlot);

    // Activa o desactiva un jugador (teclas 1/2/3, seccion de prueba): al
    // activarlo, reinicia su tanque de cero y lo respawnea; al desactivarlo,
    // el flag pasa a false y Update()/Render() simplemente dejan de tocar a
    // ese jugador (no se mueve, no dispara, no es blanco de nada, no se
    // dibuja) hasta que se vuelva a activar. Distinto de Eliminate(): esto es
    // reversible, pensado solo para probar con menos jugadores en pantalla.
    // slotIndex (0-3) se usa solo para asignarle un color libre (ver
    // playerColorIndex_/FirstAvailableColor) al activarlo mid-partida por
    // esta via (la pantalla de seleccion de personaje, ver
    // EnterCharacterSelect, ya asigna el suyo antes de llamar aca).
    void SetPlayerActive(bool& activeFlag, Tank& tank, SpawnFlash& spawn, float spawnX, float spawnY, bool active, int slotIndex);

    // Item Pala (seccion custom): convierte en hierro temporal las hasta 8
    // celdas alrededor de la base (las que sean Ladrillo o Vacio; el hierro
    // ya existente se deja como esta), y guarda su tipo original en
    // fortifiedCells_ para poder devolverlas como estaban al vencer el
    // timer (ver TickBaseFortification, llamado desde Update).
    void ApplyShovelFortification();
    void TickBaseFortification(double dt);

    // Recorta las celdas de ladrillo del anillo alrededor de la base (ver
    // ResetState) a solo 2 unidades de espesor (de las 4 de la grilla),
    // pegadas al lado que da hacia la base. Se llama una vez despues de
    // cargar el nivel; no afecta a ningun otro ladrillo del mapa.
    void ThinBrickCellVertical(int x, int y, bool keepBottomHalf);
    void ThinBrickCellHorizontal(int x, int y, bool keepRightHalf);

    // Punto unico al que llegan todas las formas de morir (fuego amigo,
    // explosion especial, choque directo del especial): resta 1 vida y, si
    // todavia le quedan, respawnea; si no, Eliminate() (no vuelve a jugar
    // hasta reiniciar la partida).
    void HandlePlayerDeath(Tank& tank, int ownerId);

    // Aplica el dano de las explosiones especiales de este frame a un
    // tanque (ownerId identifica cual, para reconocer un choque directo -
    // ver SpecialExplosionEvent::directHitOwnerId): usa el mismo sistema de
    // potencia/salud que el resto (ver ResolveTankHit), con la potencia de
    // explosion del especial (kSpecialExplosionPower). Con escudo, lo
    // absorbe el escudo (y ademas paraliza 5s, es una explosion). Devuelve
    // true si el tanque quedo destruido y quien llama debe respawnearlo.
    bool ApplyExplosionSelfDamage(Tank& tank, int ownerId);

    // Efecto de las explosiones especiales de este frame (ver
    // specialExplosionEvents_) sobre los 4 tipos de enemigo, pedido
    // explicitamente: Basico/Rapido/Blindado mueren de un saque si la
    // explosion los alcanza (la bala en si los atraviesa sin frenarse salvo
    // que tengan escudo, ver la lista "tanks" en Update); Power resiste (ver
    // Enemy::hp) — la explosion lo deja a un impacto de morir
    // (kSpecialExplosionPowerWeakenHp) en vez de matarlo, salvo que ya
    // estuviera asi, en cuyo caso lo remata.
    void ApplySpecialExplosionsToEnemies();

    // Anima la muerte de un tanque (explosion chica a escala nativa, ver
    // Documentaciones/Exploción.png) y le resetea el nivel de arma. No
    // respawnea: eso queda a cargo de quien llama.
    void DestroyTank(Tank& tank);

    // Fuego amigo (ver FriendlyFireMode): si el modo esta activo, mata las
    // balas del OTRO jugador que esten sobre este tanque y le aplica el
    // efecto que corresponda al modo actual. Devuelve true si el tanque
    // quedo destruido, y quien llama debe respawnearlo.
    bool ApplyFriendlyFire(Tank& tank, int ownerId);

    // Sistema de resistencia bala-vs-tanque unificado (pedido explicitamente,
    // reemplaza el viejo switch por nivel con throttle/congelamiento
    // especiales): la "salud" del tanque es su nivel de arma actual
    // (Tank::WeaponLevel); un impacto de potencia "power" le resta
    // min(power, nivel actual) — si eso agota el nivel, el tanque queda
    // destruido (DestroyTank + outDestroyed=true); si no, ApplyWeaponLevelPenalty
    // con esa misma cantidad. Mientras el tanque tenga escudo (Tank::
    // IsShielded), el golpe se lo lleva el escudo entero (AbsorbShieldHit)
    // y el tanque no pierde nivel ese impacto. Devuelve cuanta salud
    // (del escudo o del tanque, la que haya absorbido) consumio este
    // impacto — lo usa quien llama para restarselo a la salud propia de la
    // bala que pego (ver BulletSystem::TankHitResolver).
    int ResolveTankHit(Tank& tank, int power, bool& outDestroyed);

    // Movimiento, disparo (normal y especial) y recogida de power-ups de un
    // tanque para este frame; comun a todos los jugadores activos. No toca
    // nada relacionado con las balas ya en vuelo ni las explosiones (eso se
    // procesa una sola vez para todos los tanques, ver Update()). others son
    // los demas tanques activos (para colision), sin contarse a si mismo.
    void UpdatePlayer(Tank& tank, PlayerInput& input, SpawnFlash& spawn, int ownerId, const std::vector<Tank*>& others, double fixedDt);

    // Arma la lista de tanques activos (para colision con others) excluyendo
    // al de excludeOwnerId, ver UpdatePlayer.
    std::vector<Tank*> ActiveOthers(int excludeOwnerId);

    // Caja de cada tanque activo (todos, sin excluir ninguno), para que los
    // power-ups no aparezcan encima de un jugador (ver PowerUpSystem::Update).
    std::vector<TankOccupiedBounds> ActiveTankBounds() const;

    // Estrella/pistola tambien las puede agarrar un enemigo (el resto de
    // los power-ups son solo para jugadores, no hacen nada si un enemigo
    // los toca). En vez de subir de nivel de arma como a un jugador, lo
    // convierten en el siguiente tipo de tanque (Basico -> Rapido ->
    // Blindado -> Power con la estrella; la pistola salta directo a Power
    // desde cualquiera). El Power, al no tener un "siguiente" tipo, arma su
    // disparo especial en vez de convertirse (ver Enemy::specialShotFuseTimer).
    void ApplyEnemyPowerUpPickups();

    // Item Granada (seccion custom): un jugador que la agarra hace explotar
    // a todos los enemigos vivos en pantalla, de los 4 tipos, de una sola
    // vez (el mapa entero entra en una sola pantalla, no hay scroll). Cada
    // uno destruido cuenta para la cuota del nivel (ver enemiesKilledThisLevel_)
    // y le acredita el puntaje que corresponda a creditOwnerId (quien agarro
    // la granada).
    void DestroyAllEnemies(int creditOwnerId);

    // Item Reloj (seccion custom): paraliza (Tank::Freeze) a todos los
    // enemigos vivos en pantalla, de los 4 tipos, por kClockFreezeDuration.
    void FreezeAllEnemies(double duration);

    // Le suma points al puntaje del jugador ownerId (0-3; cualquier otro
    // valor, por ejemplo un enemigo, no hace nada) y muestra el popup del
    // numero en (x,y) (ver ScorePopupSystem/kScoreBasicKill..kScorePowerUpPickup).
    // De paso acumula en playerLevelStats_[ownerId] (puntaje total del nivel,
    // y si points es un puntaje de eliminacion, el conteo de ese tipo de
    // enemigo), para la pantalla de puntuacion de fin de nivel.
    void AwardScoreAt(int ownerId, float x, float y, int points);

    // Vida extra por puntaje (pedido explicitamente, ver kExtraLifeScoreThreshold
    // y extraLifeMilestonesClaimed_): si newScore ya paso algun escalon de
    // 20.000 que este jugador todavia no tenia acreditado, le suma tantas
    // vidas como escalones nuevos crucen de una sola vez (por si un bono
    // grande lo hace saltar mas de uno) y suena extraLifeSound_. No hace
    // nada si el jugador ya esta eliminado (sin vidas, fuera de la partida)
    // o si el ownerId no es un jugador.
    void CheckExtraLifeByScore(int ownerId, int newScore);

    // Automatizacion de oleadas (40 niveles, ver ResetState/kEnemyWaveTiers
    // en Game.cpp): cada 1s intenta spawnear 1 enemigo (tipo al azar entre
    // los 4) en la siguiente celda de enemySpawnPositions_, en orden y
    // rotando; no lo hace si esa celda todavia tiene a otro enemigo
    // apareciendo encima (ver IsEnemySpawningAtCell), si la pantalla ya
    // esta al maximo para el nivel actual, o si ya se alcanzo la cuota de
    // enemigos a spawnear de este nivel.
    void TickEnemySpawning(double dt);

    // Apenas arranca un nivel: hace aparecer 1 enemigo al azar en cada una
    // de las celdas de spawn, todas a la vez (llamada al terminar la
    // transicion de nivel, ver BeginLevelTransition), y deja el
    // temporizador listo para que TickEnemySpawning recien intente el
    // siguiente spawn "de a uno" tras kEnemySpawnInterval.
    void SpawnInitialWaveForLevel();

    // Arranca la transicion de nivel (pantalla "NIVEL N" + telon, ver
    // LevelTransitionPhase): se llama en vez de SpawnInitialWaveForLevel
    // directamente, que recien se llama sola cuando la transicion termina
    // (ver el manejo de levelTransitionPhase_ en Update()).
    void BeginLevelTransition();

    // Dibuja el overlay de la transicion (pantalla gris + texto, o las 2
    // mitades del telon) encima de todo lo demas. No hace nada si
    // levelTransitionPhase_ es None.
    void RenderLevelTransition();

    // Cuantos enemigos (de los 4 tipos, vivos) hay ahora mismo en pantalla.
    int CountAliveEnemies() const;

    // Hay un enemigo (de cualquier tipo) con el destello de aparicion
    // todavia activo justo en esa celda.
    bool IsEnemySpawningAtCell(int cellX, int cellY) const;

    // Arma levelEnemyTypeSequence_: la lista (tamano = cuota maxima del
    // nivel actual) de que tipo de enemigo le toca a cada spawn de este
    // nivel, segun la composicion pedida por rango de nivel (ver
    // EnemyTypeCountsForLevel en Game.cpp), barajada al azar. Se llama una
    // vez por nivel (ResetState/CheckLevelCompletion), antes de spawnear.
    void BuildEnemyTypeSequenceForLevel();

    // Consume el siguiente tipo de levelEnemyTypeSequence_ y lo hace
    // aparecer en (x,y). Usado por el spawn automatico (inicial y de a
    // uno); el spawn manual de depuracion (F10 y variantes) no pasa por aca.
    void SpawnNextEnemyForLevelAt(float x, float y);

    // Si enemiesKilledThisLevel_ ya alcanzo la cuota del nivel actual (segun
    // cantidad de jugadores activos), no pasa de nivel todavia: arma
    // levelClearPending_/levelClearTimer_ (kLevelClearDelay, pedido
    // explicitamente) y sigue tickeando ese timer en los llamados
    // siguientes, con el juego normal seguir andando mientras tanto.
    // Cuando el timer llega a 0 recien ahi llama a AdvanceToNextLevel().
    void CheckLevelCompletion(double dt);

    // El paso real de cambiar de nivel (antes era el cuerpo entero de
    // CheckLevelCompletion): pasa al siguiente currentLevel_ (tope 40: se
    // queda repitiendo el 40 para siempre), carga su mapa, reubica
    // jugadores y arranca la transicion (ver BeginLevelTransition).
    void AdvanceToNextLevel();

    // Arma la pantalla de puntuacion de fin de nivel (ver ScoreTallyPhase):
    // se llama en vez de AdvanceToNextLevel directamente cuando se cumple
    // kLevelClearDelay; AdvanceToNextLevel recien se llama sola cuando la
    // pantalla de puntuacion termina (ver TickScoreTally).
    void BeginScoreTally();

    // Avanza la animacion de conteo de la pantalla de puntuacion (o la
    // pausa final de Holding); llamada desde Update() mientras
    // scoreTallyPhase_ != None, en vez del resto de la simulacion.
    void TickScoreTally(double dt);

    // Dibuja la pantalla de puntuacion de fin de nivel (fondo negro,
    // puntaje maximo + su dueno, "NIVEL N", y una fila por jugador activo
    // con el conteo de enemigos y el total ganado en el nivel). Reemplaza a
    // la escena de juego por completo mientras scoreTallyPhase_ != None (ver
    // Render()).
    void RenderScoreTally();

    // Cuantos enemigos faltan matar para terminar currentLevel_ (cuota del
    // nivel, segun cantidad de jugadores activos, menos los ya eliminados);
    // para el HUD, ver Render().
    int EnemiesRemainingThisLevel() const;

    // Nivel de agresividad 1-4 que le corresponde a currentLevel_ (1-10 = 1,
    // 11-20 = 2, 21-30 = 3, 31-40 = 4), aplicado a los 4 tipos de enemigo.
    void ApplyAggressivenessForCurrentLevel();

    // Boton de prueba (+/-): salta directo a newLevel (recortado a 1..40),
    // reiniciando todo (mapa, jugadores, enemigos, balas, power-ups, y los
    // contadores de oleada) como ResetState, pero arrancando ya en ese nivel
    // con los parametros de dificultad que le correspondan.
    void JumpToLevel(int newLevel);

    // Dibuja el destello de aparicion (mientras dura) o el tanque + escudo.
    void RenderTank(const Tank& tank, const SpawnFlash& spawn, const TankSpriteSet& sprites, const MapViewport& viewport);

    // Dibuja un tanque enemigo "Basico" (sprite propio, ver EnemySprites).
    void RenderEnemy(const Enemy& enemy, const EnemySprites& sprites, const MapViewport& viewport);

    // Arma la lista de tanques de jugador activos (sin excluir a nadie), para
    // colision/deteccion/blanco de los enemigos (ver EnemySystem::Update).
    std::vector<Tank*> ActivePlayerTanks();

    // Red de seguridad general (ver ResolveTankOverlaps): si dos tanques ya
    // materializados (jugador o enemigo, de cualquier tipo) terminan con
    // las hitboxes superpuestas por cualquier motivo, se separan un poco
    // cada frame hasta dejar de solaparse, en vez de quedar trabados. La
    // colision normal de movimiento (Tank::IsPositionBlocked) sigue siendo
    // un bloqueo duro como siempre: esto solo corrige un solape que ya
    // existe, nunca empuja durante el contacto normal.
    void ResolveTankOverlaps();

    // Solo contra el mapa (paredes/bloques), sin mirar otros tanques: usado
    // por ResolveTankOverlaps para no separar dos tanques metiendo a uno
    // adentro de una pared.
    bool IsPositionBlockedByMap(const Tank& tank, float newX, float newY) const;

    // Panel de nivel de arma / disparo especial / calor / paralisis de un
    // tanque, alineado al centro: centerX es el centro horizontal del bloque.
    // score es el puntaje propio de ESE jugador (ver playerXScore_).
    // playerColor tine el texto de datos (etiqueta/vidas/nivel/calor/
    // puntaje, ver kPlayer1HudColor..kPlayer4HudColor); las alertas
    // (sobrecalentado, especial listo, paralizado) siguen en rojo/amarillo
    // fijo, sin importar el jugador.
    void RenderPlayerHud(const Tank& tank, const char* label, int centerX, int y, int score, Color playerColor);

    static constexpr int kPlayer1Id = 0;
    static constexpr int kPlayer2Id = 1;
    static constexpr int kPlayer3Id = 2;
    static constexpr int kPlayer4Id = 3;

    TileMap map_;
    Tank player1_;
    PlayerInput input1_;
    TankSpriteSet player1Sprites_;
    SpawnFlash player1Spawn_;
    float player1SpawnX_ = 0.0f;
    float player1SpawnY_ = 0.0f;
    bool player1Active_ = true;
    Tank player2_;
    PlayerInput input2_;
    TankSpriteSet player2Sprites_;
    SpawnFlash player2Spawn_;
    float player2SpawnX_ = 0.0f;
    float player2SpawnY_ = 0.0f;
    bool player2Active_ = false;
    Tank player3_;
    PlayerInput input3_;
    TankSpriteSet player3Sprites_;
    SpawnFlash player3Spawn_;
    float player3SpawnX_ = 0.0f;
    float player3SpawnY_ = 0.0f;
    bool player3Active_ = false;
    Tank player4_;
    PlayerInput input4_;
    TankSpriteSet player4Sprites_;
    SpawnFlash player4Spawn_;
    float player4SpawnX_ = 0.0f;
    float player4SpawnY_ = 0.0f;
    bool player4Active_ = false;

    // Que color (0-3, indice entre player1Sprites_..player4Sprites_ y
    // kPlayer1HudColor..kPlayer4HudColor, ver SpriteSetForColor/
    // HudColorForColor) tiene puesto CADA jugador (0=P1..3=P4) — pedido
    // explicitamente: desde que existe la pantalla de seleccion de
    // personaje, el color ya no es siempre "igual al numero de jugador".
    // Por defecto cada uno arranca con su propio numero (0,1,2,3), como
    // era antes de que existiera esa pantalla.
    int playerColorIndex_[4] = {0, 1, 2, 3};
    BulletSystem bullets_;
    BulletSpriteSet bulletSprites_;
    EnemySystem enemies_;
    EnemySprites enemySprites_;
    FastEnemySystem fastEnemies_;
    EnemySprites fastEnemySprites_;
    ArmorEnemySystem armorEnemies_;
    EnemySprites armorEnemySprites_;
    PowerEnemySystem powerEnemies_;
    EnemySprites powerEnemySprites_;
    std::vector<std::array<int, 2>> enemySpawnPositions_;

    // Automatizacion de oleadas (ver TickEnemySpawning/CheckLevelCompletion
    // en Game.cpp): nivel de juego actual (1-40), cuantos enemigos van
    // eliminados y cuantos van spawneados en ESTE nivel, temporizador de 1s
    // entre intentos de spawn, y proxima celda de enemySpawnPositions_ (en
    // orden, rotando) para el siguiente spawn.
    int currentLevel_ = 1;
    int enemiesKilledThisLevel_ = 0;
    int enemiesSpawnedThisLevel_ = 0;
    double enemySpawnTimer_ = 0.0;
    size_t nextEnemySpawnCellIndex_ = 0;

    // Transicion de nivel (ver LevelTransitionPhase/BeginLevelTransition).
    LevelTransitionPhase levelTransitionPhase_ = LevelTransitionPhase::None;
    double levelTransitionTimer_ = 0.0;

    // Espera antes de pasar de nivel una vez cumplida la cuota (ver
    // CheckLevelCompletion/kLevelClearDelay): el juego sigue andando
    // normal durante esta espera, a diferencia de la transicion de arriba.
    bool levelClearPending_ = false;
    double levelClearTimer_ = 0.0;

    // Puntaje propio de cada jugador (ver ScoreEvent.h para los valores por
    // tipo/item, investigados del juego original): se acredita a quien
    // disparo/agarro, no es compartido entre los 4.
    int player1Score_ = 0;
    int player2Score_ = 0;
    int player3Score_ = 0;
    int player4Score_ = 0;

    // Vida extra por puntaje (pedido explicitamente, investigado del juego
    // original: cada 20.000 puntos — ver kExtraLifeScoreThreshold en
    // Game.cpp). Cuantos "escalones" de ese umbral ya se le convirtieron en
    // vida a cada jugador, para no volver a darle una si simplemente se
    // vuelve a chequear el mismo puntaje en otro frame (ver AwardScoreAt).
    std::array<int, 4> extraLifeMilestonesClaimed_{};

    // Eliminaciones (por tipo, mismo orden 0=Basico 1=Rapido 2=Blindado
    // 3=Power que levelEnemyTypeSequence_) y puntaje total conseguidos por
    // CADA jugador en el nivel actual: se resetea al empezar cada nivel
    // (ResetState/AdvanceToNextLevel/JumpToLevel) y se acumula en
    // AwardScoreAt. Usado solo por la pantalla de puntuacion de fin de nivel
    // (ver ScoreTallyPhase).
    struct PlayerLevelStats {
        int kills[4] = {0, 0, 0, 0};
        int scoreGained = 0;
    };
    std::array<PlayerLevelStats, 4> playerLevelStats_{};

    // A que jugador (kPlayer1Id..kPlayer4Id, -1 si ninguno todavia en esta
    // partida) le pertenece el puntaje maximo actual (ver
    // MaybeUpdateHighScore); se muestra su nombre junto al puntaje maximo en
    // la pantalla de puntuacion de fin de nivel. Se reinicia en ResetState
    // (es un dato de la partida en curso, no se persiste en highscore.txt).
    int highScoreHolderId_ = -1;

    // Animacion de la fila de un jugador en la pantalla de puntuacion: el
    // numero que va mostrando el slot actual mientras cuenta, y el
    // acumulador de tiempo para el proximo tick (ver TickScoreTally).
    // currentSlot se limita a REFLEJAR scoreTallySlot_ (el slot es
    // compartido entre todos los jugadores, pedido explicitamente — ver mas
    // abajo), no lo maneja cada fila por su cuenta; queda en la struct nada
    // mas para no tener que tocar RenderScoreTally.
    struct ScoreTallyRowAnim {
        int currentSlot = 0;
        int currentCount = 0;
        double timer = 0.0;
    };
    std::array<ScoreTallyRowAnim, 4> scoreTallyAnim_{};

    // Slot de tipo de enemigo (0-3, 4 = todas las filas ya terminaron de
    // contar) que estan revelando TODOS los jugadores a la vez (pedido
    // explicitamente): cada uno cuenta hasta su propio numero de ese tipo a
    // su propio ritmo, pero el que termina antes (porque mato menos de ese
    // tipo) espera a los demas — recien cuando TODOS terminaron ese slot
    // (incluida la pausa de kScoreTallyRowPause) se avanza al siguiente,
    // para todos al mismo tiempo. Ver TickScoreTally/BeginScoreTally.
    int scoreTallySlot_ = 0;

    // Bono de fin de nivel (pedido explicito): al terminar de contar todas
    // las filas, el/los jugador(es) con mayor scoreGained en
    // playerLevelStats_ (empate incluido) lo tienen en true — reciben
    // kScoreTallyClearBonus extra (ya sumado a su puntaje real) que se
    // muestra en rojo a la derecha de su TOTAL, sin mezclarse con ese numero
    // (ver TickScoreTally/RenderScoreTally). Se arma recien al pasar a
    // ScoreTallyPhase::Holding, todo en false mientras se cuenta. Pedido
    // explicitamente: solo existe con MAS de un jugador activo (con uno
    // solo no tendria sentido, siempre "ganaria" el mismo) — con un jugador
    // queda todo en false y nunca se revela nada.
    std::array<bool, 4> scoreTallyBonusAwarded_{};

    // Pedido explicitamente: el bono no aparece apenas termina el conteo,
    // espera 1s mas (dentro de la pausa de Holding) antes de sumarse al
    // puntaje real y mostrarse en pantalla. scoreTallyBonusRevealTimer_
    // cuenta ese segundo hacia atras; scoreTallyBonusRevealed_ se pone en
    // true una sola vez, cuando llega a 0 (ver TickScoreTally/RenderScoreTally).
    double scoreTallyBonusRevealTimer_ = 0.0;
    bool scoreTallyBonusRevealed_ = false;

    // Pantalla de puntuacion de fin de nivel (ver ScoreTallyPhase arriba).
    ScoreTallyPhase scoreTallyPhase_ = ScoreTallyPhase::None;
    double scoreTallyTimer_ = 0.0; // usado en Holding para la pausa final
    int scoreTallyLevelShown_ = 0; // currentLevel_ al armar la pantalla (AdvanceToNextLevel lo pisa antes de que se dibuje el numero, por eso se guarda aparte)

    // Composicion de tipos de enemigo de ESTE nivel (ver
    // BuildEnemyTypeSequenceForLevel/SpawnNextEnemyForLevelAt en Game.cpp):
    // 0=Basico, 1=Rapido, 2=Blindado, 3=Power, ya barajada al azar. Se
    // reconstruye entera cada vez que arranca un nivel.
    std::vector<int> levelEnemyTypeSequence_;
    size_t nextEnemyTypeIndex_ = 0;
    BulletImpactSystem bulletImpacts_;
    BulletImpactSprites bulletImpactSprites_;
    ScorePopupSystem scorePopups_;
    ScorePopupSprites scorePopupSprites_;
    SpecialExplosionSystem specialExplosions_;
    SpecialExplosionSprites specialExplosionSprites_;
    std::vector<SpecialExplosionEvent> specialExplosionEvents_; // scratch: se llena de nuevo cada Update
    std::vector<SpecialDirectKillEvent> specialDirectKillEvents_; // scratch: se llena de nuevo cada Update
    PowerUpSystem powerUps_;
    Texture2D starTexture_{};
    Texture2D helmetTexture_{};
    Texture2D gunTexture_{};
    Texture2D lifeTexture_{};
    Texture2D grenadeTexture_{};
    Texture2D shovelTexture_{};
    Texture2D clockTexture_{};
    Texture2D brickUnitTextures_[2]{}; // ver BrickUnit.h: 0 = liso, 1 = esquina/junta
    Texture2D steelUnitTexture_{};      // ver SteelUnit.h: unico frame, se repite en la grilla 2x2
    Texture2D treesTexture_{};          // arbusto: bloque entero (no por unidad), se dibuja encima de tanques/balas
    Texture2D waterTextures_[2]{};       // agua: 2 frames que alternan para animar el oleaje
    Texture2D iceTexture_{};             // hielo: hace patinar al tanque y enfria el arma el doble de rapido (ver Tank)
    Texture2D baseEagleTexture_{};       // aguila (Tanques.png, misma fila que agua/arbusto/hielo): objeto a defender, un impacto la destruye y termina la partida
    Texture2D baseEagleDestroyedTexture_{}; // frame de aguila destruida (Tanques.png, celda siguiente a la de arriba), pedido explicitamente: queda dibujada asi en vez de desaparecer (ver TileType::BaseDestroyed)
    Music battleLoopMusic_{}; // pedido explicitamente: bucle infinito mientras haya enemigos en el nivel (ver TickAudio)
    Music movementLoopMusic_{}; // pedido explicitamente: bucle infinito mientras al menos un jugador se este moviendo (ver TickAudio)
    Sound scoreTickSound_{};      // cada vez que sube el recuento de enemigos eliminados en la pantalla de puntuacion (ver TickScoreTallySequence)
    Sound hitNonLethalSound_{};   // un jugador o enemigo recibe un disparo que no lo destruye
    Sound blockResistSound_{};    // una bala pega en hierro (u otro obstaculo indestructible) y no lo rompe
    Sound brickBreakSound_{};     // se rompe un ladrillo
    Sound playerShootSound_{};    // un jugador dispara (normal o especial)
    Sound tankDeathSound_{};      // un jugador o enemigo muere
    Sound scoreBonusSound_{};     // termina el recuento de la pantalla de puntuacion y se dan los puntos extra
    Sound bigExplosionSound_{};   // se destruye el aguila, o explota la bala especial
    Sound itemAppearSound_{};     // aparece un power-up en el mapa
    Sound pauseToggleSound_{};    // se abre el menu de pausa, o se cierra para seguir jugando
    Sound itemPickupSound_{};     // un jugador o enemigo agarra un power-up (todos menos la vida extra)
    Sound extraLifeSound_{};      // el contador de vidas de algun jugador sube en una
    Sound stageStartSound_{};     // empieza un nivel (ver BeginLevelTransition)
    Sound specialShootSound_{};   // un jugador dispara el disparo especial (distinto del disparo normal, ver playerShootSound_)
    Texture2D hudLifeIconTexture_{};     // icono chico (Tanques.png, junto a "1P") que reemplaza la "X" en "P1 X 3" del HUD
    Texture2D stageFlagIconTexture_{};   // icono de bandera (Tanques.png, junto a "STAGE"), arriba de la barra izquierda
    SpawnFlashSprites spawnFlashSprites_;
    ShieldSprites shieldSprites_;
    double screenShakeTimer_ = 0.0; // onda expansiva del disparo especial
    FriendlyFireMode friendlyFireMode_ = FriendlyFireMode::Off;
    bool showGrid_ = false; // grilla de referencia sobre el mapa (boton de prueba F6, ver kDebugFeaturesEnabled), para ubicar bloques al diseñar el escenario
    bool showHitboxes_ = false; // recuadros verdes de colision de cada objeto (boton de prueba H, ver kDebugFeaturesEnabled)
    int lastGamepadButtonPressed_ = -1; // debug: para calibrar a que numero de boton responde cada mando (ver Render)

    // Estado del stick analogico izquierdo del mando del menu, cuadro
    // anterior (ver ProcessMenuInput): el D-pad ya tiene deteccion de flanco
    // gratis via IsGamepadButtonPressed, pero el stick analogico no — sin
    // guardar si YA estaba inclinado en cada direccion el cuadro anterior,
    // mantenerlo inclinado repetiria el movimiento sin control cada cuadro.
    bool menuStickUpHeld_ = false;
    bool menuStickDownHeld_ = false;
    bool menuStickLeftHeld_ = false;
    bool menuStickRightHeld_ = false;
    bool gameOver_ = false; // el aguila fue destruida O todos los jugadores activos se quedaron sin vidas (ver TriggerGameOver); mientras sea true, los jugadores quedan congelados pero el resto del juego sigue (ver GameOverPhase)
    int windowWidth_ = 0;
    int windowHeight_ = 0;

    // Secuencia de fin de partida (ver GameOverPhase/TriggerGameOver).
    GameOverPhase gameOverPhase_ = GameOverPhase::None;
    double gameOverTimer_ = 0.0;        // progreso de Rising, o cuenta regresiva de Holding
    int gameOverWinnerOwnerId_ = -1;    // jugador activo con mas puntaje al terminar Holding (ver FinishGameOverHolding); -1 = ninguno (no deberia pasar si hubo al menos 1 activo)
    int gameOverWinnerScore_ = 0;       // su puntaje final, lo que se guarda como highScore_ si se confirma el nombre
    std::string gameOverNameEntry_;     // texto que se va tipeando en GameOverPhase::NameEntry (ver ProcessGameOverInput)
    Texture2D gameOverTexture_{};       // "GAME OVER" (2 renglones), extraido de Documentaciones/Tanques.png

    // Puntaje maximo que HABIA antes de que arrancara la partida actual
    // (foto de highScore_ tomada en ResetState): highScore_ se puede ir
    // actualizando en caliente durante la partida (ver MaybeUpdateHighScore),
    // asi que compararse contra el ya no sirve para saber si ESTA partida
    // rompio records — hay que compararse contra este valor congelado.
    int matchStartHighScore_ = 0;

    // Nombre del dueño del record historico (ver highScoreHolderName_ en el
    // menu principal y GameOverPhase::NameEntry/ConfirmGameOverNameEntry).
    // Persistido en highscore.txt junto al numero; vacio si nunca se cargo
    // uno (partidas de antes de que esto existiera, o el record nunca se
    // supero desde entonces).
    std::string highScoreHolderName_;

    // Pantalla de titulo/menu (ver AppState/MenuScreen arriba y
    // ProcessMenuInput/RenderMenu en Game.cpp).
    AppState appState_ = AppState::Menu;
    MenuScreen menuScreen_ = MenuScreen::Main;
    int mainMenuIndex_ = 0;         // 0=Arcade 1=Supervivencia 2=Versus 3=Modo construccion 4=Opciones 5=Salir
    int mainConfirmExitIndex_ = 1;  // 0=Si 1=No (MenuScreen::MainConfirmExit); arranca en No por seguridad
    int modeSubmenuIndex_ = 0;      // 0=Local 1=Lan ("Opciones" solo esta en la pantalla principal)
    int optionsIndex_ = 0;          // 0=Fuego amigo 1=Pantalla 2=Mapeo de controles
    int controlMappingIndex_ = 0;   // 0-3, Jugador 1-4 (ver MenuScreen::ControlMapping)
    std::array<PlayerKeyBindings, 4> playerBindings_{}; // se llenan en InitDefaultKeyBindings; editables desde el menu
    std::array<PlayerGamepadBindings, 4> playerGamepadBindings_{}; // se llenan en InitDefaultGamepadBindings; editables desde el menu
    int controlDeviceIndex_ = 0;    // 0=Teclado 1=Mando (ver MenuScreen::ControlDeviceChoice)
    bool mappingGamepad_ = false;   // que dispositivo eligio en ControlDeviceChoice: PlayerActions/CapturingKey leen/escriben playerGamepadBindings_ en vez de playerBindings_ si es true
    int mappingPlayerIndex_ = 0;    // 0-3, que jugador se esta configurando (ver MenuScreen::PlayerActions)
    int playerActionIndex_ = 0;     // 0-6 (indice dentro de kKeyMappableActions) en teclado, 0-2 (indice dentro de kGamepadMappableActions) en mando; que fila esta resaltada en PlayerActions
    int capturedKey_ = 0;           // ultima tecla o boton de mando detectado en CapturingKey; 0 = ninguna todavia
    int selectedMainModeIndex_ = 0; // que modo (0-2) abrio el ModeSubmenu actual
    int highScore_ = 0;

    // Red LAN (Fase 7, fundacion nada mas: conexion + handshake +
    // descubrimiento, ver src/net/. Todavia NO hay gameplay real viajando
    // por la red, ver el plan de esta fase). netHost_ vale tanto para
    // hostear (rol servidor) como para conectarse (rol cliente); nunca los
    // dos a la vez. lanAnnouncer_ solo corre mientras se hostea (emite el
    // broadcast, ver MenuScreen::LanHosting); lanBrowser_ solo mientras se
    // busca (escucha ese broadcast, ver MenuScreen::LanBrowsing).
    bc::net::NetHost netHost_;
    bc::net::LanAnnouncer lanAnnouncer_;
    bc::net::LanBrowser lanBrowser_;
    int lanModeChoiceIndex_ = 0;   // 0=Hostear 1=Buscar 2=Conectar por IP (ver MenuScreen::LanModeChoice)
    int lanBrowsingIndex_ = 0;     // que partida de lanBrowser_.Hosts() esta resaltada (ver MenuScreen::LanBrowsing)
    std::string lanIpEntry_;       // IP que se va tipeando en MenuScreen::LanConnectIp
    std::string lanStatusMessage_; // texto de estado en MenuScreen::LanConnecting (conectando/aceptado/rechazado/caido)
    bool lanClientAccepted_ = false; // true una vez que el servidor acepto al cliente (ver TickLanNetworking)

    void EnterLanModeChoice();
    // Procesa los eventos pendientes de netHost_/lanAnnouncer_/lanBrowser_
    // (llamar 1 vez por paso fijo, ver Update()); actualiza
    // lanStatusMessage_/lanClientAccepted_ segun lo que vaya llegando.
    void TickLanNetworking(double dt);
    // Corta host/busqueda/conexion en curso y vuelve todo a como estaba
    // antes de entrar a la Fase 7 (se llama al volver atras del todo, o al
    // salir del menu de LAN por cualquier camino).
    void StopLanActivity();

    // Gameplay real en red (Fase 7 completa, ver el plan): lobby
    // distribuido (M1), input del cliente (M2), snapshot del mundo (M3) y
    // terreno (M4). slotOwnerPeer_[i]/slotOwnerLocalIndex_[i] dicen a quien
    // pertenece cada casillero i (0-3) del lobby/partida: 0 = un
    // dispositivo local de ESTA pc (el host, o el propio cliente si Role()
    // es Client — el servidor y cada cliente lo llenan con su punto de
    // vista), 1-3 = un cliente remoto conectado (numero de slot de red, ver
    // NetHost::Slot); slotOwnerLocalIndex_[i] es el indice de dispositivo
    // local (0-3) EN LA PC duena de ese casillero. El servidor es quien
    // decide esto (ver HandleLobbyClaimFromPeer); los clientes solo lo
    // reciben (ver ApplyLobbySlotUpdate) para saber cuales de sus propios
    // dispositivos ya estan adentro.
    int slotOwnerPeer_[4] = {0, 0, 0, 0};
    int slotOwnerLocalIndex_[4] = {0, 1, 2, 3};
    bool matchStartedOverNetwork_ = false; // cliente: true una vez que llego MatchStartMsg
    bool terrainReceivedOverNetwork_ = false; // cliente: true una vez que llego el primer TerrainSyncMsg (hasta entonces no dibuja el mapa: vendria vacio/desactualizado)
    std::uint32_t lastSentTerrainHash_ = 0;   // servidor: para no remandar TerrainSyncMsg si el terreno no cambio este paso fijo (ver TickServerTerrainSync)

    // Servidor: ultimo input local conocido de cada (cliente remoto,
    // indice local 0-3) — se sigue usando si un paso fijo no trae paquete
    // nuevo de ese cliente, en vez de tratarlo como "sin apretar nada"
    // (pedido implicito: 1 paquete perdido no debe frenar en seco al
    // jugador).
    std::array<std::array<PlayerInput, net::kMaxLocalSlotsPerPeer>, 3> remotePeerLocalInputs_{};

    void EnterCharacterSelectAsClient();       // versión de EnterCharacterSelect cuando Role()==Client: no auto-activa el slot 0 (nadie entra solo en red)
    void ProcessCharacterSelectInputAsClient();
    void SendLobbyClaim(int localSlot, bc::net::LobbyClaimAction action);
    void HandleLobbyClaimFromPeer(int peerSlot, const bc::net::LobbyClaimMsg& msg);
    void FreeLobbySlotsOwnedByPeer(int peerSlot); // desconexion (lobby o mid-partida, ver M5)
    void ApplyLobbySlotUpdate(const bc::net::LobbySlotUpdateMsg& msg);
    void BroadcastLobbyState();  // servidor, 1 vez por paso fijo mientras appState_==CharacterSelect
    void BroadcastMatchStart();  // servidor: cierra el lobby, arranca la partida para todos
    void HandleRawNetMessage(int fromSlot, const std::vector<std::uint8_t>& data); // despacha segun MessageType, ver NetHost::NetEventType::RawMessage

    // M2 (input) + M3 (snapshot del mundo).
    void SendLocalInputBatchToServer();  // cliente, 1 vez por paso fijo durante la partida
    void HandlePlayerInputBatchFromPeer(int peerSlot, const std::uint8_t* data, size_t size); // servidor
    void BroadcastWorldSnapshot();       // servidor, 1 vez por paso fijo durante la partida
    void ApplyWorldSnapshot(const std::uint8_t* data, size_t size); // cliente
    void SerializeWorldSnapshot(std::vector<std::uint8_t>& out) const;

    // M4 (terreno).
    std::uint32_t HashTerrain() const;
    void SerializeTerrain(std::vector<std::uint8_t>& out) const;
    void ApplyTerrainSync(const std::uint8_t* data, size_t size);
    void TickServerTerrainSync(); // servidor, 1 vez por paso fijo: remanda TerrainSyncMsg solo si HashTerrain() cambio

    // Editor de niveles integrado ("MODO CONSTRUCCION", ver el plan): port
    // de tools/level_editor/main.cpp (herramienta interna, ejecutable
    // aparte) a un AppState propio del juego. Guarda en
    // assets/levels/custom/ (custom_level_%03d.json), grupo separado de la
    // campaña Arcade (assets/levels/level_NN.json) — nunca se mezclan.
    enum class LevelBuilderMode { Browser, Editing, ConfirmDelete };
    enum class LevelBuilderDragKind { None, Player, Enemy };

    LevelBuilderMode levelBuilderMode_ = LevelBuilderMode::Browser;

    // Lienzo actual (mismos datos que LevelData pero desglosados para
    // pintar rapido, igual que el editor standalone).
    std::vector<TileType> levelBuilderCells_;
    std::vector<BlockShape> levelBuilderCellShapes_;
    std::vector<std::array<int, 2>> levelBuilderPlayerSpawns_;
    std::vector<std::array<int, 2>> levelBuilderEnemySpawns_;
    int levelBuilderWidth_ = 0;
    int levelBuilderHeight_ = 0;
    int levelBuilderBaseX_ = 0;
    int levelBuilderBaseY_ = 0;
    TileType levelBuilderSelectedType_ = TileType::Brick;
    int levelBuilderVertical_ = 0;   // 0=ninguno 1=arriba 2=abajo (ver CombineLevelBuilderShape)
    int levelBuilderHorizontal_ = 0; // 0=ninguno 1=izquierda 2=derecha
    bool levelBuilderToolActive_ = true;
    std::string levelBuilderStatusMessage_;
    int levelBuilderEditingId_ = -1; // -1 = todavia no guardado (nuevo); >=1 = pisa ese archivo al guardar
    LevelBuilderDragKind levelBuilderDragKind_ = LevelBuilderDragKind::None;
    size_t levelBuilderDragIndex_ = 0;

    // Navegador (lista con vista previa, ver el plan): ids encontrados en
    // assets/levels/custom/, y cual esta resaltado ahora.
    std::vector<int> levelBuilderCustomIds_;
    int levelBuilderBrowseIndex_ = 0;
    LevelData levelBuilderBrowsePreview_;
    bool levelBuilderBrowsePreviewValid_ = false;

    // "Jugar" un nivel personalizado: mini-campaña por TODOS los niveles
    // personalizados en orden (pedido explicitamente), reusando la misma
    // maquinaria de progresion/game over que Arcade (ver
    // LoadCurrentLevelMap/AdvanceToNextLevel/FinishGameOverSequence).
    bool playingCustomLevels_ = false;
    std::vector<int> customLevelPlayOrder_;
    int customLevelPlayIndex_ = 0;

    void EnterLevelBuilderBrowser();  // desde el menu principal
    void RefreshLevelBuilderCustomIds();
    void RefreshLevelBuilderBrowsePreview();
    void ResetLevelBuilderCanvas();   // lienzo en blanco + anillo de ladrillo por defecto (misma plantilla que el editor standalone)
    void LoadLevelDataIntoBuilderCanvas(const LevelData& level);
    LevelData BuildLevelDataFromBuilderCanvas() const;
    void SaveLevelBuilderCanvas(); // usa levelBuilderEditingId_ (nuevo -> NextCustomLevelId(); existente -> pisa)
    int NextCustomLevelId() const;
    std::string CustomLevelFilePath(int id) const;
    void StartPlayingCustomLevels(int startId);
    BlockShape CombineLevelBuilderShape() const; // segun levelBuilderVertical_/levelBuilderHorizontal_, mismo criterio que CombineShape del editor standalone
    bool IsLevelBuilderCellProtected(int x, int y) const; // el aguila y los puntos de spawn: no se pintan encima, se arrastran
    // Dibuja un tipo+forma de terreno en pantalla (usa las texturas que
    // Game::Init() ya carga para el mapa real, ver brickUnitTextures_ etc.
    // en vez de cargar duplicados): unico lugar que sabe dibujar cada tipo,
    // reusado por el lienzo, la paleta y la vista previa del navegador.
    void DrawLevelBuilderTerrainCell(TileType type, BlockShape shape, Vector2 pos, float cellSize, Color tint) const;

    void ProcessLevelBuilderInput();
    void RenderLevelBuilder();

    Texture2D titleLogoTexture_{};    // "BATTLE CITY" en ladrillos, extraido de Documentaciones/Titulo.png
    Texture2D menuSelectorTexture_{}; // tanque amarillo chico que senala la opcion resaltada

    // A que pantalla vuelve "Esc" desde MenuScreen::Options: Main si se entro
    // desde el titulo, PauseMain si se entro desde el menu de pausa (ver
    // MenuScreen::PauseMain). Options/ControlMapping/PlayerActions/
    // CapturingKey son el mismo codigo en los dos casos; solo este dato
    // cambia segun de donde se vino.
    MenuScreen optionsBackTarget_ = MenuScreen::Main;

    // Menu de pausa (pedido explicitamente, ver MenuScreen::PauseMain y
    // siguientes): paused_ congela el juego entero (Update() no hace nada
    // mientras sea true, ver Update()) y hace que ProcessInput() reuse
    // ProcessMenuInput() en vez del input de gameplay normal. Se activa con
    // el boton de Pausa de cualquier jugador o con Enter (ver ProcessInput),
    // pero nunca durante la transicion de nivel, la pantalla de puntuacion,
    // o con la partida ya terminada (aguila destruida).
    bool paused_ = false;
    int pauseMenuIndex_ = 0;      // 0=Continuar 1=Guardar 2=Cargar 3=Opciones 4=Terminar partida
    int pauseEndChoiceIndex_ = 0; // 0=Menu principal 1=Cerrar el juego (MenuScreen::PauseEndChoice)
    int pauseConfirmIndex_ = 1;   // 0=Si 1=No (MenuScreen::PauseConfirmEnd); arranca en No por seguridad

    // "Cerrar el juego" (ver MenuScreen::PauseConfirmEnd) no puede cerrar la
    // ventana en el momento (seguiria dibujando ese mismo frame despues):
    // solo pide que termine, y Run() corta el bucle principal apenas puede
    // (ver el while de Run()), pasando por Shutdown() como al cerrar con la
    // X de la ventana.
    bool wantsExit_ = false;

    // Item Pala: ver ApplyShovelFortification/TickBaseFortification.
    struct FortifiedCell {
        int x = 0;
        int y = 0;
        TileType originalType = TileType::Empty;
        // Snapshot completo (no solo el tipo) para poder restaurar exacto
        // al vencer el efecto, incluso si la celda ya estaba parcialmente
        // rota/recortada antes del gancho de hierro del item Pala (ver
        // ApplyShovelFortification) — a diferencia de antes, que revivia
        // TODAS las unidades de ladrillo sin importar como estaban.
        std::array<BrickUnit, kBrickUnitCount> originalBrickUnits{};
        std::array<SteelUnit, kSteelUnitCount> originalSteelUnits{};
    };
    int basePositionX_ = -1;
    int basePositionY_ = -1;
    std::vector<FortifiedCell> fortifiedCells_;
    double baseFortifyTimer_ = 0.0;
};

} // namespace bc
