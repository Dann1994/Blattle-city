#include "editor.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace app {

namespace {

// Orden de la paleta (ids de bloque): ladrillo entero y sus cuatro mitades, hierro entero y sus mitades, agua, arbusto, hielo y vacio.
const std::vector<int> kPalette = {4, 0, 1, 2, 3, 9, 5, 6, 7, 8, 10, 11, 12, 14, 15, 13};      // ... hielo, arena (14), lava (15) y vacio

enum MenuId { kMiContinue, kMiType, kMiDifficulty, kMiRank, kMiBomb, kMiLayers, kMiSave, kMiSaveAs, kMiLoad, kMiNew, kMiExit };

std::vector<MenuId> MenuIds(bool boss, bool versus) {
    std::vector<MenuId> ids = {kMiContinue, kMiType, kMiDifficulty};
    if (boss) ids.push_back(kMiRank);
    if (versus) { ids.push_back(kMiBomb); ids.push_back(kMiLayers); }
    for (MenuId m : {kMiSave, kMiSaveAs, kMiLoad, kMiNew, kMiExit}) ids.push_back(m);
    return ids;
}

}  // namespace

const std::vector<int>& LevelEditor::PaletteIds() { return kPalette; }

LevelEditor::LevelEditor(Target target) : target_(target), def_(BlankLevelDef(false)) {
    stage_ = std::make_unique<nes::Battle>();
}

bool LevelEditor::Protected(int col, int row) {
    // la base de los jugadores y los cuatro lugares donde empiezan
    if (row == kGridRows - 1 && col >= 10 && col <= 16) return true;
    if (row == kGridRows - 2 && col >= 12 && col <= 14) return true;
    return false;
}

int LevelEditor::SlotState(int slot) const {
    if (target_ == Target::Own) return OwnSlotHasFile(slot) ? 1 : 0;
    if (MainLevelHasFile(slot)) return 1;
    return slot <= kLevelCount ? 2 : 0;
}

std::string LevelEditor::FileOf(int slot) const {
    return target_ == Target::Own ? OwnLevelFile(slot) : MainLevelFile(slot);
}

bool LevelEditor::Rep(bool held, int& timer) {
    if (!held) { timer = 0; return false; }
    ++timer;
    return timer == 1 || (timer > 14 && (timer - 14) % 3 == 0);
}

void LevelEditor::RebuildStage() {
    stage_ = std::make_unique<nes::Battle>();
    stage_->SetWide(true);
    nes::Battle::StageSpec spec;
    uint8_t blocks[kLevelRows][kLevelCols];
    uint8_t strip[kLevelCols];
    LevelDefToStage(def_, blocks, strip, spec);
    stage_->SetStageSpec(spec);
    stage_->SetLevel(1);
    stage_->NewGame(1, 1);
    stage_->LoadWideStage(blocks, strip);
    for (int i = 0; i < 64; ++i) stage_->R(static_cast<uint16_t>(nes::ram_oam + i * 4)) = 0xF0;      // sin sprites
    stageDirty_ = false;
}

const nes::Battle& LevelEditor::Stage() {
    if (stageDirty_) RebuildStage();
    return *stage_;
}

void LevelEditor::NewLevel(bool boss) {
    def_ = BlankLevelDef(boss);
    slot_ = 0;
    dirty_ = false;
    stageDirty_ = true;
    tool_ = Tool::Block;
}

bool LevelEditor::SaveToSlot(int slot) {
    if (slot < 1 || slot > SlotCount()) return false;
    if (!SaveLevelDef(FileOf(slot), def_)) return false;
    slot_ = slot;
    dirty_ = false;
    return true;
}

bool LevelEditor::LoadFromSlot(int slot) {
    if (slot < 1 || slot > SlotCount()) return false;
    LevelDef d;
    const bool ok = target_ == Target::Own ? LoadOwnLevelDef(slot, d) : LoadMainLevelDef(slot, d);
    if (!ok) return false;
    def_ = d;
    slot_ = slot;
    dirty_ = false;
    stageDirty_ = true;
    if (!def_.boss && tool_ == Tool::Eagle) tool_ = Tool::Block;
    return true;
}

void LevelEditor::PlaceBlock(int id) {
    if (IsProtected(col_, row_)) return;
    if (def_.cells[row_][col_] == id) return;
    def_.cells[row_][col_] = static_cast<uint8_t>(id);
    dirty_ = stageDirty_ = true;
}

void LevelEditor::ToggleSpawn(bool removeOnly) {
    if (def_.versus && !removeOnly && VsSpawnTeam(def_.vsEagleCol, def_.vsEagleRow, col_, row_) < 0) return;   // a igual distancia de las dos aguilas: no es de ningun equipo
    for (int i = 0; i < def_.nSpawns; ++i) {
        if (def_.spawnCol[i] == col_ && def_.spawnRow[i] == row_) {
            for (int j = i; j + 1 < def_.nSpawns; ++j) {
                def_.spawnCol[j] = def_.spawnCol[j + 1];
                def_.spawnRow[j] = def_.spawnRow[j + 1];
            }
            --def_.nSpawns;
            dirty_ = stageDirty_ = true;
            return;
        }
    }
    if (removeOnly || def_.nSpawns >= kMaxSpawns || IsProtected(col_, row_)) return;
    if (def_.versus) {                                                     // 2 por equipo: el de la aguila que tenga mas cerca
        const int me = VsSpawnTeam(def_.vsEagleCol, def_.vsEagleRow, col_, row_);
        int mine = 0;
        for (int i = 0; i < def_.nSpawns; ++i) mine += VsSpawnTeam(def_.vsEagleCol, def_.vsEagleRow, def_.spawnCol[i], def_.spawnRow[i]) == me ? 1 : 0;
        if (mine >= 2) return;
    }
    def_.spawnCol[def_.nSpawns] = static_cast<uint8_t>(col_);
    def_.spawnRow[def_.nSpawns] = static_cast<uint8_t>(row_);
    ++def_.nSpawns;
    if (!LevelCellPassable(def_.cells[row_][col_]) || def_.cells[row_][col_] != 13) def_.cells[row_][col_] = 13;   // el lugar queda libre
    dirty_ = stageDirty_ = true;
}

void LevelEditor::PlaceEagle() {
    if (def_.versus) {                                                     // mueve el aguila del equipo de ese lado
        if (col_ == 13) return;
        const int t = col_ < 13 ? 0 : 1;                                   // la aguila del equipo de ese lado (izquierda = equipo 1)
        def_.vsEagleCol[t] = t == 0 ? std::max(1, std::min(11, col_)) : std::max(15, std::min(25, col_));
        def_.vsEagleRow[t] = std::max(1, std::min(13, row_));
        ClearVersusZones(def_);
        dirty_ = stageDirty_ = true;
        return;
    }
    if (!def_.boss) return;
    const int reach = EagleReachBlocks(def_.rank);
    const int c = std::max(reach, std::min(kLevelCols - 1 - reach, col_));
    const int r = std::max(0, std::min(kGridRows - 3 - reach, row_));
    def_.eagleCol = c;
    def_.eagleRow = r;
    ClearEagleZone(def_);
    dirty_ = stageDirty_ = true;
}

void LevelEditor::ApplyTool(bool erase) {
    switch (tool_) {
        case Tool::Block: PlaceBlock(erase ? 13 : kPalette[static_cast<size_t>(palette_)]); break;
        case Tool::Spawn: ToggleSpawn(erase); break;
        case Tool::Eagle: if (!erase) PlaceEagle(); break;
    }
}

void LevelEditor::SetKind(int kind) {
    if (kind == 2) {
        if (def_.versus) return;
        const LevelDef keep = def_;
        def_ = BlankVersusDef();
        std::memcpy(def_.cells, keep.cells, sizeof(def_.cells));         // el dibujo se conserva, con las zonas de las aguilas despejadas
        ClearVersusZones(def_);
    } else {
        if (def_.versus) {
            const LevelDef keep = def_;
            def_ = BlankLevelDef(false);
            std::memcpy(def_.cells, keep.cells, sizeof(def_.cells));
        }
        SetBoss(kind == 1);
    }
    if (tool_ == Tool::Eagle && !def_.boss && !def_.versus) tool_ = Tool::Block;
    dirty_ = stageDirty_ = true;
}

void LevelEditor::SetBoss(bool on) {
    if (def_.boss == on) return;
    def_.boss = on;
    if (on) {
        def_.eagleCol = 13;
        def_.eagleRow = 1;
        ClearEagleZone(def_);
    } else if (tool_ == Tool::Eagle) {
        tool_ = Tool::Block;
    }
    dirty_ = stageDirty_ = true;
}

void LevelEditor::SetRank(int rank) {
    rank = std::max(1, std::min(4, rank));
    if (rank == def_.rank) return;
    def_.rank = rank;
    if (def_.boss) {
        const int reach = EagleReachBlocks(rank);
        def_.eagleCol = std::max(reach, std::min(kLevelCols - 1 - reach, def_.eagleCol));
        def_.eagleRow = std::max(0, std::min(kGridRows - 3 - reach, def_.eagleRow));
        ClearEagleZone(def_);
    }
    dirty_ = stageDirty_ = true;
}

void LevelEditor::CycleTool() {
    int t = static_cast<int>(tool_);
    for (int i = 0; i < 3; ++i) {
        t = (t + 1) % 3;
        if (t == static_cast<int>(Tool::Eagle) && !def_.boss && !def_.versus) continue;
        break;
    }
    tool_ = static_cast<Tool>(t);
}

void LevelEditor::MoveCursor(const EditorKeys& k) {
    if (Rep(k.left, timers_[0])) col_ = std::max(0, col_ - 1);
    if (Rep(k.right, timers_[1])) col_ = std::min(kLevelCols - 1, col_ + 1);
    if (Rep(k.up, timers_[2])) row_ = std::max(0, row_ - 1);
    if (Rep(k.down, timers_[3])) row_ = std::min(kGridRows - 1, row_ + 1);
}

bool LevelEditor::MouseOverBar(int my) const {
    return barTop_ ? my < kEditorBarH : my >= kScreenH - kEditorBarH;
}

void LevelEditor::OpenMenu() {
    page_ = Page::Menu;
    menuSel_ = 0;
}

void LevelEditor::OpenSlots(SlotMode mode) {
    page_ = Page::Slots;
    slotMode_ = mode;
    slotSel_ = slot_ > 0 ? slot_ : 1;
    if (mode == SlotMode::Save && slot_ == 0) {                       // al guardar un nivel nuevo, el primer casillero libre
        for (int s = 1; s <= SlotCount(); ++s) {
            if (SlotState(s) == 0) { slotSel_ = s; break; }
        }
    }
}

void LevelEditor::AskConfirm(Ask what, const std::string& text, int slot) {
    page_ = Page::Confirm;
    ask_ = what;
    confirmText_ = text;
    askSlot_ = slot;
    confirmSel_ = 1;
}

void LevelEditor::Message(const std::string& text) {
    message_.clear();
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            message_.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    page_ = Page::Message;
}

std::vector<std::string> LevelEditor::MenuItems() const {
    std::vector<std::string> out;
    for (MenuId m : MenuIds(def_.boss, def_.versus)) {
        switch (m) {
            case kMiContinue: out.push_back("CONTINUAR"); break;
            case kMiType: out.push_back(std::string("TIPO: ") + (def_.versus ? "BATALLA VS" : (def_.boss ? "ESPECIAL" : "NORMAL"))); break;
            case kMiDifficulty: out.push_back((def_.versus ? "DIFICULTAD IA: " : "DIFICULTAD: ") + std::to_string(def_.tier + 1) + "/8"); break;
            case kMiBomb: out.push_back("BOMBARDEO CADA: " + std::to_string(5 - def_.rank) + (def_.rank == 4 ? " MINUTO" : " MINUTOS")); break;
            case kMiLayers: out.push_back("CAPAS DE HIERRO: " + std::to_string(def_.vsLayers) + "/2"); break;
            case kMiRank: out.push_back("FUERZA DEL ÁGUILA: " + std::to_string(def_.rank) + "/4"); break;
            case kMiSave: out.push_back(slot_ > 0 ? "GUARDAR (" + SlotLabel(slot_) + ")" : "GUARDAR"); break;
            case kMiSaveAs: out.push_back("GUARDAR EN OTRO"); break;
            case kMiLoad: out.push_back("ABRIR NIVEL"); break;
            case kMiNew: out.push_back("NIVEL NUEVO"); break;
            case kMiExit: out.push_back("SALIR"); break;
        }
    }
    return out;
}

std::string LevelEditor::SlotLabel(int slot) const {
    return (target_ == Target::Own ? "CASILLERO " : "NIVEL ") + std::to_string(slot);
}

void LevelEditor::StepMenu(const EditorKeys& k) {
    const std::vector<MenuId> ids = MenuIds(def_.boss, def_.versus);
    const int n = static_cast<int>(ids.size());
    if (menuSel_ >= n) menuSel_ = n - 1;
    if (Rep(k.up, timers_[2])) menuSel_ = (menuSel_ + n - 1) % n;
    if (Rep(k.down, timers_[3])) menuSel_ = (menuSel_ + 1) % n;
    const bool left = Rep(k.left, timers_[0]), right = Rep(k.right, timers_[1]);
    const bool accept = k.accept || k.place;
    const MenuId id = ids[static_cast<size_t>(menuSel_)];
    if (k.back || k.menu) { page_ = Page::Edit; return; }
    switch (id) {
        case kMiType:
            if (left || right || accept) {
                const int cur = def_.versus ? 2 : (def_.boss ? 1 : 0);
                const int count = target_ == Target::Own ? 3 : 2;          // la batalla vs solo se arma en el editor del juego
                SetKind((cur + (left ? count - 1 : 1)) % count);
            }
            break;
        case kMiBomb:
            if (left || right || accept) {
                def_.rank = (def_.rank - 1 + 4 + (left ? -1 : 1)) % 4 + 1;
                dirty_ = true;
            }
            break;
        case kMiLayers:
            if (left || right || accept) {
                def_.vsLayers = def_.vsLayers == 1 ? 2 : 1;
                dirty_ = stageDirty_ = true;
            }
            break;
        case kMiDifficulty:
            if (left || right || accept) {
                def_.tier = (def_.tier + (left ? 7 : 1)) % 8;
                dirty_ = stageDirty_ = true;
            }
            break;
        case kMiRank:
            if (left) SetRank(def_.rank - 1);
            if (right || accept) SetRank(def_.rank % 4 + 1);
            break;
        case kMiContinue:
            if (accept) page_ = Page::Edit;
            break;
        case kMiSave:
            if (!accept) break;
            if (slot_ > 0) {
                if (SaveToSlot(slot_)) {
                    const auto warn = CheckLevelDef(def_);
                    std::string msg = SlotLabel(slot_) + " GUARDADO";
                    for (const std::string& w : warn) msg += "\n" + w;
                    Message(msg);
                } else {
                    Message("NO SE PUDO GUARDAR");
                }
            } else {
                OpenSlots(SlotMode::Save);
            }
            break;
        case kMiSaveAs:
            if (accept) OpenSlots(SlotMode::Save);
            break;
        case kMiLoad:
            if (accept) OpenSlots(SlotMode::Load);
            break;
        case kMiNew:
            if (!accept) break;
            if (dirty_) AskConfirm(Ask::NewLevel, "HAY CAMBIOS SIN GUARDAR. ¿EMPEZAR OTRO?", 0);
            else NewLevel(false);
            if (page_ == Page::Menu) page_ = Page::Edit;
            break;
        case kMiExit:
            if (!accept) break;
            if (dirty_) AskConfirm(Ask::Exit, "HAY CAMBIOS SIN GUARDAR. ¿SALIR?", 0);
            else exit_ = true;
            break;
    }
}

void LevelEditor::StepSlots(const EditorKeys& k) {
    const int cols = target_ == Target::Main ? 10 : 8;
    const int count = SlotCount();
    if (Rep(k.left, timers_[0])) slotSel_ = std::max(1, slotSel_ - 1);
    if (Rep(k.right, timers_[1])) slotSel_ = std::min(count, slotSel_ + 1);
    if (Rep(k.up, timers_[2])) slotSel_ = slotSel_ - cols >= 1 ? slotSel_ - cols : slotSel_;
    if (Rep(k.down, timers_[3])) slotSel_ = slotSel_ + cols <= count ? slotSel_ + cols : slotSel_;
    if (k.back || k.menu) { page_ = Page::Menu; return; }
    if (k.erase) {                                                    // borrar el archivo de ese casillero
        const bool hasFile = target_ == Target::Own ? OwnSlotHasFile(slotSel_) : MainLevelHasFile(slotSel_);
        if (hasFile) {
            const bool restores = target_ == Target::Main && slotSel_ <= kLevelCount;
            AskConfirm(Ask::Delete, restores ? "¿VOLVER AL NIVEL DE FÁBRICA?" : "¿BORRAR ESTE NIVEL?", slotSel_);
        }
        return;
    }
    if (!(k.accept || k.place)) return;
    if (slotMode_ == SlotMode::Save) {
        if (SlotState(slotSel_) == 1 && slotSel_ != slot_) AskConfirm(Ask::Overwrite, "¿REEMPLAZAR EL NIVEL GUARDADO?", slotSel_);
        else if (SlotState(slotSel_) == 2 && target_ == Target::Main) AskConfirm(Ask::Overwrite, "¿REEMPLAZAR EL NIVEL DE FÁBRICA?", slotSel_);
        else {
            if (SaveToSlot(slotSel_)) {
                std::string msg = SlotLabel(slotSel_) + " GUARDADO";
                for (const std::string& w : CheckLevelDef(def_)) msg += "\n" + w;
                if (target_ == Target::Main && slotSel_ > MainLevelCount()) {
                    msg += "\nFALTAN NIVELES ANTES DE ESTE: NO SE JUEGA TODAVÍA";
                }
                Message(msg);
            } else {
                Message("NO SE PUDO GUARDAR");
            }
        }
    } else {
        if (SlotState(slotSel_) == 0) { Message("ESE CASILLERO ESTÁ VACÍO"); return; }
        if (dirty_) AskConfirm(Ask::LoadDiscard, "HAY CAMBIOS SIN GUARDAR. ¿ABRIR IGUAL?", slotSel_);
        else if (LoadFromSlot(slotSel_)) page_ = Page::Edit;
        else Message("NO SE PUDO ABRIR");
    }
}

void LevelEditor::StepConfirm(const EditorKeys& k) {
    if (Rep(k.left, timers_[0]) || Rep(k.right, timers_[1])) confirmSel_ = 1 - confirmSel_;
    if (k.back || k.menu) { page_ = ask_ == Ask::Overwrite || ask_ == Ask::Delete ? Page::Slots : Page::Menu; return; }
    if (!(k.accept || k.place)) return;
    if (confirmSel_ != 0) {                                           // "no"
        page_ = ask_ == Ask::Overwrite || ask_ == Ask::Delete || ask_ == Ask::LoadDiscard ? Page::Slots : Page::Menu;
        return;
    }
    switch (ask_) {
        case Ask::Overwrite:
            if (SaveToSlot(askSlot_)) {
                std::string msg = SlotLabel(askSlot_) + " GUARDADO";
                for (const std::string& w : CheckLevelDef(def_)) msg += "\n" + w;
                Message(msg);
            } else {
                Message("NO SE PUDO GUARDAR");
            }
            break;
        case Ask::Delete:
            DeleteLevelFile(FileOf(askSlot_));
            if (askSlot_ == slot_) slot_ = target_ == Target::Main && askSlot_ <= kLevelCount ? slot_ : 0;
            page_ = Page::Slots;
            break;
        case Ask::NewLevel: NewLevel(false); page_ = Page::Edit; break;
        case Ask::Exit: exit_ = true; page_ = Page::Edit; break;
        case Ask::LoadDiscard:
            if (LoadFromSlot(askSlot_)) page_ = Page::Edit;
            else Message("NO SE PUDO ABRIR");
            break;
    }
}

void LevelEditor::Step(const EditorKeys& k) {
    switch (page_) {
        case Page::Message:
            if (k.accept || k.back || k.place || k.menu || k.erase) page_ = Page::Edit;
            prevMenuKey_ = k.menu;
            return;
        case Page::Menu: StepMenu(k); prevMenuKey_ = k.menu; return;
        case Page::Slots: StepSlots(k); prevMenuKey_ = k.menu; return;
        case Page::Confirm: StepConfirm(k); prevMenuKey_ = k.menu; return;
        case Page::Edit: break;
    }
    if (k.menu) { OpenMenu(); return; }
    // la barra de herramientas se pone del lado opuesto al cursor
    barTop_ = row_ >= 8;
    const int oldCol = col_, oldRow = row_;
    MoveCursor(k);
    bool onBar = false;
    if (k.mx >= 0 && k.my >= 0) {
        onBar = MouseOverBar(k.my);
        if (!onBar) {
            col_ = std::max(0, std::min(kLevelCols - 1, k.mx / 16));
            row_ = std::max(0, std::min(kGridRows - 1, k.my / 16));
        }
    }
    barTop_ = row_ >= 8;
    if (k.wheel != 0) palette_ = ((palette_ - k.wheel) % static_cast<int>(kPalette.size()) + static_cast<int>(kPalette.size())) % static_cast<int>(kPalette.size());
    if (k.prevItem) palette_ = (palette_ + static_cast<int>(kPalette.size()) - 1) % static_cast<int>(kPalette.size());
    if (k.nextItem) palette_ = (palette_ + 1) % static_cast<int>(kPalette.size());
    if (k.tool) CycleTool();
    if (onBar) {
        // clic en la barra: herramienta o bloque de la paleta
        if (k.place) {
            const int rel = barTop_ ? k.my : k.my - (kScreenH - kEditorBarH);
            if (rel < 11) {
                if (k.mx >= kEditorToolX[0] && k.mx < kEditorToolX[0] + 52) tool_ = Tool::Block;
                else if (k.mx >= kEditorToolX[1] && k.mx < kEditorToolX[1] + 76) tool_ = Tool::Spawn;
                else if (def_.boss && k.mx >= kEditorToolX[2] && k.mx < kEditorToolX[2] + 52) tool_ = Tool::Eagle;
            } else if (tool_ == Tool::Block) {
                const int i = (k.mx - kEditorPalX) / kEditorPalStep;
                if (i >= 0 && i < static_cast<int>(kPalette.size())) palette_ = i;
            }
        }
        prevPlace_ = false;
        return;
    }
    const bool moved = col_ != oldCol || row_ != oldRow || (k.mx >= 0 && (col_ != lastPaintCol_ || row_ != lastPaintRow_));
    if (k.place) {
        ApplyTool(false);
        lastPaintCol_ = col_;
        lastPaintRow_ = row_;
    } else if (k.erase) {
        ApplyTool(true);
        lastPaintCol_ = col_;
        lastPaintRow_ = row_;
    } else if ((k.placeHeld || k.eraseHeld) && tool_ == Tool::Block && moved && (col_ != lastPaintCol_ || row_ != lastPaintRow_)) {
        ApplyTool(!k.placeHeld);                                       // pintar arrastrando
        lastPaintCol_ = col_;
        lastPaintRow_ = row_;
    }
    if (!k.placeHeld && !k.eraseHeld) lastPaintCol_ = lastPaintRow_ = -1;
    (void)oldCol;
}

void LevelEditor::BuildView(EditorView& v) {
    v = EditorView{};
    v.cursorCol = col_;
    v.cursorRow = row_;
    v.nSpawns = def_.nSpawns;
    for (int i = 0; i < def_.nSpawns; ++i) {
        v.spawnCol[i] = def_.spawnCol[i];
        v.spawnRow[i] = def_.spawnRow[i];
    }
    v.boss = def_.boss;
    v.versus = def_.versus;
    for (int t = 0; t < 2; ++t) { v.vsEagleCol[t] = def_.vsEagleCol[t]; v.vsEagleRow[t] = def_.vsEagleRow[t]; }
    v.eagleCol = def_.eagleCol;
    v.eagleRow = def_.eagleRow;
    v.eagleReach = EagleReachBlocks(def_.rank);
    v.tool = static_cast<int>(tool_);
    v.palette = palette_;
    v.paletteIds = kPalette;
    v.barTop = barTop_;
    v.help = help_;
    v.showCursor = page_ == Page::Edit;
    std::string st = slot_ > 0 ? (target_ == Target::Own ? "CASILLERO " : "NIVEL ") + std::to_string(slot_) : std::string("SIN GUARDAR");
    st += std::string(def_.versus ? "  VS" : (def_.boss ? "  ESPECIAL" : "  NORMAL")) + "  DIF " + std::to_string(def_.tier + 1);
    if (dirty_) st += " *";
    v.status = st;
    v.warnings = CheckLevelDef(def_);
    switch (page_) {
        case Page::Edit: break;
        case Page::Menu:
            v.page = 1;
            v.menuItems = MenuItems();
            v.menuSel = menuSel_;
            break;
        case Page::Slots: {
            v.page = 2;
            v.slotCols = target_ == Target::Main ? 10 : 8;
            v.slotCount = SlotCount();
            v.slotSel = slotSel_;
            v.slotTitle = std::string(slotMode_ == SlotMode::Save ? "GUARDAR EN " : "ABRIR ") + (target_ == Target::Main ? "NIVEL DEL MODO PRINCIPAL" : "CASILLERO");
            for (int s = 1; s <= v.slotCount; ++s) v.slotState.push_back(SlotState(s));
            // datos del nivel que esta bajo el cursor
            LevelDef d;
            const int st2 = SlotState(slotSel_);
            if (st2 != 0 && (target_ == Target::Own ? LoadOwnLevelDef(slotSel_, d) : LoadMainLevelDef(slotSel_, d))) {
                v.slotInfo = SlotLabel(slotSel_) + ": " + (st2 == 2 ? "DE FÁBRICA, " : "") + (d.versus ? "BATALLA VS" : (d.boss ? "ESPECIAL" : "NORMAL")) +
                             ", DIF " + std::to_string(d.tier + 1) + ", " + std::to_string(d.nSpawns) + " APARICIONES";
            } else {
                v.slotInfo = SlotLabel(slotSel_) + ": VACÍO";
            }
            v.slotLegend = target_ == Target::Main ? "AZUL FÁBRICA  VERDE PROPIO" : "VERDE: OCUPADO";
            break;
        }
        case Page::Confirm:
            v.page = 3;
            v.confirmText = confirmText_;
            v.confirmSel = confirmSel_;
            break;
        case Page::Message:
            v.page = 4;
            v.message = message_;
            break;
    }
}

}  // namespace app
