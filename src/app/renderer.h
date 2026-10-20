#pragma once

// Renderer por software de la escena de la NES (fondo + sprites de 8x16) a
// partir de la RAM del port y de los graficos originales (CHR). Sin raylib:
// escribe en un buffer RGBA, asi se puede usar tambien en pruebas sin ventana.
//
// La salida es 16:9 (426x240): el campo clasico de 13x13 bloques va centrado,
// el panel de la derecha (HUD original) pegado al campo, y a los costados
// paneles extra para la ayuda de pruebas.

#include <cstdint>
#include <string>
#include <vector>

#include "../nes/battle.h"
#include "flow.h"
#include "levels.h"

namespace app {

constexpr int kScreenW = 432;
constexpr int kScreenH = 240;

struct HudInfo {
    std::vector<std::string> pauseItems;   // menu de pausa (vacio = no se muestra)
    int pauseSelected = 0;
    int curtain = 0;                       // filas (0..15) de cortina gris tapadas arriba y abajo
    std::string introText;                 // en lugar del "STAGE N" de la cortina (por ejemplo "BATALLA VS")
    std::string introPrompt;               // segunda linea bajo el STAGE N (por ejemplo "APRETA START")
    std::string introSubtitle;             // texto bajo el STAGE N de la cortina (niveles del aguila enemiga)
    int introLevel = 0;                    // numero que muestra la cortina cerrada
    std::string banner;                    // cartel centrado (por ejemplo "NIVEL 3 COMPLETADO")
    bool overlay = false;                  // cartel con la ayuda y el estado (tecla Esc)
    std::vector<std::string> leftLines;    // ayuda de teclas
    std::vector<std::string> rightLines;   // estado de la prueba
    int players = 1;
    int stage = 1;
    bool paused = false;
};

// Menu generico (titulo, jugar, configuraciones...) y seleccion de jugadores del modo local.
struct MenuView {
    int scrollY = 0;                       // titulo: cuanto falta para que suba a su lugar (240 -> 0)
    int p1Score = 0, hiScore = 0;          // puntajes del titulo
    bool logo = false;                     // muestra BATTLE CITY con letras enormes de ladrillos
    std::string title;
    std::vector<std::string> items;
    std::vector<bool> enabled;
    int selected = 0;
    std::string note;
                                           // linea de aviso (por ejemplo "PROXIMAMENTE")
    std::string footer;
    std::string version;                   // titulo: la version del juego, abajo a la derecha
    std::string cornerNote;                // titulo: aviso abajo a la izquierda (por ejemplo "ACTUALIZACION DISPONIBLE")
};

struct LobbyView {
    static constexpr int kSlots = 8;       // lugares (la sala normal usa 4; la batalla vs en red, hasta 8)
    bool joined[kSlots] = {};
    int color[kSlots] = {0, 1, 2, 3, 4, 5, 6, 7};           // 0..7 (ver app::kColorNames)
    std::string joinKey[kSlots];           // nombre de la tecla (o del boton) de disparo de cada ranura
    bool grid8 = false;                    // battle royale en red: 8 lugares sueltos (dos filas de 4), sin equipos
    int teamSize = 0;                      // batalla vs en red: tanques por equipo (2, 3 o 4); 0 = la sala de siempre
    std::string hint, startText, backText, upDownText;   // textos que dependen de si se usa teclado o mando
    // Sala de espera en red (modo LAN): nombre de cada jugador, quien esta listo, cual soy yo y los textos que cambian
    bool net = false;
    std::string name[kSlots];
    bool ready[kSlots] = {};
    bool mine[kSlots] = {};                 // jugadores de esta PC
    std::string note;
    std::string meLine2[kSlots];                 // segunda linea de ayuda en cada casilla propia (por ejemplo "F LISTO")
    std::string addHint;                    // como sumar otro jugador de esta PC
    std::string title;                      // titulo propio (si esta vacio, el de siempre)
    std::string joinLine1, joinLine2;       // en las casillas libres (por defecto PRESIONA / DISPARO)
    std::string tag[kSlots];                // en lugar del nombre del color (por ejemplo "EQUIPO 1")
};


// Editor de niveles: lo que hay que dibujar (lo arma LevelEditor::BuildView).
constexpr int kEditorBarH = 30;                      // alto de la barra de herramientas
constexpr int kEditorToolX[3] = {4, 62, 148};         // x de cada herramienta en la barra
constexpr int kEditorPalX = 4;                        // x del primer bloque de la paleta
constexpr int kEditorPalStep = 18;                    // separacion entre bloques de la paleta

struct EditorView {
    int cursorCol = 0, cursorRow = 0;
    bool showCursor = true;
    int nSpawns = 0;
    uint8_t spawnCol[kMaxSpawns] = {}, spawnRow[kMaxSpawns] = {};
    bool boss = false;
    bool versus = false;                              // batalla vs: dos aguilas
    int vsEagleCol[2] = {1, 25}, vsEagleRow[2] = {13, 1};
    int eagleCol = 13, eagleRow = 1, eagleReach = 1;
    int tool = 0;                                     // 0 bloque, 1 aparicion, 2 aguila
    int palette = 0;
    std::vector<int> paletteIds;
    bool barTop = true;
    std::string help, status;
    std::vector<std::string> warnings;
    int page = 0;                                     // 0 edicion, 1 menu, 2 casilleros, 3 confirmar, 4 mensaje
    std::vector<std::string> menuItems;
    int menuSel = 0;
    int slotCols = 8, slotCount = 40, slotSel = 1;
    std::string slotTitle, slotInfo, slotLegend;
    std::vector<int> slotState;                       // 0 vacio, 1 propio, 2 de fabrica
    std::string confirmText;
    int confirmSel = 1;
    std::vector<std::string> message;
    bool slotOnly = false;                            // solo la lista de casilleros (sin el mapa de fondo)
    std::string slotFooter;                           // pie de la lista (si esta vacio, el de siempre)
};

class Renderer {
public:
    // Carga los 8 KB de CHR (sacados de la ROM). Devuelve false si falla.
    bool LoadChr(const std::string& path);
    bool LoadChrData(const uint8_t* data, size_t size);       // desde memoria (Android: viene dentro del APK)

    // Dibuja un cuadro 16:9 en rgba (kScreenW * kScreenH * 4 bytes).
    void Render(const nes::Battle& battle, const HudInfo& hud, std::vector<uint8_t>& rgba) const;

    // Pantalla de recuento de puntos (hasta 4 jugadores) y pantalla de GAME OVER.
    void RenderScoreScreen(const ScoreTally& tally, std::vector<uint8_t>& rgba) const;
    void RenderGameOver(std::vector<uint8_t>& rgba) const;
    void RenderContinue(int continues, int seconds, bool canAccept, std::vector<uint8_t>& rgba) const;      // "CONTINUAR?" con los continues que quedan
    void RenderHighScore(int score, int frame, std::vector<uint8_t>& rgba) const;
    void RenderMenu(const MenuView& menu, std::vector<uint8_t>& rgba) const;
    void RenderLobby(const LobbyView& lobby, std::vector<uint8_t>& rgba) const;
    void RenderEditor(const nes::Battle& stage, const EditorView& view, std::vector<uint8_t>& rgba) const;

    // La pantalla NES pura de 256x240 (sin paneles), como 0x00RRGGBB.
    void RenderNesScreen(const nes::Battle& battle, std::vector<uint32_t>& rgb) const;

private:
    // pixel (0..3) de un tile 8x8 de la tabla de patrones 0 (sprites) o 1 (fondo)
    int ChrPixel(int table, int tile, int x, int y) const;
    int FontPixel(int code, int x, int y) const;       // letras, digitos y los caracteres propios (X, Z, tilde, acentos, signos)
    bool HasGlyph(int code) const;
    // Ajustes para que un texto nunca se salga de la pantalla: Fit lo corta con "..." y Wrap lo parte en renglones (por palabras).
    std::string Fit(const std::string& text, int maxPx) const;
    std::vector<std::string> Wrap(const std::string& text, int maxPx) const;
    int TextWidth(const std::string& text) const;      // ancho en px; los iconos {A} {B} {X} {Y} {DPAD} {START} miden 14
    void DrawIcon(std::vector<uint32_t>& canvas, int w, int x, int y, const std::string& token) const;
    void DrawText(std::vector<uint32_t>& canvas, int w, int x, int y, const std::string& text, uint32_t color) const;
    void DrawHudTile(std::vector<uint32_t>& canvas, int w, int x, int y, int tile, const nes::Battle& battle, int palette) const;
    void DrawBlockIcon(std::vector<uint32_t>& canvas, int x, int y, int id, const nes::Battle& battle) const;
    void DrawSprite16(std::vector<uint32_t>& canvas, int w, int x, int y, int tileBase, int palette, int scale = 1) const;
    void DrawBrickLetters(std::vector<uint32_t>& canvas, int w, const char* text, int x0, int y0, int bgSet) const;
    void DrawBombing(std::vector<uint32_t>& world, int w, const nes::Battle& battle) const;
    void DrawBlastPiece(std::vector<uint32_t>& canvas, int w, int x, int y, int tile, int palette, int size) const;
    void DrawColoredTile(std::vector<uint32_t>& canvas, int w, int x, int y, int tile, const uint32_t colors[4]) const;
    void DrawWideHud(std::vector<uint32_t>& world, int w, const nes::Battle& battle, const HudInfo& hud) const;
    void DrawWideHudOverlay(std::vector<uint32_t>& world, int w, const nes::Battle& battle, const HudInfo& hud) const;
    void DrawHudTileT(std::vector<uint32_t>& canvas, int w, int x, int y, int tile, const nes::Battle& battle, int palette) const;
    void RenderWide(const nes::Battle& battle, const HudInfo& hud, std::vector<uint32_t>& canvas) const;
    void FinishFrame(std::vector<uint32_t>& canvas, const HudInfo& hud, std::vector<uint8_t>& rgba) const;

    std::vector<uint8_t> chr_;
};

} // namespace app
