#pragma once

// Editor de niveles. Sirve para dos cosas:
//   - integrado al juego (CONSTRUCCION): guarda en 40 casilleros propios, que se juegan aparte;
//   - herramienta aparte (level_editor.exe): agrega o cambia niveles del modo principal (hasta el 99).
// La logica no depende de ninguna ventana: recibe las teclas/mouse de cada cuadro (EditorKeys) y arma lo que hay que dibujar
// (EditorView); el dibujo lo hace Renderer::RenderEditor.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "levels.h"
#include "renderer.h"

namespace app {

struct EditorKeys {
    bool left = false, right = false, up = false, down = false;     // mantenidos (el editor aplica la repeticion)
    bool place = false, placeHeld = false;                          // poner (A / clic izquierdo): apretado este cuadro / mantenido
    bool erase = false, eraseHeld = false;                          // borrar (B / clic derecho)
    bool prevItem = false, nextItem = false;                        // bloque anterior / siguiente de la paleta
    bool tool = false;                                              // siguiente herramienta
    bool menu = false;                                              // abrir el menu (Start / Esc)
    bool back = false;                                              // volver atras (Esc / B)
    bool accept = false;                                            // aceptar en menus (Enter / A)
    int mx = -1, my = -1;                                           // mouse en pixeles de la pantalla del juego (-1: no hay)
    int wheel = 0;
};

class LevelEditor {
public:
    enum class Target { Own, Main };
    enum class Tool { Block = 0, Spawn = 1, Eagle = 2 };

    explicit LevelEditor(Target target);

    void Step(const EditorKeys& keys);                 // un cuadro
    void BuildView(EditorView& view);                  // lo que hay que dibujar ahora
    const nes::Battle& Stage();                        // el campo tal como lo va a armar el juego
    bool WantsExit() const { return exit_; }
    void SetHelp(const std::string& h) { help_ = h; }   // linea de ayuda de teclas (cambia segun el teclado o el mando)
    Target target() const { return target_; }

    // Para pruebas y para el programa
    const LevelDef& def() const { return def_; }
    LevelDef& mutableDef() { dirty_ = true; stageDirty_ = true; return def_; }
    int CurrentSlot() const { return slot_; }
    bool Dirty() const { return dirty_; }
    bool InEditPage() const { return page_ == Page::Edit; }
    int CursorCol() const { return col_; }
    int CursorRow() const { return row_; }
    Tool CurrentTool() const { return tool_; }
    bool SaveToSlot(int slot);                          // guarda el nivel actual en ese casillero / numero
    bool LoadFromSlot(int slot);                        // carga ese casillero / numero (sin preguntar nada)
    void NewLevel(bool boss);
    static bool Protected(int col, int row);            // lugar de la base y de los jugadores: no se pinta
    static const std::vector<int>& PaletteIds();
    int SlotCount() const { return target_ == Target::Main ? kMaxMainLevel : kOwnSlots; }
    int SlotState(int slot) const;                      // 0 vacio, 1 con archivo, 2 de fabrica (sin archivo propio)

private:
    enum class Page { Edit, Menu, Slots, Confirm, Message };
    enum class SlotMode { Save, Load };
    enum class Ask { Overwrite, Delete, NewLevel, Exit, LoadDiscard };

    bool Rep(bool held, int& timer);                    // true al apretar y luego con repeticion
    void MoveCursor(const EditorKeys& k);
    void ApplyTool(bool erase);
    void PlaceBlock(int id);
    void ToggleSpawn(bool removeOnly);
    void PlaceEagle();
    void SetKind(int kind);                              // 0 normal, 1 especial, 2 batalla vs
    void SetBoss(bool on);
    bool IsProtected(int col, int row) const { return !def_.versus && Protected(col, row); }
    void SetRank(int rank);
    void CycleTool();
    void StepMenu(const EditorKeys& k);
    void StepSlots(const EditorKeys& k);
    void StepConfirm(const EditorKeys& k);
    void OpenMenu();
    void OpenSlots(SlotMode mode);
    void AskConfirm(Ask what, const std::string& text, int slot);
    void Message(const std::string& text);
    void RebuildStage();
    std::string SlotLabel(int slot) const;
    std::string FileOf(int slot) const;
    std::vector<std::string> MenuItems() const;
    bool MouseOverBar(int my) const;

    Target target_;
    LevelDef def_;
    std::unique_ptr<nes::Battle> stage_;
    bool stageDirty_ = true;
    bool dirty_ = false;
    bool exit_ = false;
    int slot_ = 0;                  // casillero abierto (0 = nivel nuevo sin guardar)
    int col_ = 13, row_ = 7;
    Tool tool_ = Tool::Block;
    int palette_ = 1;               // posicion en PaletteIds()
    Page page_ = Page::Edit;
    int menuSel_ = 0;
    SlotMode slotMode_ = SlotMode::Save;
    int slotSel_ = 1;
    Ask ask_ = Ask::Exit;
    int askSlot_ = 0;
    int confirmSel_ = 1;            // 0 = si, 1 = no
    std::string confirmText_;
    std::vector<std::string> message_;
    int timers_[6] = {};            // repeticion de izquierda, derecha, arriba, abajo (+ 2 de reserva)
    bool prevPlace_ = false;
    int lastPaintCol_ = -1, lastPaintRow_ = -1;
    bool barTop_ = false;           // la barra de herramientas esta arriba (se mueve para no tapar el cursor)
    bool prevMenuKey_ = false;
    std::string help_;
};

} // namespace app
