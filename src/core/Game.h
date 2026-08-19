#pragma once

#include <array>
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
// Damage (nivel 2): el efecto depende del nivel de arma de quien dispara
//   (ver ProcessFriendlyFireDamageHit) — de "resta 1 nivel" a "atraviesa el
//   escudo y resta 2". Si el objetivo ya esta en nivel 1, cualquier impacto
//   sin escudo lo destruye: explota con la animacion del disparo especial y
//   respawnea como si hubiera perdido una vida.
enum class FriendlyFireMode { Off, Paralyze, Damage };

// Pantalla de titulo (seccion custom, pedida explicitamente con un arbol de
// opciones concreto): Menu = en algun lugar del menu (ver MenuScreen para
// cual exactamente), Playing = la partida en curso (Update/Render de
// siempre).
enum class AppState { Menu, Playing };

// Los niveles del arbol de menu. Options solo se entra desde la pantalla
// principal (Main), asi que Esc siempre vuelve ahi. ControlMapping = lista
// Jugador 1-4; PlayerActions = las 7 acciones reasignables de un jugador
// puntual, con la tecla actual al lado; CapturingKey = el prompt "Presione
// una tecla" al reasignar una.
enum class MenuScreen { Main, ModeSubmenu, Options, ControlMapping, PlayerActions, CapturingKey };

// Transicion de nivel (seccion custom, calcada del original): None = juego
// normal; ShowingText = pantalla gris solida con "NIVEL N" en el centro
// durante un tiempo fijo; Curtain = esa misma pantalla gris partida al
// medio, una mitad se va para arriba y la otra para abajo (como un telon
// que se abre) dejando ver el nivel de fondo. Mientras no sea None,
// Update() congela todo el juego (nada se mueve/dispara/spawnea) y solo
// avanza el temporizador de la transicion (ver BeginLevelTransition).
enum class LevelTransitionPhase { None, ShowingText, Curtain };

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

// Las 7 acciones de juego que se pueden reasignar por jugador (ver
// PlayerKeyBindings). Start es la tecla que une/saca a ese jugador de la
// partida (antes fija en las teclas 1/2/3/4, ver ProcessInput).
enum class InputAction { Up, Down, Left, Right, Shoot, Special, Start };
constexpr int kInputActionCount = 7;

struct PlayerKeyBindings {
    int keys[kInputActionCount] = {};
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

    // Entrada del menu (flechas/Enter/Esc) mientras appState_ != Playing;
    // reemplaza a ProcessInput() por completo ese frame (el menu no necesita
    // el resto: input de jugador, teclas de prueba, etc.).
    void ProcessMenuInput();

    // Dibuja la pantalla de titulo/menu completa (logo, puntaje maximo, la
    // lista que corresponda a menuScreen_, y el texto de ayuda de abajo).
    // Reemplaza a Render() por completo mientras appState_ != Playing.
    void RenderMenu();

    // Una linea de una lista del menu, centrada, con el tanque selector
    // (menuSelectorTexture_) a la izquierda si esta resaltada; en gris si
    // enabled es false (opciones marcadas "todavia no disponible").
    void DrawMenuLine(const char* text, float y, bool selected, bool enabled);

    // Arranca la partida de verdad: recarga el mapa/estado (ResetState) y
    // pasa a appState_ = Playing. Por ahora es el unico modo/submenu que
    // hace algo real (Arcade > Local); los demas todavia no tienen
    // gameplay propio (ver ProcessMenuInput).
    void StartArcadeLocal();

    // Puntaje maximo historico (no de esta partida sino de todas), mostrado
    // arriba de todo en el menu. Se guarda en un archivo de texto en
    // /assets junto a los niveles (mismo criterio que usa el editor).
    void LoadHighScore();
    void SaveHighScore();

    // Si candidateScore supera al maximo historico, lo actualiza (y lo
    // persiste) y recuerda a ownerId como quien lo tiene ahora (ver
    // highScoreHolderId_), para mostrar su nombre junto al puntaje maximo en
    // la pantalla de puntuacion de fin de nivel.
    void MaybeUpdateHighScore(int candidateScore, int ownerId);

    // Esquema de teclado por defecto de cada jugador (el mismo que antes
    // estaba fijo en ProcessInput: WASD/flechas/IJKL/numpad + Ctrl-Espacio/
    // Ctrl-Shift/UO/KP0-KPEnter, mas las teclas 1-4 para unirse). Se llama
    // una sola vez desde Init(); de ahi en mas playerBindings_ se edita
    // desde el menu (ver MenuScreen::CapturingKey).
    void InitDefaultKeyBindings();

    // Suma el mando gamepadId al input (no reemplaza el teclado, cualquiera
    // de los dos mueve/dispara). D-pad + stick izquierdo mueven; los botones
    // de disparo/especial se ajustan segun kGamepadShootButton/kGamepadSpecialButton
    // (ver ProcessInput). No hace nada si ese mando no esta conectado.
    void ApplyGamepadInput(PlayerInput& input, int gamepadId) const;

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

    // Activa o desactiva un jugador (teclas 1/2/3, seccion de prueba): al
    // activarlo, reinicia su tanque de cero y lo respawnea; al desactivarlo,
    // el flag pasa a false y Update()/Render() simplemente dejan de tocar a
    // ese jugador (no se mueve, no dispara, no es blanco de nada, no se
    // dibuja) hasta que se vuelva a activar. Distinto de Eliminate(): esto es
    // reversible, pensado solo para probar con menos jugadores en pantalla.
    void SetPlayerActive(bool& activeFlag, Tank& tank, SpawnFlash& spawn, float spawnX, float spawnY, bool active);

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
    // ver SpecialExplosionEvent::directHitOwnerId): con escudo, se pierde el
    // escudo y 1 nivel; sin escudo, 2 niveles (salvo choque directo en nivel
    // 4: -3 niveles y paralizado 5s). Si ya estaba en nivel 1, en vez de
    // bajar mas queda destruido (devuelve true) y quien llama debe
    // respawnearlo.
    bool ApplyExplosionSelfDamage(Tank& tank, int ownerId);

    // Anima la muerte de un tanque (explosion chica a escala nativa, ver
    // Documentaciones/Exploción.png) y le resetea el nivel de arma. No
    // respawnea: eso queda a cargo de quien llama.
    void DestroyTank(Tank& tank);

    // Fuego amigo (ver FriendlyFireMode): si el modo esta activo, mata las
    // balas del OTRO jugador que esten sobre este tanque y le aplica el
    // efecto que corresponda al modo actual. Devuelve true si el tanque
    // quedo destruido, y quien llama debe respawnearlo.
    bool ApplyFriendlyFire(Tank& tank, int ownerId);

    // Nivel 2 de fuego amigo: efecto de un solo impacto sobre tank, segun el
    // nivel de arma de quien disparo (shooterWeaponLevel). Devuelve true si
    // el tanque quedo destruido (explota y se resetea; RespawnPlayerN queda
    // a cargo de quien llama).
    bool ProcessFriendlyFireDamageHit(Tank& tank, int shooterWeaponLevel);

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
    void RenderPlayerHud(const Tank& tank, const char* label, int centerX, int y, int score);

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

    // Animacion de la fila de un jugador en la pantalla de puntuacion: que
    // slot de tipo de enemigo (0-3) esta revelando ahora (4 = fila
    // terminada, ya se ve TOTAL), el numero que va mostrando ese slot
    // mientras cuenta, y el acumulador de tiempo para el proximo tick (ver
    // TickScoreTally).
    struct ScoreTallyRowAnim {
        int currentSlot = 0;
        int currentCount = 0;
        double timer = 0.0;
    };
    std::array<ScoreTallyRowAnim, 4> scoreTallyAnim_{};

    // Bono de fin de nivel (pedido explicito): al terminar de contar todas
    // las filas, el/los jugador(es) con mayor scoreGained en
    // playerLevelStats_ (empate incluido) lo tienen en true — reciben
    // kScoreTallyClearBonus extra (ya sumado a su puntaje real) que se
    // muestra en rojo a la derecha de su TOTAL, sin mezclarse con ese numero
    // (ver TickScoreTally/RenderScoreTally). Se arma recien al pasar a
    // ScoreTallyPhase::Holding, todo en false mientras se cuenta.
    std::array<bool, 4> scoreTallyBonusAwarded_{};

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
    Texture2D hudLifeIconTexture_{};     // icono chico (Tanques.png, junto a "1P") que reemplaza la "X" en "P1 X 3" del HUD
    Texture2D stageFlagIconTexture_{};   // icono de bandera (Tanques.png, junto a "STAGE"), arriba de la barra izquierda
    SpawnFlashSprites spawnFlashSprites_;
    ShieldSprites shieldSprites_;
    double screenShakeTimer_ = 0.0; // onda expansiva del disparo especial
    FriendlyFireMode friendlyFireMode_ = FriendlyFireMode::Off;
    bool showGrid_ = false; // grilla de referencia sobre el mapa (boton de prueba F6, ver kDebugFeaturesEnabled), para ubicar bloques al diseñar el escenario
    int lastGamepadButtonPressed_ = -1; // debug: para calibrar a que numero de boton responde cada mando (ver Render)
    bool gameOver_ = false; // el aguila (TileType::Base) recibio un impacto: se congela todo hasta reiniciar (ESC)
    int windowWidth_ = 0;
    int windowHeight_ = 0;

    // Pantalla de titulo/menu (ver AppState/MenuScreen arriba y
    // ProcessMenuInput/RenderMenu en Game.cpp).
    AppState appState_ = AppState::Menu;
    MenuScreen menuScreen_ = MenuScreen::Main;
    int mainMenuIndex_ = 0;         // 0=Arcade 1=Supervivencia 2=Versus 3=Modo construccion 4=Opciones
    int modeSubmenuIndex_ = 0;      // 0=Local 1=Lan ("Opciones" solo esta en la pantalla principal)
    int optionsIndex_ = 0;          // 0=Fuego amigo 1=Pantalla 2=Mapeo de controles
    int controlMappingIndex_ = 0;   // 0-3, Jugador 1-4 (ver MenuScreen::ControlMapping)
    std::array<PlayerKeyBindings, 4> playerBindings_{}; // se llenan en InitDefaultKeyBindings; editables desde el menu
    int mappingPlayerIndex_ = 0;    // 0-3, que jugador se esta configurando (ver MenuScreen::PlayerActions)
    int playerActionIndex_ = 0;     // 0-6 (InputAction), que accion esta resaltada en PlayerActions
    int capturedKey_ = 0;           // ultima tecla detectada en CapturingKey; 0 = ninguna todavia
    int selectedMainModeIndex_ = 0; // que modo (0-2) abrio el ModeSubmenu actual
    int highScore_ = 0;
    Texture2D titleLogoTexture_{};    // "BATTLE CITY" en ladrillos, extraido de Documentaciones/Titulo.png
    Texture2D menuSelectorTexture_{}; // tanque amarillo chico que senala la opcion resaltada

    // Item Pala: ver ApplyShovelFortification/TickBaseFortification.
    struct FortifiedCell {
        int x = 0;
        int y = 0;
        TileType originalType = TileType::Empty;
    };
    int basePositionX_ = -1;
    int basePositionY_ = -1;
    std::vector<FortifiedCell> fortifiedCells_;
    double baseFortifyTimer_ = 0.0;
};

} // namespace bc
