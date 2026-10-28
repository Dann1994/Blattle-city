#include "renderer.h"

#include "heli_data.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>

namespace app {

namespace {

// Paleta maestra de la NES (2C02, NTSC), formato 0x00RRGGBB.
constexpr uint32_t kNesRgb[64] = {
    0x7C7C7C, 0x0000FC, 0x0000BC, 0x4428BC, 0x940084, 0xA80020, 0xA81000, 0x881400,
    0x503000, 0x007800, 0x006800, 0x005800, 0x004058, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x0078F8, 0x0058F8, 0x6844FC, 0xD800CC, 0xE40058, 0xF83800, 0xE45C10,
    0xAC7C00, 0x00B800, 0x00A800, 0x00A844, 0x008888, 0x000000, 0x000000, 0x000000,
    0xF8F8F8, 0x3CBCFC, 0x6888FC, 0x9878F8, 0xF878F8, 0xF85898, 0xF87858, 0xFCA044,
    0xF8B800, 0xB8F818, 0x58D854, 0x58F898, 0x00E8D8, 0x787878, 0x000000, 0x000000,
    0xFCFCFC, 0xA4E4FC, 0xB8B8F8, 0xD8B8F8, 0xF8B8F8, 0xF8A4C0, 0xF0D0B0, 0xFCE0A8,
    0xF8D878, 0xD8F878, 0xB8F8B8, 0xB8F8D8, 0x00FCFC, 0xF8D8F8, 0x000000, 0x000000,
};

// Paletas de sprites extra del campo ancho (el atributo usa los bits 2-4, libres en la NES):
// 4 = jugador 3 (azul), 5 = jugador 4 (rojizo).
constexpr uint8_t kExtraSpritePalettes[6][4] = {
    {0x0F, 0x02, 0x21, 0x31},   // 4 azul
    {0x0F, 0x05, 0x16, 0x35},   // 5 rojo
    {0x0F, 0x03, 0x13, 0x33},   // 6 violeta
    {0x0F, 0x17, 0x27, 0x37},   // 7 naranja
    {0x0F, 0x08, 0x07, 0x17},   // 8 marron
    {0x0F, 0x15, 0x25, 0x35},   // 9 rosa
};

uint8_t SpritePaletteColor(int palette, int idx) {
    if (palette >= 4) {
        return kExtraSpritePalettes[(palette - 4) % 6][idx];
    }
    return nes::tbl_D555_sprites_palette[palette * 4 + idx];
}

// Grafico propio del item de aliados: el mismo marco que los demas items (copiado de la CHR) con un tanque y un
// simbolo + adentro. 16x16 en dos sprites de 8x16, tiles 0/1 (mitad izquierda) y 2/3 (derecha). 0 transparente,
// 1-3 colores de la paleta de sprites 2 (igual que los otros items).
constexpr const char* kAllyItemArt[16] = {
    "0333333333333320",
    "3000000000000031",
    "3011111111131131",
    "3011111111131131",
    "3011111113333331",
    "3011131111131131",
    "3002333201131131",
    "3002323201111131",
    "3002333201111131",
    "3002323201111131",
    "3002333201111131",
    "3002222201111131",
    "3011111111111131",
    "2333333333333321",
    "0111111111111110",
    "0000000000000000",
};

// Avion bombardero (el QING gris de la hoja de aviones), mirando hacia arriba; se dibuja al reves porque vuela hacia abajo.
// a = gris claro, b = gris medio, r = rojo.
constexpr int kPlaneW = 25;
constexpr int kPlaneH = 23;
constexpr const char* kPlaneArt[kPlaneH] = {
    "............b............",
    ".........aaabaaaa........",
    "..........bbabb..........",
    "..........aaaab..........",
    "...........bab...........",
    "..........aaaab..........",
    "..........aarab..........",
    "..aaaaaaaaarrrbbaaaaaaa..",
    "aaaaaaaaaaarrrbaaaaaaaaaa",
    "..aaaaaaaaaaaabbaaaaaaa..",
    ".....ababaaaaabbbaba.....",
    ".........bbaaabb.........",
    "..........brrrb..........",
    "..........barab..........",
    "...........aaa...........",
    "...........aaa...........",
    "...........aaa...........",
    "...........aaa...........",
    "............a............",
    "...........aaa...........",
    ".........aaaabaa.........",
    "........aaaaabbaa........",
    "........aaa.a.bab........",
};

int CustomPixel(int tile, int x, int y) {
    const int col = (tile & 2) ? 8 : 0;
    const int row = (tile & 1) ? 8 : 0;
    return kAllyItemArt[row + y][col + x] - '0';
}

constexpr int kNesW = 256;
constexpr int kNesH = 240;

// Posiciones de la pantalla NES que se ven en el escenario 16:9.
constexpr int kFieldPx = 224;                       // columnas 0..27 (borde + campo)
constexpr int kHudPx = 32;                          // columnas 28..31 (HUD original)
constexpr int kFieldX = 104;                       // panel izquierdo de 13 caracteres (104 px) para la ayuda
constexpr int kHudX = kFieldX + kFieldPx;

uint32_t NesColor(uint8_t index) {
    return kNesRgb[index & 0x3F];
}

// La arena es el mosaico de la nieve con una paleta amarillenta (los colores de la nieve son gris claro; estos, tonos de arena).
uint32_t SandColor(int idx) {
    static const uint8_t kIdx[4] = {0x0F, 0x28, 0x38, 0x37};
    return NesColor(kIdx[idx & 3]);
}
constexpr int kSnowTile = 0x21;

// Piedra de lava: cada mosaico de 8x8 lleva el dibujo de 4x4 del diseno ampliado al doble (morado oscuro, marcas negras y
// dos brasas que cambian de color). Alterna los cuadros 1 y 2 al mismo ritmo que el agua y la lava.
uint32_t RockPixel(int px, int py, int step) {
    static const uint8_t kPat[4][4] = {{1, 0, 0, 0}, {0, 2, 1, 0}, {0, 0, 2, 1}, {0, 0, 0, 1}};   // 0 morado, 1 negro, 2 brasa
    static const uint32_t kEmber[2] = {0xED0707, 0xED6B0B};
    const uint8_t v = kPat[(py >> 1) & 3][(px >> 1) & 3];
    return v == 1 ? 0x000000 : (v == 2 ? kEmber[step & 1] : 0x331940);
}
// Campo de fuerza y cristales de los niveles del aguila: dibujos de 8x8 (campo, dos cuadros que centellean) y de 16x16 (cristal).
const char* const kFieldA[8] = {"bbbbbbbb", "bCcbbcCb", "bcwCbbcb", "bbcCcbbb", "bbbcCcbb", "bcbbcwCb", "bCcbbcCb", "bbbbbbbb"};
const char* const kFieldB[8] = {"bbbbbbbb", "bbcCbbbb", "bcCwcbbb", "bbbcCcbb", "bbcbbcCb", "bCcbbcwb", "bbcCbbCb", "bbbbbbbb"};
const char* const kCrystalArt[16] = {"................", ".......ww.......", "......wppw......", ".....wpPPpw.....", "....wpPPPPpw....", "...wpPPwwPPpw...",
                                     "..wpPPwPPPPPpw..", "..wpPPwPPPPmpw..", "..wpPPPPPPmmpw..", "...wpPPPPmmpw...", "....wpPPmmpw....", ".....wpPmpw.....",
                                     "......wpmw......", ".......wm.......", "................", "................"};
// Grietas del cristal: 0 sano, 1 agrietado, 2 casi roto (se pintan en negro).
const int kCrack1[7][2] = {{8, 4}, {8, 5}, {7, 6}, {7, 7}, {8, 8}, {9, 9}, {9, 10}};
const int kCrack2[10][2] = {{5, 6}, {4, 7}, {4, 8}, {5, 9}, {11, 5}, {12, 6}, {12, 7}, {11, 8}, {10, 10}, {11, 11}};

uint32_t FieldColor(char ch, bool flash) {
    switch (ch) {
        case 'b': return flash ? 0x2878F0 : 0x002878;
        case 'c': return flash ? 0x78E6FC : 0x2878F0;
        case 'C': return flash ? 0xFCFCFC : 0x78E6FC;
        case 'w': return 0xFCFCFC;
        default: return 0;
    }
}

// Un pixel del campo de fuerza (kind 1) o de la placa de energia (5..7): `edge` marca el borde brillante de la placa.
uint32_t FieldPixel(int tx, int ty, int px, int py, int phase, bool flash, bool plateTop, bool plateBottom, bool plateLeft, bool plateRight) {
    if ((plateTop && py == 0) || (plateBottom && py == 7) || (plateLeft && px == 0) || (plateRight && px == 7)) return FieldColor('C', flash);
    const char* const* art = ((tx + ty + phase) & 1) ? kFieldB : kFieldA;
    return FieldColor(art[py][px], flash);
}

// Un pixel del cristal: lx, ly en 0..15 dentro de su cuadro; damage 0..2 segun la vida; hit = destello al recibir un disparo.
uint32_t CrystalPixel(int lx, int ly, int damage, bool hit) {
    uint32_t color = 0;
    const char ch = kCrystalArt[ly][lx];
    switch (ch) {
        case 'w': color = 0xFCFCFC; break;
        case 'p': color = 0xFC78C8; break;
        case 'P': color = 0xFCA0DC; break;
        case 'm': color = 0xC82896; break;
        default: color = 0x000000; break;
    }
    if (ch != '.') {
        if (damage >= 1) for (const auto& k : kCrack1) if (k[0] == lx && k[1] == ly) color = 0x000000;
        if (damage >= 2) for (const auto& k : kCrack2) if (k[0] == lx && k[1] == ly) color = 0x000000;
        if (hit) color = 0xFCFCFC;
    }
    return color;
}

constexpr int kWaterTile = 0x12;
// La lava es el mosaico del agua con una paleta roja que va cambiando (como el agua).
uint32_t LavaColor(int idx, int phase) {
    static const uint8_t kA[4] = {0x0F, 0x16, 0x27, 0x06};
    static const uint8_t kB[4] = {0x0F, 0x27, 0x06, 0x16};
    return NesColor((phase ? kB : kA)[idx & 3]);
}

} // namespace

bool Renderer::LoadChr(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    chr_.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return chr_.size() >= 8192;
}

bool Renderer::LoadChrData(const uint8_t* data, size_t size) {
    chr_.assign(data, data + size);
    return chr_.size() >= 8192;
}

int Renderer::ChrPixel(int table, int tile, int x, int y) const {
    if (table == 2) {
        return CustomPixel(tile, x, y);
    }
    const size_t base = static_cast<size_t>(table) * 0x1000 + static_cast<size_t>(tile & 0xFF) * 16;
    const uint8_t p0 = chr_[base + y];
    const uint8_t p1 = chr_[base + 8 + y];
    const int bit = 7 - x;
    return ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1);
}

namespace {
constexpr int kNesHeightPx = 240;

// ---- items nuevos (ids 8..12), minas, portal y helicopteros ----
// Los items nuevos usan el mismo marco y los mismos colores que los originales (paleta de sprites 2): 0 negro, 1 fondo verde azulado,
// 2 gris, 3 blanco. El dibujo ocupa el interior: 12 columnas por 11 filas (columnas 2..13, filas 2..12 del item de 16x16).
constexpr const char* kItemArt[6][11] = {
    {   // 8: invisible (fantasma)
        "111133331111", "111333333111", "113333333311", "113303303311", "113303303311", "113333333311",
        "113333333311", "113333333311", "113333333311", "113323323311", "113313313311",
    },
    {   // 9: helicoptero aliado
        "111111111111", "333333333333", "111112211111", "111222222111", "112222222211", "112220022211",
        "112222222211", "111122221111", "111112211111", "111112211111", "111133331111",
    },
    {   // 10: mina terrestre
        "111113311111", "131112211131", "113222222311", "112222222211", "332220022233", "332220022233",
        "112222222211", "113222222311", "131112211131", "111113311111", "111111111111",
    },
    {   // 11: portal (puerta)
        "111133331111", "113333333311", "113222222311", "113222222311", "113222222311", "113222222311",
        "113222223311", "113222222311", "113222222311", "113222222311", "113333333311",
    },
    {   // 12: velocidad (doble flecha)
        "311111311111", "331111331111", "333111333111", "333311333311", "333331333331", "333333333333",
        "333331333331", "333311333311", "333111333111", "331111331111", "311111311111",
    },
    {   // 13: canones (un canon con el caño hacia arriba)
        "111133331111", "111132231111", "111132231111", "111132231111", "111132231111", "113222222311",
        "132222222231", "132233332231", "132222222231", "113222222311", "111333333111",
    },
};

// El marco de los items originales (copiado de la CHR): 0 transparente, 1-3 colores de la paleta de sprites 2.
constexpr const char* kItemFrame[16] = {
    "0333333333333320", "3000000000000031", "3011111111111131", "3011111111111131",
    "3011111111111131", "3011111111111131", "3011111111111131", "3011111111111131",
    "3011111111111131", "3011111111111131", "3011111111111131", "3011111111111131",
    "3011111111111131", "2333333333333321", "0111111111111110", "0000000000000000",
};

// Mezcla un pixel ARGB sobre el buffer (alfa 0 = nada, 255 = tapa).
inline void BlendPx(std::vector<uint32_t>& rgb, int w, int x, int y, uint32_t argb) {
    const uint32_t a = argb >> 24;
    if (a == 0 || x < 0 || y < 0 || x >= w || y >= kNesHeightPx) return;
    uint32_t& d = rgb[static_cast<size_t>(y) * w + x];
    if (a >= 250) { d = argb & 0xFFFFFF; return; }
    auto ch = [&](int sh) { return ((((d >> sh) & 0xFF) * (255 - a) + ((argb >> sh) & 0xFF) * a) / 255) << sh; };
    d = ch(16) | ch(8) | ch(0);
}

void BlitHeli(std::vector<uint32_t>& rgb, int w, const app::HeliSprite& sp, int cx, int cy) {
    for (int y = 0; y < sp.h; ++y)
        for (int x = 0; x < sp.w; ++x) BlendPx(rgb, w, cx - sp.ax + x, cy - sp.ay + y, sp.px[y * sp.w + x]);
}

// El helicoptero girado hacia donde va (dir 0..7, de a 45 grados): la silueta de la sombra cae siempre hacia abajo a la derecha.
void BlitHeliRotated(std::vector<uint32_t>& rgb, int w, const app::HeliSprite& sp, int cx, int cy, int dir, uint32_t tint) {
    static const int kCos[8] = {1024, 724, 0, -724, -1024, -724, 0, 724};
    static const int kSin[8] = {0, 724, 1024, 724, 0, -724, -1024, -724};
    const int c = kCos[dir & 7], s = kSin[dir & 7];
    constexpr int R = 27;
    for (int pass = 0; pass < 2; ++pass) {                       // primero la sombra, despues el cuerpo
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx) {
                const int sx = sp.ax + (dx * c + dy * s + (dx * c + dy * s >= 0 ? 512 : -512)) / 1024;
                const int sy = sp.ay + (-dx * s + dy * c + (-dx * s + dy * c >= 0 ? 512 : -512)) / 1024;
                if (sx < 0 || sy < 0 || sx >= sp.w || sy >= sp.h) continue;
                const uint32_t px = sp.px[sy * sp.w + sx];
                if ((px >> 24) == 0) continue;
                if (pass == 0) BlendPx(rgb, w, cx + dx + 8, cy + dy + 11, 0x60000000);
                else BlendPx(rgb, w, cx + dx, cy + dy, (tint && (px & 0xFFFFFF) == 0x5C94FC) ? (0xFF000000 | tint) : px);      // el azul del casco toma el color del jugador
            }
    }
}

void DrawItemIcon(std::vector<uint32_t>& rgb, int w, int cx, int cy, int id) {
    const int x0 = cx - 8, y0 = cy - 8;
    const uint32_t col[4] = {0x000000, NesColor(SpritePaletteColor(2, 1)), NesColor(SpritePaletteColor(2, 2)), NesColor(SpritePaletteColor(2, 3))};
    const int k = id - 8;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            int idx = kItemFrame[y][x] - '0';
            const bool inner = x >= 2 && x <= 13 && y >= 2 && y <= 12;
            if (inner && k >= 0 && k <= 5) idx = kItemArt[k][y - 2][x - 2] - '0';
            const bool ring = y == 1 || x == 1 || x == 14 || inner;
            if (idx == 0 && !(ring && !(y == 15 || (y == 14 && (x == 0 || x == 15))))) continue;   // lo transparente del marco queda transparente; lo negro de adentro, negro
            BlendPx(rgb, w, x0 + x, y0 + y, 0xFF000000 | col[idx]);
        }
}

// Mina terrestre en el color de su dueno (los enemigos: gris).
void DrawMine(std::vector<uint32_t>& rgb, int w, int cx, int cy, uint32_t body, uint32_t light, bool blinkCenter) {
    for (int dy = -7; dy <= 7; ++dy)
        for (int dx = -7; dx <= 7; ++dx) {
            const int r2 = dx * dx + dy * dy;
            uint32_t c = 0;
            if ((std::abs(dx) <= 1 && std::abs(dy) <= 7) || (std::abs(dy) <= 1 && std::abs(dx) <= 7)) c = light;   // puas
            if (r2 <= 27) c = (r2 <= 27 && r2 > 20) ? 0x101010 : body;                                              // cuerpo y borde
            if (r2 <= 6) c = blinkCenter ? 0xFC2020 : light;                                                        // centro (luz)
            if (c) BlendPx(rgb, w, cx + dx, cy + dy, 0xFF000000 | c);
        }
}

// Puerta del portal: marco marron y adentro un remolino que cambia de color.
void DrawDoor(std::vector<uint32_t>& rgb, int w, int cx, int cy, uint32_t c1, uint32_t c2, int phase) {
    for (int dy = -8; dy <= 7; ++dy)
        for (int dx = -6; dx <= 5; ++dx) {
            const bool top = dy < -3;                                  // arco redondeado
            if (top && (dx + 0.5) * (dx + 0.5) / 36.0 + (dy + 3) * (dy + 3) / 25.0 > 1.0) continue;
            uint32_t c;
            if (dx == -6 || dx == 5 || dy == -8 || dy == 7) c = 0x8C5C00;                // marco
            else c = (((dx + dy + phase) >> 1) & 1) ? c1 : c2;                          // remolino
            BlendPx(rgb, w, cx + dx, cy + dy, 0xFF000000 | c);
        }
}

// Bloque de canon (16x16): base gris oscura con remaches, torreta redonda y un cano largo apuntando hacia afuera (a su costado). La luz de la torreta es verde cuando
// esta listo y roja mientras recarga; tres lucecitas abajo muestran la salud. Destella en blanco cuando lo golpean.
void DrawCannon(std::vector<uint32_t>& rgb, int w, const nes::Battle::Cannon& cn) {
    const int x0 = cn.col * 16, y0 = cn.row * 16;
    const bool flash = cn.flash > 0 && ((cn.flash >> 1) & 1) != 0;
    const uint32_t body = flash ? 0xFCFCFC : 0x585858, edge = 0x101010, hi = flash ? 0xFCFCFC : 0x9C9C9C, barrel = flash ? 0xFCFCFC : 0xC8C8C8;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            uint32_t col = body;
            if (x == 0 || y == 0 || x == 15 || y == 15) col = edge;
            else if (x == 1 || y == 1) col = hi;                                              // reborde de luz arriba y a la izquierda
            else if ((x == 2 || x == 13) && (y == 2 || y == 13)) col = 0xD8D8D8;              // remaches
            BlendPx(rgb, w, x0 + x, y0 + y, 0xFF000000 | col);
        }
    for (int dy = -6; dy <= 6; ++dy)                                                          // torreta
        for (int dx = -6; dx <= 6; ++dx) {
            const int r2 = dx * dx + dy * dy;
            if (r2 > 30) continue;
            BlendPx(rgb, w, x0 + 8 + dx, y0 + 8 + dy, 0xFF000000 | (r2 > 22 ? edge : 0x383838));
        }
    for (int i = 0; i < 9; ++i) {                                                             // cano hacia afuera, con la boca mas ancha
        const int ex = cn.side > 0 ? 15 - i : i;
        const bool mouth = i <= 2;
        for (int k = mouth ? -2 : -1; k <= (mouth ? 2 : 1); ++k) BlendPx(rgb, w, x0 + ex, y0 + 8 + k, 0xFF000000 | ((k == 0 || (!mouth && k == -1)) ? barrel : edge));
    }
    const bool ready = cn.cd == 0;
    for (int dy = -1; dy <= 0; ++dy)
        for (int dx = -1; dx <= 0; ++dx) BlendPx(rgb, w, x0 + 8 + dx, y0 + 8 + dy, 0xFF000000 | (ready ? 0x38FC38 : 0xFC3838));
    for (int h = 0; h < cn.hp; ++h)                                                           // salud
        for (int k = 0; k < 2; ++k) BlendPx(rgb, w, x0 + 4 + h * 3 + k, y0 + 13, 0xFF000000 | 0xFC7838);
}

// Lo que esta pegado al piso: minas y portal (debajo de los tanques).
void DrawExtraGround(const nes::Battle& b, std::vector<uint32_t>& rgb, int w) {
    for (int i = 0; i < nes::Battle::kMaxCannons; ++i) if (b.CannonState(i).on) DrawCannon(rgb, w, b.CannonState(i));
    for (int i = 0; i < nes::Battle::kMaxMines; ++i) {
        if (!b.MineVisible(i)) continue;
        const auto& m = b.MineState(i);
        uint32_t body = 0x7C7C7C, light = 0xBCBCBC;
        if (m.owner >= 0) {
            const int pal = b.PlayerPaletteIndex(m.owner);
            body = NesColor(SpritePaletteColor(pal, 2));
            light = NesColor(SpritePaletteColor(pal, 1));
        }
        DrawMine(rgb, w, m.col * 16 + 8, m.row * 16 + 8, body, light, ((b.R(nes::ram_frm_cnt_lo) >> 4) & 1) != 0);
    }
    const auto& pt = b.PortalState();
    if (pt.on) {
        const int phase = b.R(nes::ram_frm_cnt_lo) >> 2;
        DrawDoor(rgb, w, pt.col[0] * 16 + 8, pt.row[0] * 16 + 8, 0x5C94FC, 0xA8D8FC, phase);
        DrawDoor(rgb, w, pt.col[1] * 16 + 8, pt.row[1] * 16 + 8, 0xFC9838, 0xFCD8A8, phase);
    }
}

// Lo que va por encima: helicopteros (hoja de sprites propia), explosiones, iconos de los items nuevos y la marca de velocidad.
void DrawExtraAir(const nes::Battle& b, std::vector<uint32_t>& rgb, int w) {
    for (int i = 0; i < nes::Battle::kMaxSuperShots; ++i) {                               // super canonazo: un obus grande con estela
        const auto& sh = b.SuperShotState(i);
        if (!sh.on) continue;
        for (int k = 1; k <= 4; ++k)
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) BlendPx(rgb, w, sh.x - sh.dx * k * 3 + dx, sh.y - sh.dy * k * 3 + dy, (0x70 - k * 14) << 24 | 0xFC9838);
        for (int dy = -4; dy <= 3; ++dy)
            for (int dx = -4; dx <= 3; ++dx) BlendPx(rgb, w, sh.x + dx, sh.y + dy, 0xFF000000 | (std::abs(dx + 0.5) + std::abs(dy + 0.5) < 5 ? 0xFCFCFC : 0xFC9838));
    }
    for (int i = 0; i < nes::Battle::kMaxCannonShots; ++i) {                              // disparos de los canones
        const auto& sh = b.CannonShotState(i);
        if (!sh.on) continue;
        for (int dy = -2; dy <= 1; ++dy)
            for (int dx = -2; dx <= 1; ++dx) BlendPx(rgb, w, sh.x + dx, sh.y + dy, 0xFF000000 | ((dx >= -1 && dx <= 0 && dy >= -1 && dy <= 0) ? 0xFCFCA8 : 0xFC9838));
    }
    nes::Battle::ExtraIcon icons[8];
    const int n = b.ExtraIcons(icons, 8);
    for (int i = 0; i < n; ++i) DrawItemIcon(rgb, w, icons[i].x, icons[i].y, icons[i].id);
    for (int t = 0; t <= b.LastEnemy() + 4 && t < nes::kMaxTanks; ++t) {                  // velocidad: dos flechitas sobre el tanque
        if (!b.TankFast(t) || (b.TankFlags(t) & 0x80) == 0 || b.TankFlags(t) >= 0xE0) continue;
        if (((b.R(nes::ram_frm_cnt_lo) >> 2) & 1) == 0) continue;
        const int x = b.TankX(t) - 5, y = b.TankY(t) - 15;
        for (int k = 0; k < 2; ++k)
            for (int r = 0; r < 5; ++r) {
                const int len = r < 3 ? r + 1 : 5 - r;
                for (int c = 0; c < len; ++c) BlendPx(rgb, w, x + k * 5 + c, y + r, 0xFFFCD818);
            }
    }
    for (int i = 0; i < nes::Battle::kMaxBooms; ++i) {
        const auto& bm = b.BoomState(i);
        if (bm.t <= 0) continue;
        const app::HeliSprite& sp = app::kHeliBoom[std::min(3, (16 - bm.t) / 4)];
        BlitHeli(rgb, w, sp, bm.x, bm.y);
    }
    for (int i = 0; i < nes::Battle::kMaxHelis; ++i) {
        const auto& h = b.HeliState(i);
        if (!h.on) continue;
        const app::HeliSprite& sp = (h.owner >= 0 ? app::kHeliBlue : app::kHeliRed)[(h.frame / 2) % 6];
        const uint32_t tint = h.owner >= 0 ? NesColor(SpritePaletteColor(b.PlayerPaletteIndex(h.owner), 2)) : 0u;   // el helicoptero aliado lleva el color de quien lo trajo
        BlitHeliRotated(rgb, w, sp, h.x, h.y, h.dir, tint);
    }
}
} // namespace

void Renderer::RenderNesScreen(const nes::Battle& battle, std::vector<uint32_t>& rgb) const {
    const int kNesW = battle.MapCols() * 8;                 // 256 en modo clasico, 432 en el campo ancho
    rgb.assign(static_cast<size_t>(kNesW) * kNesH, 0);

    const int bgSet = battle.BgPaletteSet();
    auto bgColor = [&](int palette, int idx) {
        return NesColor(nes::tbl_D565_background_palette[bgSet * 16 + palette * 4 + idx]);
    };

    // ---- fondo (tabla de patrones 1) ----
    std::vector<uint8_t> bgIndex(static_cast<size_t>(kNesW) * kNesH, 0);
    for (int ty = 0; ty < 30; ++ty) {
        for (int tx = 0; tx < battle.MapCols(); ++tx) {
            const int tile = battle.Tile(tx, ty);
            const uint8_t attr = battle.AttrCell(tx, ty);
            const int quadrant = ((ty >> 1) & 1) * 2 + ((tx >> 1) & 1);
            const int palette = (attr >> (quadrant * 2)) & 3;
            int eagleTeam = -1;                              // batalla vs: cada aguila lleva el color de su equipo
            if (battle.IsVersus()) {
                for (int t = 0; t < 2; ++t) {
                    if (battle.VsEagleAlive(t) && tx >= 2 * battle.VsEagleCol(t) && tx < 2 * battle.VsEagleCol(t) + 2 &&
                        ty >= 2 * battle.VsEagleRow(t) && ty < 2 * battle.VsEagleRow(t) + 2) eagleTeam = t;
                }
            }
            const bool sand = tile == nes::Battle::kSandTile;
            const bool lava = tile == nes::Battle::kLavaTile;
            const bool rock = tile == 0x10 && battle.RockAt(tx, ty);
            const int field = tile == 0x10 ? battle.FieldAt(tx, ty) : 0;              // campo de fuerza (1), cristal (2..6) o placa (7..11)
            const int rockStep = (battle.R(nes::ram_frm_cnt_lo) >> 5) & 1;
            const int lavaPhase = (battle.R(nes::ram_frm_cnt_lo) >> 5) & 1;
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    const int idx = ChrPixel(1, sand ? kSnowTile : (lava ? kWaterTile : tile), px, py);
                    const size_t o = static_cast<size_t>(ty * 8 + py) * kNesW + (tx * 8 + px);
                    bgIndex[o] = static_cast<uint8_t>(idx);
                    rgb[o] = eagleTeam >= 0 ? NesColor(SpritePaletteColor(battle.TeamPaletteIndex(eagleTeam), idx))
                                            : (sand ? SandColor(idx) : (lava ? LavaColor(idx, lavaPhase) : (rock ? RockPixel(px, py, rockStep) : bgColor(palette, idx))));
                    if (field != 0) {
                        const int phase = (battle.R(nes::ram_frm_cnt_lo) >> 4) & 1;
                        const bool flash = battle.FieldHitT() > 0;
                        if (field == 1) {
                            rgb[o] = FieldPixel(tx, ty, px, py, phase, flash, false, false, false, false);
                        } else if (field <= 6) {
                            const int i = field - 2;
                            const int lx = (tx - battle.CrystalTx(i)) * 8 + px, ly = (ty - battle.CrystalTy(i)) * 8 + py;
                            const int hp = battle.CrystalHp(i), mx = battle.CrystalMaxHp();
                            const int damage = hp * 3 <= mx ? 2 : (hp * 3 <= mx * 2 ? 1 : 0);
                            const bool hit = battle.CrystalFx(i) > 0 && ((battle.R(nes::ram_frm_cnt_lo) >> 1) & 1);
                            rgb[o] = CrystalPixel(lx, ly, damage, hit);
                        } else {
                            const int i = field - 7;
                            const int rel = tx - (battle.CrystalTx(i) - 1);          // 0..3 a lo ancho de la placa
                            rgb[o] = FieldPixel(tx, ty, px, py, phase, flash, true, true, rel == 0, rel == 3);
                        }
                    }
                }
            }
        }
    }

    DrawExtraGround(battle, rgb, kNesW);                    // minas y portal: pegados al piso

    // ---- sprites 8x16: se pintan del ultimo al primero para que el indice menor quede arriba ----
    struct SpritePixel {
        uint32_t color = 0;
        bool opaque = false;
        bool behind = false;
    };
    std::vector<SpritePixel> layer(static_cast<size_t>(kNesW) * kNesH);
    // super canonazo: el tanque que lo tiene listo brilla (pulso blanco) y el que acaba de disparar parpadea en rojo (5 segundos sin disparar)
    static const int kPulse[4] = {0, 1, 2, 1};
    const int pulse = kPulse[(battle.R(nes::ram_frm_cnt_lo) >> 3) & 3];
    auto tankFxOf = [&](const nes::Sprite& s) {                       // 0 nada, 1 brillo, 2 rojo; el nivel va en el pulso
        for (int p = 0; p < 4 && p < battle.Players(); ++p) {
            const int f = battle.TankFlags(p);
            if (!(f & 0x80) || f >= 0xE0) continue;
            const int cx = s.x + 4, cy = s.y + 9;
            if (cx < battle.TankX(p) - 9 || cx > battle.TankX(p) + 9 || cy < battle.TankY(p) - 9 || cy > battle.TankY(p) + 9) continue;
            if (battle.SuperLock(p) > 0) return 2;
            if (battle.SuperReady(p)) return 1;
        }
        return 0;
    };
    for (int i = 63; i >= 0; --i) {
        const nes::Sprite s = battle.OamSprite(i);
        if (s.y >= 0xEF) {
            continue;
        }
        const int fx = pulse > 0 ? tankFxOf(s) : 0;
        const int palette = (s.attr & 7) | ((s.attr & 0x10) >> 1);   // el bit 4 del atributo suma el banco 8 (marron)
        const bool behind = (s.attr & 0x20) != 0;
        const bool flipH = (s.attr & 0x40) != 0;
        const bool flipV = (s.attr & 0x80) != 0;
        const int table = (s.attr & 0x08) ? 2 : (s.tile & 1);   // bit 3 del atributo: banco propio del renderer
        const int topTile = (s.attr & 0x08) ? (s.tile & 0xFE) : (s.tile & 0xFE);
        for (int py = 0; py < 16; ++py) {
            const int sy = flipV ? 15 - py : py;
            const int tile = topTile + (sy >> 3);
            const int ly = sy & 7;
            const int y = s.y + 1 + py;
            if (y < 0 || y >= kNesH) continue;
            for (int px = 0; px < 8; ++px) {
                const int sx = flipH ? 7 - px : px;
                const int idx = ChrPixel(table, tile, sx, ly);
                if (idx == 0) continue;
                const int x = s.x + px;
                if (x < 0 || x >= kNesW) continue;
                SpritePixel& p = layer[static_cast<size_t>(y) * kNesW + x];
                uint32_t col = NesColor(SpritePaletteColor(palette, idx));
                if (fx == 1) {                                                    // brillo: se suma blanco, mas fuerte en el pico del pulso
                    const uint32_t add = pulse == 1 ? 70 : 150;
                    const uint32_t r = std::min<uint32_t>(255, ((col >> 16) & 0xFF) + add), g = std::min<uint32_t>(255, ((col >> 8) & 0xFF) + add), b = std::min<uint32_t>(255, (col & 0xFF) + add);
                    col = (r << 16) | (g << 8) | b;
                } else if (fx == 2) {                                             // rojo: de a pasos (rojo suave, rojo pleno)
                    const uint32_t mr = pulse == 1 ? 255 : 230, mg = pulse == 1 ? 150 : 41, mb = pulse == 1 ? 150 : 55;
                    col = ((((col >> 16) & 0xFF) * mr / 255) << 16) | ((((col >> 8) & 0xFF) * mg / 255) << 8) | ((col & 0xFF) * mb / 255);
                }
                p.color = col;
                p.opaque = true;
                p.behind = behind;
            }
        }
    }
    for (size_t o = 0; o < layer.size(); ++o) {
        if (!layer[o].opaque) continue;
        if (layer[o].behind && bgIndex[o] != 0) continue;
        rgb[o] = layer[o].color;
    }
    DrawExtraAir(battle, rgb, kNesW);                       // helicopteros, explosiones e iconos de los items nuevos
}

namespace {

// Caracteres que la fuente de la ROM no trae (la X de la ROM es un bloque, no hay Z, tilde, signos...).
// 8x8 con el mismo estilo: trazo de 2 px, 6 de ancho y 7 de alto.
struct CustomGlyph {
    char c;
    const char* rows[8];
};

const CustomGlyph kCustomGlyphs[] = {
    {'X', {"..##..##", "...####.", "....##..", "....##..", "...####.", "..##..##", "..##..##", "........"}},
    {'Z', {"..######", "......##", ".....##.", "....##..", "...##...", "..##....", "..######", "........"}},
    {':', {"........", "...##...", "...##...", "........", "...##...", "...##...", "........", "........"}},
    {'!', {"...##...", "...##...", "...##...", "...##...", "...##...", "........", "...##...", "........"}},
    {'?', {"..####..", "..#..##.", "....##..", "...##...", "...##...", "........", "...##...", "........"}},
    {'/', {"......##", ".....##.", "....##..", "...##...", "..##....", ".##.....", "##......", "........"}},
    {',', {"........", "........", "........", "........", "........", "...##...", "...##...", "..##...."}},
    {'%', {"##....#.", "##...#..", "....#...", "...#....", "..#..##.", ".#...##.", "........", "........"}},
    {'+', {"........", "...##...", "...##...", ".######.", "...##...", "...##...", "........", "........"}},
    {'(', {"....##..", "...##...", "..##....", "..##....", "..##....", "...##...", "....##..", "........"}},
    {')', {"..##....", "...##...", "....##..", "....##..", "....##..", "...##...", "..##....", "........"}},
};

// codigos propios: 0x200 + letra = vocal con acento, 0x300 + 'U' = U con dieresis, 0x400 = signo de apertura de ?, 0x401 = de !
constexpr int kAcute = 0x200, kDiaeresis = 0x300, kInvQuestion = 0x400, kInvExclaim = 0x401;

const CustomGlyph* FindCustom(int code) {
    for (const CustomGlyph& g : kCustomGlyphs) {
        if (g.c == code) return &g;
    }
    return nullptr;
}

// Decodifica un caracter UTF-8 (Ñ, vocales con acento, signos de apertura) a un codigo de la fuente.
int DecodeChar(const std::string& text, size_t& i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch < 0x80) {
        ++i;
        return std::toupper(ch);
    }
    if ((ch & 0xE0) == 0xC0 && i + 1 < text.size()) {
        const int cp = ((ch & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
        i += 2;
        switch (cp) {
            case 0xD1: case 0xF1: return 0xD1;                              // N con tilde
            case 0xC1: case 0xE1: return kAcute + 'A';
            case 0xC9: case 0xE9: return kAcute + 'E';
            case 0xCD: case 0xED: return kAcute + 'I';
            case 0xD3: case 0xF3: return kAcute + 'O';
            case 0xDA: case 0xFA: return kAcute + 'U';
            case 0xDC: case 0xFC: return kDiaeresis + 'U';
            case 0xBF: return kInvQuestion;
            case 0xA1: return kInvExclaim;
            default: return ' ';
        }
    }
    ++i;
    return ' ';
}

} // namespace

// Pixel (0 o 1) de un caracter de la fuente.
int Renderer::FontPixel(int code, int x, int y) const {
    if (code >= kAcute && code < kInvQuestion) {                // vocal con acento / dieresis: la letra bajada un renglon y la marca arriba
        const int base = code & 0xFF;
        const bool dia = code >= kDiaeresis;
        if (y == 0) {
            if (dia) return (x == 3 || x == 5) ? 1 : 0;
            return (x == 4 || x == 5) ? 1 : 0;
        }
        return FontPixel(base, x, y - 1);
    }
    if (code == 0xD1) {                                          // N con tilde
        if (y == 0) return (x == 2 || x == 3 || x == 5 || x == 6) ? 1 : 0;
        return ChrPixel(1, 0x41 + ('N' - 'A'), x, y - 1) != 0 ? 1 : 0;
    }
    if (code == kInvQuestion) return FontPixel('?', x, 7 - y);   // signos de apertura: el de cierre dado vuelta
    if (code == kInvExclaim) return FontPixel('!', x, 7 - y);
    if (const CustomGlyph* g = FindCustom(code)) {
        return g->rows[y][x] == '#' ? 1 : 0;
    }
    int tile = -1;
    if (code >= 'A' && code <= 'Z') tile = 0x41 + (code - 'A');
    else if (code >= '0' && code <= '9') tile = 0x30 + (code - '0');
    else if (code == '-') tile = 0x6B;
    else if (code == '.') tile = 0x69;
    return tile >= 0 && ChrPixel(1, tile, x, y) != 0 ? 1 : 0;
}

bool Renderer::HasGlyph(int code) const {
    return (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') || code == '-' || code == '.' || code == 0xD1 ||
           (code >= kAcute && code <= kInvExclaim) || FindCustom(code) != nullptr;
}

std::string Renderer::Fit(const std::string& text, int maxPx) const {
    if (TextWidth(text) <= maxPx) return text;
    std::string t = text;
    while (!t.empty() && TextWidth(t + "...") > maxPx) {
        size_t cut = t.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;       // sin partir un caracter UTF-8
        t.erase(cut);
    }
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t + "...";
}

std::vector<std::string> Renderer::Wrap(const std::string& text, int maxPx) const {
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i <= text.size()) {
        size_t e = text.find(' ', i);
        if (e == std::string::npos) e = text.size();
        const std::string word = text.substr(i, e - i);
        const std::string next = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && TextWidth(next) > maxPx) { lines.push_back(cur); cur = word; }
        else cur = next;
        i = e + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    for (std::string& l : lines) l = Fit(l, maxPx);                    // (una palabra sola que no entra)
    return lines;
}

int Renderer::TextWidth(const std::string& text) const {
    int w = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            const size_t e = text.find('}', i);
            if (e != std::string::npos) {
                w += 14;
                i = e + 1;
                continue;
            }
        }
        DecodeChar(text, i);
        w += 8;
    }
    return w;
}

// Iconos de los botones del mando (inventados, tipo Xbox): {A} {B} {X} {Y} {DPAD} {START}. 12x12 px.
void Renderer::DrawIcon(std::vector<uint32_t>& canvas, int w, int x, int y, const std::string& token) const {
    auto put = [&](int px, int py, uint32_t color) {
        const int dx = x + px, dy = y + py;
        if (dx >= 0 && dx < w && dy >= 0 && dy < kScreenH) canvas[static_cast<size_t>(dy) * w + dx] = color;
    };
    auto disc = [&](uint32_t color) {
        for (int py = 0; py < 12; ++py)
            for (int px = 0; px < 12; ++px) {
                const int dx = 2 * px - 11, dy = 2 * py - 11;
                if (dx * dx + dy * dy <= 121) put(px, py, color);
            }
    };
    if (token == "A" || token == "B" || token == "X" || token == "Y") {
        const uint32_t color = token == "A" ? 0x3CB043 : (token == "B" ? 0xE03030 : (token == "X" ? 0x3C78E0 : 0xE0C030));
        disc(color);
        for (int py = 0; py < 8; ++py)
            for (int px = 0; px < 8; ++px)
                if (FontPixel(token[0], px, py)) put(px + 2, py + 2, 0x000000);
    } else if (token == "DPAD") {
        for (int i = 0; i < 12; ++i)
            for (int t = 4; t < 8; ++t) {
                put(i, t, 0xC0C0C0);
                put(t, i, 0xC0C0C0);
            }
        for (int t = 5; t < 7; ++t) {
            put(t, 1, 0x404040);
            put(t, 10, 0x404040);
            put(1, t, 0x404040);
            put(10, t, 0x404040);
        }
    } else if (token == "START") {
        disc(0x909090);
        for (int line = 0; line < 3; ++line)
            for (int px = 3; px < 9; ++px) put(px, 3 + line * 2, 0xFFFFFF);
    } else {
        disc(0x606060);
    }
}

// ---- auditoria de textos (solo con la variable BCNES_TEXT_AUDIT): avisa de los textos que se salen de la pantalla o se pisan entre si ----
namespace {
struct AuditRect { int x, y, w; std::string text; };
std::vector<AuditRect>& AuditRects() { static std::vector<AuditRect> v; return v; }
bool AuditOn() { static const bool on = std::getenv("BCNES_TEXT_AUDIT") != nullptr; return on; }
} // namespace

void TextAuditFlush(const char* tag) {
    if (!AuditOn()) return;
    auto& v = AuditRects();
    FILE* f = std::fopen(std::getenv("BCNES_TEXT_AUDIT"), "a");                // el valor de la variable es el archivo donde se escriben los avisos
    if (!f) { v.clear(); return; }
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].x < 0 || v[i].x + v[i].w > kScreenW || v[i].y < 0 || v[i].y + 8 > kScreenH)
            std::fprintf(f, "[texto] SE SALE (%s): \"%s\" x=%d y=%d ancho=%d\n", tag, v[i].text.c_str(), v[i].x, v[i].y, v[i].w);
        for (size_t j = i + 1; j < v.size(); ++j) {
            if (v[i].text == v[j].text && v[i].x == v[j].x && v[i].y == v[j].y) continue;
            const bool ox = v[i].x < v[j].x + v[j].w && v[j].x < v[i].x + v[i].w;
            const bool oy = v[i].y < v[j].y + 8 && v[j].y < v[i].y + 8;
            if (ox && oy) std::fprintf(f, "[texto] SE PISAN (%s): \"%s\" (%d,%d) con \"%s\" (%d,%d)\n", tag, v[i].text.c_str(), v[i].x, v[i].y, v[j].text.c_str(), v[j].x, v[j].y);
        }
    }
    std::fclose(f);
    v.clear();
}

void Renderer::DrawText(std::vector<uint32_t>& canvas, int w, int x, int y, const std::string& text, uint32_t color) const {
    if (AuditOn() && w == kScreenW && !text.empty()) AuditRects().push_back({x, y, TextWidth(text), text});
    int cx = x;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            const size_t e = text.find('}', i);
            if (e != std::string::npos) {
                DrawIcon(canvas, w, cx, y - 2, text.substr(i + 1, e - i - 1));
                cx += 14;
                i = e + 1;
                continue;
            }
        }
        const int code = DecodeChar(text, i);
        if (HasGlyph(code)) {
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    if (FontPixel(code, px, py) != 0) {
                        const int dx = cx + px;
                        const int dy = y + py;
                        if (dx >= 0 && dx < w && dy >= 0 && dy < kScreenH) {
                            canvas[static_cast<size_t>(dy) * w + dx] = color;
                        }
                    }
                }
            }
        }
        cx += 8;
    }
}

void Renderer::DrawHudTile(std::vector<uint32_t>& canvas, int w, int x, int y, int tile, const nes::Battle& battle, int palette) const {
    const int bgSet = battle.BgPaletteSet();
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            const int idx = ChrPixel(1, tile, px, py);
            canvas[static_cast<size_t>(y + py) * w + (x + px)] =
                NesColor(nes::tbl_D565_background_palette[bgSet * 16 + palette * 4 + idx]);
        }
    }
}

// HUD como el original (columna de la derecha: enemigos que faltan arriba, vidas de cada jugador, bandera y etapa abajo) pero
// semitransparente sobre el campo, para no gastar espacio de pantalla.
void Renderer::DrawWideHudOverlay(std::vector<uint32_t>& world, int w, const nes::Battle& battle, const HudInfo& hud) const {
    if (hud.hideHud) return;                                      // la informacion se oculta con Select
    std::vector<uint32_t> layer(world.size(), 0u);               // 0 = transparente
    static const char* const kLabels[4] = {"IP", "IIP", "IIIP", "IVP"};
    const int xr = w - 3;                                         // borde derecho visible (se recortan 3 px por lado)
    const int x0 = xr - 34;
    if (battle.IsRoyale()) {
        // battle royale: los 8 combatientes en dos filas arriba a la izquierda (vidas en la batalla a muerte, destrucciones por tiempo)
        for (int id = 0; id < 8; ++id) {
            const int px = 6 + (id & 3) * 52, py = 3 + (id >> 2) * 11;
            const int pal = battle.RoyalePaletteIndex(id);
            const uint32_t color = NesColor(SpritePaletteColor(pal, 2));
            const uint32_t colors[4] = {0, NesColor(SpritePaletteColor(pal, 1)), color, NesColor(SpritePaletteColor(pal, 3))};
            const bool out = !battle.RoyaleAlive(id);
            const std::string label = battle.RoyaleIsBot(id) ? std::string("IA") : std::to_string(id + 1);
            DrawText(layer, w, px, py, label, out ? 0x7C7C7C : color);
            DrawColoredTile(layer, w, px + 19, py, nes::tbl_D341_tile___player_icon[0], out ? std::array<uint32_t, 4>{0, 0x4C4C4C, 0x7C7C7C, 0x4C4C4C}.data() : colors);
            const int n = battle.RoyaleTimed() ? battle.RoyaleScore(id) : battle.RoyaleLives(id);      // por tiempo: bajas menos muertes
            DrawText(layer, w, px + 30, py, std::to_string(std::max(-99, std::min(99, n))), out ? 0x7C7C7C : color);
        }
        if (battle.RoyaleTimed()) {                              // el reloj de la partida, bajo el del bombardeo
            const int secs = (battle.RoyaleFramesLeft() + 59) / 60;
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
            DrawText(layer, w, xr - TextWidth(buf), 14, buf, 0xF8D878);
        }
    } else if (battle.IsVersus()) {
        // batalla vs: las vidas de cada equipo en su esquina de arriba (equipo 1 a la izquierda, equipo 2 a la derecha)
        int cnt[2] = {0, 0};
        for (int p = 0; p < nes::kMaxPlayers && p < hud.players; ++p) {
            if (!battle.PlayerPresent(p)) continue;
            const int t = battle.PlayerTeam(p), k = cnt[t]++;
            const int px = t == 0 ? 6 + k * 42 : xr - 38 - k * 42, py = t == 0 ? 3 : 229;     // cada equipo en la esquina de su aguila: el 1 arriba a la izquierda, el 2 abajo a la derecha
            const uint32_t color = NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 2));
            const uint32_t colors[4] = {0, NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 1)), color,
                                        NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 3))};
            const std::string label = battle.IsBot(p) ? std::string("IA") : std::to_string(p + 1);
            DrawText(layer, w, px, py, label, color);
            DrawColoredTile(layer, w, px + 9 + (label.size() > 1 ? 8 : 0), py, nes::tbl_D341_tile___player_icon[0], colors);
            if (battle.InfiniteLives()) DrawText(layer, w, px + 19 + (label.size() > 1 ? 8 : 0), py, "OO", color);        // vidas infinitas: el simbolo del infinito
            else DrawHudTileT(layer, w, px + 19 + (label.size() > 1 ? 8 : 0), py, 0x6E + (battle.Lives(p) % 10), battle, 0);
        }
    } else {
    // enemigos que faltan por aparecer: dos columnas de hasta 10, arriba a la derecha
    const int pending = battle.R(nes::ram_enemy_spawn_cnt);
    for (int i = 0; i < pending && i < 20; ++i) {
        DrawHudTileT(layer, w, xr - 18 + (i & 1) * 8, 16 + (i >> 1) * 8, nes::tbl_D362_tile___enemy_icon[0], battle, 0);
    }
    // vidas de cada jugador, en columna
    const int players = hud.players < 4 ? hud.players : 4;
    int row = 0;
    for (int p = 0; p < players; ++p) {
        if (!battle.PlayerPresent(p)) continue;                  // partida en red: lugares sin jugador no se muestran
        const int y = 100 + row++ * 24;
        const uint32_t color = NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 2));
        DrawText(layer, w, x0 + 34 - TextWidth(kLabels[p]), y, kLabels[p], color);
        const uint32_t colors[4] = {0, NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 1)), color,
                                    NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 3))};
        DrawColoredTile(layer, w, xr - 18, y + 9, nes::tbl_D341_tile___player_icon[0], colors);
        DrawHudTileT(layer, w, xr - 8, y + 9, 0x6E + (battle.Lives(p) % 10), battle, 0);
        if (battle.SuperReady(p) && ((battle.R(nes::ram_frm_cnt_lo) >> 4) & 1)) DrawText(layer, w, xr - 30, y + 9, "B", 0xF8D878);        // super canonazo listo
        const int steal = battle.StealTarget(p);                  // sin vidas: a quien le va a robar
        if (steal >= 0) {
            const int pi = battle.PlayerPaletteIndex(steal);
            const uint32_t sc[4] = {0, NesColor(SpritePaletteColor(pi, 1)), NesColor(SpritePaletteColor(pi, 2)), NesColor(SpritePaletteColor(pi, 3))};
            DrawColoredTile(layer, w, xr - 44, y + 9, nes::tbl_D341_tile___player_icon[0], sc);
            DrawText(layer, w, xr - 35, y + 9, std::to_string(steal + 1), sc[2]);
        }
    }
    // bandera y etapa abajo a la derecha
    const int fx = xr - 18, fy = 204;
    DrawHudTileT(layer, w, fx, fy, nes::tbl_D365_tiles___flag1[0], battle, 0);
    DrawHudTileT(layer, w, fx + 8, fy, nes::tbl_D365_tiles___flag1[1], battle, 0);
    DrawHudTileT(layer, w, fx, fy + 8, nes::tbl_D368_tiles___flag2[0], battle, 0);
    DrawHudTileT(layer, w, fx + 8, fy + 8, nes::tbl_D368_tiles___flag2[1], battle, 0);
    if (hud.stage >= 10) {
        DrawHudTileT(layer, w, fx, fy + 18, 0x6E + (hud.stage / 10) % 10, battle, 0);
        DrawHudTileT(layer, w, fx + 8, fy + 18, 0x6E + (hud.stage % 10), battle, 0);
    } else {
        DrawHudTileT(layer, w, fx + 8, fy + 18, 0x6E + (hud.stage % 10), battle, 0);
    }
    }
    // mezcla con el campo (65% HUD)
    for (size_t i = 0; i < layer.size(); ++i) {
        const uint32_t c = layer[i];
        if (c == 0) continue;
        const uint32_t b = world[i];
        const uint32_t r = (((c >> 16) & 0xFF) * 166 + ((b >> 16) & 0xFF) * 90) >> 8;
        const uint32_t g = (((c >> 8) & 0xFF) * 166 + ((b >> 8) & 0xFF) * 90) >> 8;
        const uint32_t bl = ((c & 0xFF) * 166 + (b & 0xFF) * 90) >> 8;
        world[i] = (r << 16) | (g << 8) | bl;
    }
}

// Temporizador del bombardeo: arriba en el centro de la pantalla (se oculta con el resto de la informacion). Cuando se acaba el tiempo aparece la palabra
// ALERTA en rojo en el medio de la pantalla, parpadeando de a poco; esa si se ve siempre.
void Renderer::DrawBombingTimer(std::vector<uint32_t>& world, int w, const nes::Battle& battle, const HudInfo& hud) const {
    if (!battle.BombingActive()) return;
    const auto& bs = battle.Bombing();
    const int frame = battle.R(nes::ram_frm_cnt_lo);
    auto blend = [&](const std::vector<uint32_t>& layer, int num, int den) {          // num/den = opacidad del texto
        for (size_t i = 0; i < layer.size(); ++i) {
            if (layer[i] == 0) continue;
            const uint32_t c = layer[i], b = world[i];
            const uint32_t r = (((c >> 16) & 0xFF) * num + ((b >> 16) & 0xFF) * (den - num)) / den;
            const uint32_t g = (((c >> 8) & 0xFF) * num + ((b >> 8) & 0xFF) * (den - num)) / den;
            const uint32_t bl = ((c & 0xFF) * num + (b & 0xFF) * (den - num)) / den;
            world[i] = (r << 16) | (g << 8) | bl;
        }
    };
    if (bs.phase == 0) {
        if (hud.hideHud) return;
        const int secs = (bs.countdown + 59) / 60;
        const std::string label = std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
        const bool blink = bs.countdown < 600;                                        // el ultimo tramo parpadea, de a poco
        if (blink && ((frame >> 5) & 1)) return;
        std::vector<uint32_t> timer(world.size(), 0u);
        std::vector<uint32_t> shadow(world.size(), 0u);
        DrawText(shadow, w, (w - TextWidth(label)) / 2 + 1, 4, label, 0x200000);       // sombra: se lee sobre cualquier fondo
        blend(shadow, 150, 256);
        DrawText(timer, w, (w - TextWidth(label)) / 2, 3, label, 0xFC2020);          // arriba, en el centro
        blend(timer, 200, 256);
        return;
    }
    if ((frame >> 5) & 1) return;                                                      // parpadea: medio segundo si, medio segundo no
    std::vector<uint32_t> alert(world.size(), 0u);
    const std::string word = "ALERTA";
    const int sc = 2, tw = TextWidth(word) * sc, th = 8 * sc;
    const int x0 = (w - tw) / 2, y0 = (kNesH - th) / 2;
    for (int pass = 0; pass < 2; ++pass) {                                             // primero el borde negro, despues el rojo
        const int ox = pass == 0 ? 1 : 0, oy = pass == 0 ? 1 : 0;
        std::vector<uint32_t> small(world.size(), 0u);
        DrawText(small, w, 0, 0, word, 0xFC2020);                                      // (las letras a tamano normal, en la esquina)
        for (int py = 0; py < 8; ++py)
            for (int px = 0; px < TextWidth(word); ++px) {
                if (small[static_cast<size_t>(py) * w + px] == 0) continue;
                for (int sy = 0; sy < sc; ++sy)
                    for (int sx = 0; sx < sc; ++sx) {
                        const int dx = x0 + px * sc + sx + ox * 2, dy = y0 + py * sc + sy + oy * 2;
                        if (dx >= 0 && dx < w && dy >= 0 && dy < kNesH) alert[static_cast<size_t>(dy) * w + dx] = pass == 0 ? 0x300000 | 0x01000000u : 0xFC2020;
                    }
            }
    }
    for (size_t i = 0; i < alert.size(); ++i) if (alert[i] != 0) world[i] = alert[i] & 0xFFFFFF;
}

// Como DrawHudTile pero sin pintar el fondo (el color 0 queda transparente).
void Renderer::DrawHudTileT(std::vector<uint32_t>& canvas, int w, int x, int y, int tile, const nes::Battle& battle, int palette) const {
    const int bgSet = battle.BgPaletteSet();
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            const int idx = ChrPixel(1, tile, px, py);
            if (idx == 0) continue;
            const int dx = x + px, dy = y + py;
            if (dx < 0 || dx >= w || dy < 0 || dy >= kNesH) continue;
            canvas[static_cast<size_t>(dy) * w + dx] = NesColor(nes::tbl_D565_background_palette[bgSet * 16 + palette * 4 + idx]) | 0x01000000u;
        }
    }
}

void Renderer::DrawWideHud(std::vector<uint32_t>& world, int w, const nes::Battle& battle, const HudInfo& hud) const {
    // Franja superior de 16 px (las dos filas de mosaicos grises de arriba del campo).
    static const char* const kLabels[4] = {"IP", "IIP", "IIIP", "IVP"};
    const int players = hud.players < 4 ? hud.players : 4;
    for (int p = 0; p < players; ++p) {
        const int x = 14 + p * 46;
        const uint32_t color = NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 2));
        DrawText(world, w, x, 0, kLabels[p], color);
        // icono del tanque con los colores del jugador + vidas
        const uint32_t colors[4] = {0, NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 1)), color,
                                    NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 3))};
        DrawColoredTile(world, w, x, 8, nes::tbl_D341_tile___player_icon[0], colors);
        DrawHudTile(world, w, x + 10, 8, 0x6E + (battle.Lives(p) % 10), battle, 0);
        const int steal = battle.StealTarget(p);          // sin vidas: a quien le va a robar (izquierda/derecha para elegir, disparo para robar)
        if (steal >= 0) {
            // el tanque del elegido (en su color) y su numero: "a ese le voy a robar"
            const int pi = battle.PlayerPaletteIndex(steal);
            const uint32_t sc[4] = {0, NesColor(SpritePaletteColor(pi, 1)), NesColor(SpritePaletteColor(pi, 2)), NesColor(SpritePaletteColor(pi, 3))};
            DrawColoredTile(world, w, x + 22, 8, nes::tbl_D341_tile___player_icon[0], sc);
            DrawText(world, w, x + 31, 8, std::to_string(steal + 1), sc[2]);
        }
    }
    if (battle.BombingActive()) {                         // temporizador del bombardeo, en rojo
        const auto& bs = battle.Bombing();
        const int frame = battle.R(nes::ram_frm_cnt_lo);
        std::string label;
        if (bs.phase == 0) {
            const int secs = (bs.countdown + 59) / 60;
            label = "BOMBARDEO " + std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
        } else {
            label = "¡ALERTA!";
        }
        const bool blink = bs.phase != 0 || bs.countdown < 600;
        if (!blink || ((frame >> 3) & 1)) DrawText(world, w, 214, 4, label, 0xFC2020);
    }
    // enemigos que faltan por aparecer: dos filas de 10
    const int pending = battle.R(nes::ram_enemy_spawn_cnt);
    const int ex = 14 + 4 * 46 + 8;
    for (int i = 0; i < pending && i < 20; ++i) {
        DrawHudTile(world, w, ex + (i >> 1) * 8, (i & 1) * 8, nes::tbl_D362_tile___enemy_icon[0], battle, 0);
    }
    // bandera y etapa
    const int fx = w - 72;
    DrawHudTile(world, w, fx, 0, nes::tbl_D365_tiles___flag1[0], battle, 0);
    DrawHudTile(world, w, fx + 8, 0, nes::tbl_D365_tiles___flag1[1], battle, 0);
    DrawHudTile(world, w, fx, 8, nes::tbl_D368_tiles___flag2[0], battle, 0);
    DrawHudTile(world, w, fx + 8, 8, nes::tbl_D368_tiles___flag2[1], battle, 0);
    if (hud.stage >= 10) {
        DrawHudTile(world, w, fx + 24, 4, 0x6E + (hud.stage / 10) % 10, battle, 0);
        DrawHudTile(world, w, fx + 32, 4, 0x6E + (hud.stage % 10), battle, 0);
    } else {
        DrawHudTile(world, w, fx + 24, 4, 0x6E + (hud.stage % 10), battle, 0);
    }
}

// Un sprite de la ROM de 16x16 (dos sprites 8x16 lado a lado: tile y tile+2) escalado a size x size px.
void Renderer::DrawBlastPiece(std::vector<uint32_t>& canvas, int w, int x, int y, int tile, int palette, int size) const {
    for (int dy = 0; dy < size; ++dy) {
        for (int dx = 0; dx < size; ++dx) {
            const int sx = dx * 16 / size, sy = dy * 16 / size;
            const int t = tile + ((sx >= 8) ? 2 : 0);
            const int idx = ChrPixel(t & 1, (t & 0xFE) + (sy >= 8 ? 1 : 0), sx & 7, sy & 7);
            const int px = x + dx, py = y + dy;
            if (idx != 0 && px >= 0 && px < w && py >= 0 && py < kNesH) {
                canvas[static_cast<size_t>(py) * w + px] = NesColor(SpritePaletteColor(palette, idx));
            }
        }
    }
}

// Bombardeo: marcas rojas, explosiones agrandadas (la del aguila destruido a 3x3 cuadros) y aviones.
void Renderer::DrawBombing(std::vector<uint32_t>& world, int w, const nes::Battle& battle) const {
    if (!battle.BombingActive()) return;
    const auto& bs = battle.Bombing();
    const int frame = battle.R(nes::ram_frm_cnt_lo);
    for (const auto& t : bs.target) {                    // el cuadro donde va a caer parpadea en rojo
        if (t.fuse <= 0 || ((frame >> 2) & 1)) continue;
        for (int dy = -24; dy < 24; ++dy) {
            for (int dx = -24; dx < 24; ++dx) {
                const int px = t.cx + dx, py = t.cy + dy;
                if (px < 0 || px >= w || py < 0 || py >= kNesH) continue;
                if (std::abs(dx) < 8 && std::abs(dy) < 8) {          // el cuadro de la bomba, lleno; alrededor, solo el borde
                    uint32_t& c = world[static_cast<size_t>(py) * w + px];
                    c = ((c >> 1) & 0x7F7F7F) + 0x800000;
                } else if (dx == -24 || dx == 23 || dy == -24 || dy == 23) {
                    world[static_cast<size_t>(py) * w + px] = 0xE02020;
                }
            }
        }
    }
    for (const auto& bl : bs.blast) {                    // la explosion del aguila: 1-2-3, grande, 3-2-1 (4 cuadros cada paso)
        if (bl.age < 0) continue;
        static const int kSeq[10] = {1, 2, 3, 4, 5, 4, 3, 2, 1, 0};
        const int a = kSeq[bl.age / 4 < 10 ? bl.age / 4 : 9];
        if (a == 0) continue;
        if (a <= 3) {
            const int tiles[3] = {0xF1, 0xF5, 0xF9};
            DrawBlastPiece(world, w, bl.cx - 24, bl.cy - 24, tiles[a - 1], 3, 48);
        } else {
            const int off = a == 5 ? 0x10 : 0;
            DrawBlastPiece(world, w, bl.cx - 24, bl.cy - 24, 0xD1 + off, 3, 24);
            DrawBlastPiece(world, w, bl.cx, bl.cy - 24, 0xD5 + off, 3, 24);
            DrawBlastPiece(world, w, bl.cx - 24, bl.cy, 0xD9 + off, 3, 24);
            DrawBlastPiece(world, w, bl.cx, bl.cy, 0xDD + off, 3, 24);
        }
    }
    for (const auto& pl : bs.plane) {                    // los aviones, al doble de tamano; apuntan hacia donde vuelan
        if (!pl.active) continue;
        static const uint32_t kCol[3] = {0xC7C7C7, 0x747474, 0xD82800};
        const bool horizontal = pl.dir != 0;
        for (int ry = 0; ry < kPlaneH; ++ry) {
            for (int rx = 0; rx < kPlaneW; ++rx) {
                const char ch = kPlaneArt[ry][rx];             // el dibujo mira hacia arriba (ry = 0 es la nariz)
                if (ch == '.') continue;
                const uint32_t color = kCol[ch == 'a' ? 0 : (ch == 'b' ? 1 : 2)];
                int ox, oy;                                    // posicion del pixel respecto del centro del avion, al doble
                if (!horizontal) {                             // baja: la nariz hacia abajo
                    ox = rx * 2 - kPlaneW;
                    oy = (kPlaneH - 1 - ry) * 2 - kPlaneH;
                } else if (pl.dir == 2) {                      // va a la derecha: la nariz hacia la derecha
                    ox = (kPlaneH - 1 - ry) * 2 - kPlaneH;
                    oy = rx * 2 - kPlaneW;
                } else {                                       // va a la izquierda
                    ox = ry * 2 - kPlaneH;
                    oy = rx * 2 - kPlaneW;
                }
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const int px = pl.x + ox + dx, py = pl.y + oy + dy;
                        if (px >= 0 && px < w && py >= 0 && py < kNesH) world[static_cast<size_t>(py) * w + px] = color;
                    }
                }
            }
        }
    }
}

void Renderer::DrawColoredTile(std::vector<uint32_t>& canvas, int w, int x, int y, int tile, const uint32_t colors[4]) const {
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            const int idx = ChrPixel(1, tile, px, py);
            if (idx != 0) {
                canvas[static_cast<size_t>(y + py) * w + (x + px)] = colors[idx];
            }
        }
    }
}

void Renderer::RenderWide(const nes::Battle& battle, const HudInfo& hud, std::vector<uint32_t>& canvas) const {
    std::vector<uint32_t> world;
    RenderNesScreen(battle, world);
    const int w = battle.MapCols() * 8;
    DrawBombing(world, w, battle);
    if (nes::Battle::kOpenTopStrip) DrawWideHudOverlay(world, w, battle, hud);
    else DrawWideHud(world, w, battle, hud);
    DrawBombingTimer(world, w, battle, hud);
    // el mundo mide 432 px y la pantalla 426: se recorta un poco de borde a cada lado
    const int cropX = (w - kScreenW) / 2;
    const int shx = battle.ShakeX(), shy = battle.ShakeY();            // el escenario tiembla con cada explosion
    for (int y = 0; y < kScreenH; ++y) {
        for (int x = 0; x < kScreenW; ++x) {
            const int sx = x + cropX + shx, sy = y + shy;
            canvas[static_cast<size_t>(y) * kScreenW + x] =
                (sx >= 0 && sx < w && sy >= 0 && sy < kNesH) ? world[static_cast<size_t>(sy) * w + sx] : 0u;
        }
    }
    // niveles del aguila: cuantos cristales quedan para que caiga el campo de fuerza (abajo del reloj del bombardeo)
    if (!hud.hideHud && battle.IsBossLevel() && battle.FieldUp()) {
        const std::string label = "CRISTALES " + std::to_string(battle.CrystalsLeft());
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(label)) / 2 + 1, 15, label, 0x300020);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(label)) / 2, 14, label, 0xFC78C8);
    }
    // partida en red: el nombre de cada jugador real sobre su tanque (se oculta con la demas informacion)
    if (!hud.hideHud) {
        for (int p = 0; p < 8; ++p) {
            if (hud.playerNames[p].empty() || p >= battle.Players()) continue;
            const int f = battle.TankFlags(p);
            if (!(f & 0x80) || f >= 0xE0) continue;                             // solo con el tanque en el campo
            const std::string name = Fit(hud.playerNames[p], 100);
            const int tw = TextWidth(name);
            // el nombre va chico y casi transparente (sutil): se dibuja a tamano normal en un cuadro aparte, se reduce a 5/8 y se mezcla con el fondo
            std::vector<uint32_t> src(static_cast<size_t>(tw) * 8, 0u);
            DrawText(src, tw, 0, 0, name, 0xFFFFFF);
            const int dw = std::max(1, tw * 5 / 8), dh = 5;
            const int x0 = std::max(1, std::min(kScreenW - dw - 1, battle.TankX(p) - cropX - dw / 2));
            const int y0 = std::max(1, battle.TankY(p) - 8 - 6);                // justo encima del tanque (cada tanque mide 16 px)
            const uint32_t color = NesColor(SpritePaletteColor(battle.PlayerPaletteIndex(p), 2));
            for (int dy = 0; dy < dh; ++dy) {
                for (int dx = 0; dx < dw; ++dx) {
                    const int sx0 = dx * tw / dw, sx1 = std::max(sx0 + 1, (dx + 1) * tw / dw);
                    const int sy0 = dy * 8 / dh, sy1 = std::max(sy0 + 1, (dy + 1) * 8 / dh);
                    int on = 0, all = 0;
                    for (int yy = sy0; yy < sy1 && yy < 8; ++yy)
                        for (int xx = sx0; xx < sx1 && xx < tw; ++xx) { on += src[static_cast<size_t>(yy) * tw + xx] != 0; ++all; }
                    if (on == 0 || all == 0) continue;
                    const int alpha = 140 * on / all;                           // 0..140 de 255: transparente
                    uint32_t& dst = canvas[static_cast<size_t>(y0 + dy) * kScreenW + (x0 + dx)];
                    auto mix = [&](int sh) {
                        const int d = (dst >> sh) & 0xFF, c = (color >> sh) & 0xFF;
                        return static_cast<uint32_t>((d * (255 - alpha) + c * alpha) / 255) << sh;
                    };
                    dst = (dst & 0xFF000000u) | mix(16) | mix(8) | mix(0);
                }
            }
        }
    }
}

void Renderer::Render(const nes::Battle& battle, const HudInfo& hud, std::vector<uint8_t>& rgba) const {
    hits_.clear();
    std::vector<uint32_t> canvas(static_cast<size_t>(kScreenW) * kScreenH, 0x101018);
    if (battle.IsWide()) {
        RenderWide(battle, hud, canvas);
        FinishFrame(canvas, hud, rgba);
        return;
    }
    std::vector<uint32_t> nes;
    RenderNesScreen(battle, nes);

    // campo (columnas 0..27) y HUD original (columnas 28..31), pegados
    for (int y = 0; y < kNesH; ++y) {
        for (int x = 0; x < kFieldPx; ++x) {
            canvas[static_cast<size_t>(y) * kScreenW + (kFieldX + x)] = nes[static_cast<size_t>(y) * kNesW + x];
        }
        for (int x = 0; x < kHudPx; ++x) {
            canvas[static_cast<size_t>(y) * kScreenW + (kHudX + x)] = nes[static_cast<size_t>(y) * kNesW + kFieldPx + x];
        }
    }

    // ---- HUD original, dibujado desde el estado (la ROM lo escribe por nametable) ----
    // iconos de enemigos que faltan por aparecer (2 columnas de hasta 10)
    const int pending = battle.R(nes::ram_enemy_spawn_cnt);
    for (int i = 0; i < pending && i < 20; ++i) {
        // columnas 29 y 30, filas 3..12 de la nametable (sub_C894)
        DrawHudTile(canvas, kScreenW, kHudX + 8 + (i & 1) * 8, (3 + (i >> 1)) * 8,
                    nes::tbl_D362_tile___enemy_icon[0], battle, 0);
    }
    auto drawLives = [&](int player, int row) {
        // "IP" / "IIP" y debajo el icono de tanque y las vidas
        const std::string label = player == 0 ? "IP" : "IIP";
        DrawText(canvas, kScreenW, kHudX + 8, row * 8, label, 0xFCFCFC);
        DrawHudTile(canvas, kScreenW, kHudX + 8, (row + 1) * 8, nes::tbl_D341_tile___player_icon[0], battle, 0);
        const int lives = battle.R(nes::ram_lives + player);
        DrawHudTile(canvas, kScreenW, kHudX + 16, (row + 1) * 8, 0x6E + (lives % 10), battle, 0);
    };
    drawLives(0, 17);
    if (hud.players >= 2) {
        drawLives(1, 20);
    }
    // bandera de etapa y numero
    DrawHudTile(canvas, kScreenW, kHudX + 8, 23 * 8, nes::tbl_D365_tiles___flag1[0], battle, 0);
    DrawHudTile(canvas, kScreenW, kHudX + 16, 23 * 8, nes::tbl_D365_tiles___flag1[1], battle, 0);
    DrawHudTile(canvas, kScreenW, kHudX + 8, 24 * 8, nes::tbl_D368_tiles___flag2[0], battle, 0);
    DrawHudTile(canvas, kScreenW, kHudX + 16, 24 * 8, nes::tbl_D368_tiles___flag2[1], battle, 0);
    DrawHudTile(canvas, kScreenW, kHudX + 16, 25 * 8, 0x6E + (hud.stage % 10), battle, 0);

    FinishFrame(canvas, hud, rgba);
}

// Cartel de ayuda (tecla Esc), PAUSE y conversion a RGBA.
void Renderer::FinishFrame(std::vector<uint32_t>& canvas, const HudInfo& hud, std::vector<uint8_t>& rgba) const {
    if (hud.curtain > 0) {
        // cortina gris que cierra desde arriba y abajo (como la de la ROM); cerrada muestra "STAGE N"
        const int rows = hud.curtain * 8;
        for (int y = 0; y < kScreenH; ++y) {
            if (y < rows || y >= kScreenH - rows) {
                for (int x = 0; x < kScreenW; ++x) canvas[static_cast<size_t>(y) * kScreenW + x] = 0x7C7C7C;
            }
        }
        if (hud.curtain >= 15) {
            const std::string text = !hud.introText.empty() ? hud.introText : "STAGE " + std::to_string(hud.introLevel > 0 ? hud.introLevel : 1);
            DrawText(canvas, kScreenW, (kScreenW - TextWidth(text)) / 2, 116, text, 0x000000);
            if (!hud.introPrompt.empty()) {
                DrawText(canvas, kScreenW, (kScreenW - TextWidth(hud.introPrompt)) / 2, 164, hud.introPrompt, 0x000000);
            }
            if (!hud.introSubtitle.empty()) {
                DrawText(canvas, kScreenW, (kScreenW - TextWidth(hud.introSubtitle)) / 2, 136, hud.introSubtitle, 0xA80000);
            }
        }
    }
    if (!hud.pauseItems.empty()) {
        // menu de pausa: la partida queda atenuada de fondo
        for (uint32_t& px : canvas) px = (px >> 1) & 0x7F7F7F;
        int widest = 0;
        for (const std::string& it : hud.pauseItems) widest = std::max(widest, TextWidth(it));
        const int boxW = widest + 100, boxH = static_cast<int>(hud.pauseItems.size()) * 22 + 40;
        const int bx = (kScreenW - boxW) / 2, by = (kScreenH - boxH) / 2;
        auto fillRect = [&](int x0, int y0, int x1, int y1, uint32_t color) {
            for (int yy = y0; yy < y1 && yy < kScreenH; ++yy)
                for (int xx = x0; xx < x1 && xx < kScreenW; ++xx) canvas[static_cast<size_t>(yy) * kScreenW + xx] = color;
        };
        fillRect(bx, by, bx + boxW, by + boxH, 0x080810);
        fillRect(bx, by, bx + boxW, by + 2, 0xFCFCFC);
        fillRect(bx, by + boxH - 2, bx + boxW, by + boxH, 0xFCFCFC);
        fillRect(bx, by, bx + 2, by + boxH, 0xFCFCFC);
        fillRect(bx + boxW - 2, by, bx + boxW, by + boxH, 0xFCFCFC);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth("PAUSA")) / 2, by + 10, "PAUSA", 0xFCA044);
        for (size_t i = 0; i < hud.pauseItems.size(); ++i) {
            const bool sel = static_cast<int>(i) == hud.pauseSelected;
            const int tx = bx + (boxW - widest) / 2, ty = by + 30 + static_cast<int>(i) * 22;   // bloque centrado, opciones a la izquierda
            hits_.push_back({bx + 4, ty - 6, bx + boxW - 4, ty + 15, static_cast<int>(i)});
            DrawText(canvas, kScreenW, tx, ty, hud.pauseItems[i], sel ? 0xF8D878 : 0xFCFCFC);
            if (sel) DrawSprite16(canvas, kScreenW, tx - 28, ty - 4, 0x18, 0);
        }
    }
    if (hud.overlay) {
        // panel oscuro con las teclas (en dos columnas) y el estado (tercera columna)
        const int px0 = 6, py0 = 6, px1 = kScreenW - 6, py1 = kScreenH - 6;
        for (int y = py0; y < py1; ++y) {
            for (int x = px0; x < px1; ++x) {
                const bool border = x < px0 + 2 || x >= px1 - 2 || y < py0 + 2 || y >= py1 - 2;
                canvas[static_cast<size_t>(y) * kScreenW + x] = border ? 0x7C7C7C : 0x080810;
            }
        }
        const size_t perColumn = (hud.leftLines.size() + 1) / 2;
        for (size_t i = 0; i < hud.leftLines.size(); ++i) {
            const int col = static_cast<int>(i / perColumn);
            const int row = static_cast<int>(i % perColumn);
            DrawText(canvas, kScreenW, 16 + col * 150, 14 + row * 10, hud.leftLines[i], row == 0 && col == 0 ? 0xF8D878 : 0xFCFCFC);
        }
        int y = 14;
        for (const std::string& line : hud.rightLines) {
            DrawText(canvas, kScreenW, 316, y, line, 0xB8F8B8);
            y += 10;
        }
    }
    if (!hud.banner.empty() && !hud.overlay) {
        const int bw = TextWidth(hud.banner);
        DrawText(canvas, kScreenW, (kScreenW - bw) / 2, 100, hud.banner, 0xFCFCFC);
    }
    if (hud.paused && !hud.overlay) {
        DrawText(canvas, kScreenW, (kScreenW - 5 * 8) / 2, 100, "PAUSE", 0xFCFCFC);
    }

    TextAuditFlush("juego");
    rgba.resize(canvas.size() * 4);
    for (size_t i = 0; i < canvas.size(); ++i) {
        rgba[i * 4 + 0] = static_cast<uint8_t>((canvas[i] >> 16) & 0xFF);
        rgba[i * 4 + 1] = static_cast<uint8_t>((canvas[i] >> 8) & 0xFF);
        rgba[i * 4 + 2] = static_cast<uint8_t>(canvas[i] & 0xFF);
        rgba[i * 4 + 3] = 0xFF;
    }
}

void Renderer::DrawSprite16(std::vector<uint32_t>& canvas, int w, int x, int y, int tileBase, int palette, int scale) const {
    for (int half = 0; half < 2; ++half) {
        for (int part = 0; part < 2; ++part) {
            const int tile = tileBase + half * 2 + part;
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    const int idx = ChrPixel(0, tile, px, py);
                    if (idx == 0) continue;
                    for (int sy = 0; sy < scale; ++sy) {
                        for (int sx = 0; sx < scale; ++sx) {
                            const int dx = x + (half * 8 + px) * scale + sx, dy = y + (part * 8 + py) * scale + sy;
                            if (dx >= 0 && dx < w && dy >= 0 && dy < kScreenH) {
                                canvas[static_cast<size_t>(dy) * w + dx] = NesColor(SpritePaletteColor(palette, idx));
                            }
                        }
                    }
                }
            }
        }
    }
}

namespace {
void FillRect(std::vector<uint32_t>& canvas, int w, int x0, int y0, int x1, int y1, uint32_t color) {
    for (int y = y0; y < y1 && y < kScreenH; ++y)
        for (int x = x0; x < x1 && x < w; ++x)
            if (x >= 0 && y >= 0) canvas[static_cast<size_t>(y) * w + x] = color;
}

std::string PadLeft(const std::string& s, size_t width) {
    return s.size() >= width ? s : std::string(width - s.size(), ' ') + s;
}

void ToRgba(const std::vector<uint32_t>& canvas, std::vector<uint8_t>& rgba) {
    TextAuditFlush("pantalla");
    rgba.resize(canvas.size() * 4);
    for (size_t i = 0; i < canvas.size(); ++i) {
        rgba[i * 4 + 0] = static_cast<uint8_t>((canvas[i] >> 16) & 0xFF);
        rgba[i * 4 + 1] = static_cast<uint8_t>((canvas[i] >> 8) & 0xFF);
        rgba[i * 4 + 2] = static_cast<uint8_t>(canvas[i] & 0xFF);
        rgba[i * 4 + 3] = 0xFF;
    }
}
} // namespace

void Renderer::RenderScoreScreen(const ScoreTally& t, std::vector<uint8_t>& rgba) const {
    hits_.clear();
    std::vector<uint32_t> canvas(static_cast<size_t>(kScreenW) * kScreenH, 0x000000);
    constexpr uint32_t kRed = 0xF83800, kOrange = 0xFCA044, kWhite = 0xFCFCFC;
    static const char* const kLabels[4] = {"I-PLAYER", "II-PLAYER", "III-PLAYER", "IV-PLAYER"};
    static const int kPoints[4] = {100, 200, 300, 400};

    DrawText(canvas, kScreenW, 140, 14, "HI-SCORE", kRed);
    DrawText(canvas, kScreenW, 140 + 9 * 8, 14, std::to_string(t.hiScore), kOrange);
    const std::string stage = "STAGE  " + std::to_string(t.level);
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(stage)) / 2, 32, stage, kWhite);

    const int n = t.players < 4 ? t.players : 4;
    const int startX = 40 + (4 - n) * 48;
    for (int p = 0; p < n; ++p) {
        const int x = startX + p * 96;
        const uint32_t color = NesColor(SpritePaletteColor(t.palette[p], 2));
        DrawText(canvas, kScreenW, x, 56, kLabels[p], color);
        DrawText(canvas, kScreenW, x, 68, PadLeft(std::to_string(t.score[p]), 8), kOrange);
    }
    for (int k = 0; k < 4; ++k) {
        const int y = 96 + k * 26;
        DrawSprite16(canvas, kScreenW, startX - 32, y - 4, 0x80 + k * 0x20, 2);     // icono del tipo de enemigo
        for (int p = 0; p < n; ++p) {
            const int x = startX + p * 96;
            const int shown = t.shown[p][k];
            DrawText(canvas, kScreenW, x, y, PadLeft(std::to_string(shown), 2), kWhite);
            DrawText(canvas, kScreenW, x + 24, y, PadLeft(std::to_string(shown * kPoints[k]), 4), kOrange);
            DrawText(canvas, kScreenW, x + 56, y, "PTS", kWhite);
        }
    }
    FillRect(canvas, kScreenW, startX - 32, 202, startX + n * 96 - 8, 204, kWhite);
    DrawText(canvas, kScreenW, startX - 32, 210, "TOTAL", kWhite);
    if (t.showTotals) {
        for (int p = 0; p < n; ++p) {
            DrawText(canvas, kScreenW, startX + p * 96 + 16, 210, PadLeft(std::to_string(t.totals[p]), 2), kOrange);   // despues de la etiqueta TOTAL
        }
    }
    if (t.bonusPlayer >= 0 && t.bonusPlayer < n) {
        const int x = startX + t.bonusPlayer * 96;
        DrawText(canvas, kScreenW, x, 222, "BONUS", kRed);
        DrawText(canvas, kScreenW, x, 231, "1000", kOrange);
        DrawText(canvas, kScreenW, x + 40, 231, "PTS", kWhite);
    }
    ToRgba(canvas, rgba);
}

void Renderer::RenderGameOver(std::vector<uint8_t>& rgba) const {
    hits_.clear();
    std::vector<uint32_t> canvas(static_cast<size_t>(kScreenW) * kScreenH, 0x000000);
    const int x0 = (kScreenW - 256) / 2;                 // posiciones originales de la ROM: GAME en (60, 70) y OVER en (60, 120)
    DrawBrickLetters(canvas, kScreenW, "GAME", x0 + 60, 70, 0);
    DrawBrickLetters(canvas, kScreenW, "OVER", x0 + 60, 120, 0);
    ToRgba(canvas, rgba);
}

void Renderer::RenderContinue(int continues, int seconds, bool canAccept, std::vector<uint8_t>& rgba) const {
    hits_.clear();
    hits_.push_back({0, 0, kScreenW, kScreenH, 0});                      // tocar en cualquier lado = continuar
    std::vector<uint32_t> canvas(static_cast<size_t>(kScreenW) * kScreenH, 0x000000);
    const int x0 = (kScreenW - 9 * 32) / 2;
    DrawBrickLetters(canvas, kScreenW, "CONTINUE?", x0, 40, 0);
    const std::string n = "CONTINUES: " + std::to_string(continues);
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(n)) / 2, 110, n, 0xF8D878);
    const std::string t = std::to_string(seconds < 0 ? 0 : seconds);
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(t)) / 2, 140, t, 0xFCFCFC);
    const std::string a = canAccept ? "START: CONTINUAR" : "ESPERANDO AL ANFITRION...";
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(a)) / 2, 180, a, 0xB8F8B8);
    const std::string b = "SI NO, TERMINA LA PARTIDA";
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(b)) / 2, 200, b, 0x909090);
    ToRgba(canvas, rgba);
}

// Pantalla de HI-SCORE de la ROM (sub_C44B): "HISCORE" enorme en (16, 50) y el puntaje debajo en (16, 100), con
// los ceros de la izquierda sin dibujar; la paleta del fondo cicla entre 5 y 8 mientras suena.
void Renderer::RenderHighScore(int score, int frame, std::vector<uint8_t>& rgba) const {
    hits_.clear();
    std::vector<uint32_t> canvas(static_cast<size_t>(kScreenW) * kScreenH, 0x000000);
    const int x0 = (kScreenW - 256) / 2;
    const int pal = 5 + (frame & 3);
    DrawBrickLetters(canvas, kScreenW, "HISCORE", x0 + 16, 50, pal);
    char digits[16];
    std::snprintf(digits, sizeof digits, "%07d", score < 0 ? 0 : (score > 9999999 ? 9999999 : score));
    int skip = 0;
    while (skip < 6 && digits[skip] == '0') ++skip;
    DrawBrickLetters(canvas, kScreenW, digits + skip, x0 + 16 + skip * 32, 100, pal);
    ToRgba(canvas, rgba);
}

void Renderer::DrawBrickLetters(std::vector<uint32_t>& canvas, int w, const char* text, int x0, int y0, int bgSet) const {
    int x = x0;
    for (const char* c = text; *c; ++c) {
        if (*c != ' ') {
            int mask[4][4] = {};
            for (int gy = 0; gy < 8; ++gy)
                for (int gx = 0; gx < 8; ++gx)
                    if (FontPixel(*c, gx, gy)) mask[gy / 2][gx / 2] |= 1 << ((gy & 1) * 2 + (gx & 1));
            for (int ty = 0; ty < 4; ++ty) {
                for (int tx = 0; tx < 4; ++tx) {
                    const int m = mask[ty][tx];
                    if (m == 0) continue;
                    for (int py = 0; py < 8; ++py) {
                        for (int px = 0; px < 8; ++px) {
                            const int idx = ChrPixel(1, m, px, py);
                            if (idx == 0) continue;
                            const int dx = x + tx * 8 + px, dy = y0 + ty * 8 + py;
                            if (dx >= 0 && dx < w && dy >= 0 && dy < kScreenH) {
                                canvas[static_cast<size_t>(dy) * w + dx] = NesColor(nes::tbl_D565_background_palette[bgSet * 16 + idx]);
                            }
                        }
                    }
                }
            }
        }
        x += 32;
    }
}

void Renderer::RenderMenu(const MenuView& m, std::vector<uint8_t>& rgba) const {
    hits_.clear();
    std::vector<uint32_t> canvas(static_cast<size_t>(kScreenW) * kScreenH, 0x000000);
    const bool dense = m.items.size() > 5;               // listas largas: mas arriba y mas juntas para no pisar las indicaciones de abajo
    int y = 100;
    if (m.logo) {
        // Pantalla de titulo como la de la ROM (coordenadas de la NES, centrada): el logo y las opciones suben desde abajo
        // hasta su lugar (scrollY baja de 240 a 0).
        const int x0 = (kScreenW - 256) / 2;
        const int sc = m.scrollY;
        auto text = [&](int col, int row, const std::string& t, uint32_t color) { DrawText(canvas, kScreenW, x0 + col * 8, row * 8 + sc, t, color); };
        text(2, 3, "I-" + PadLeft(std::to_string(m.p1Score), 6), 0xFCFCFC);
        {                                                                  // el puntaje maximo, centrado en la pantalla
            const std::string hi = "HI-" + PadLeft(std::to_string(m.hiScore), 7);
            DrawText(canvas, kScreenW, (kScreenW - TextWidth(hi)) / 2, 3 * 8 + sc, hi, 0xFCFCFC);
        }
        DrawBrickLetters(canvas, kScreenW, "BATTLE", x0 + 26, 46 + sc, 3);
        DrawBrickLetters(canvas, kScreenW, "CITY", x0 + 60, 86 + sc, 3);
        for (size_t i = 0; i < m.items.size(); ++i) {
            const int row = 17 + static_cast<int>(i) * 2;
            text(11, row, m.items[i], static_cast<int>(i) == m.selected ? 0xF8D878 : 0xFCFCFC);
            if (sc == 0) hits_.push_back({x0 + 11 * 8 - 32, row * 8 - 6, x0 + 11 * 8 + TextWidth(m.items[i]) + 24, row * 8 + 14, static_cast<int>(i)});
            if (static_cast<int>(i) == m.selected) DrawSprite16(canvas, kScreenW, x0 + 64, row * 8 - 4 + sc, 0x18, 0);
        }
        if (!m.footer.empty() && sc == 0) {
            DrawText(canvas, kScreenW, (kScreenW - TextWidth(m.footer)) / 2, 224, m.footer, 0x7C7C7C);
        }
        if (sc == 0) {
            if (!m.version.empty()) DrawText(canvas, kScreenW, kScreenW - TextWidth(m.version) - 6, 230, m.version, 0x7C7C7C);       // la version, abajo a la derecha
            if (!m.cornerNote.empty()) DrawText(canvas, kScreenW, (kScreenW - TextWidth(m.cornerNote)) / 2, 208, m.cornerNote, 0xF8D878);
        }
        ToRgba(canvas, rgba);
        return;
    }
    if (false) {
    } else if (!m.title.empty()) {
        const std::string title = Fit(m.title, kScreenW - 16);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(title)) / 2, dense ? 18 : 40, title, 0xFCA044);
        y = dense ? 42 : 90;
    }
    // El bloque de opciones va centrado en la pantalla y las opciones alineadas a la izquierda entre si, como en el original.
    std::vector<std::string> items;
    for (const std::string& it : m.items) items.push_back(Fit(it, kScreenW - 80));
    int widest = 0;
    for (const std::string& it : items) widest = std::max(widest, TextWidth(it));
    const int blockX = (kScreenW - widest) / 2;
    for (size_t i = 0; i < items.size(); ++i) {
        const bool selected = static_cast<int>(i) == m.selected;
        const bool enabled = i >= m.enabled.size() || m.enabled[i];
        const uint32_t color = !enabled ? 0x7C7C7C : (selected ? 0xF8D878 : 0xFCFCFC);
        const int x = blockX;
        if (enabled) hits_.push_back({x - 32, y - 5, x + widest + 16, y + (m.items.size() >= 9 ? 12 : (dense ? 14 : 15)), static_cast<int>(i)});
        DrawText(canvas, kScreenW, x, y, items[i], color);
        if (selected) {
            DrawSprite16(canvas, kScreenW, x - 28, y - 4, 0x18, 0);   // el tanque de P1 mirando a la derecha hace de cursor
        }
        y += m.items.size() >= 9 ? 16 : (dense ? 18 : 20);        // las listas de 9 o mas (configuracion) van mas juntas
    }
    if (!m.note.empty()) {                                                 // el aviso se parte en renglones si no entra
        int ny = dense ? y + 2 : y + 8;
        const int room = std::max(1, (220 - ny) / 10);                    // renglones que caben antes de las indicaciones de abajo
        std::vector<std::string> lines = Wrap(m.note, kScreenW - 24);
        if (static_cast<int>(lines.size()) > room) { lines.resize(static_cast<size_t>(room)); lines.back() = Fit(lines.back() + "...", kScreenW - 24); }
        for (const std::string& ln : lines) {
            DrawText(canvas, kScreenW, (kScreenW - TextWidth(ln)) / 2, ny, ln, 0xF83800);
            ny += 10;
        }
    }
    if (m.progress >= 0 || m.progress == -2) {                            // barra de progreso (descarga de la actualizacion)
        const int bw = 260, bh = 14, bx = (kScreenW - bw) / 2;
        int by = 140;
        FillRect(canvas, kScreenW, bx - 2, by - 2, bx + bw + 2, by + bh + 2, 0xFCFCFC);
        FillRect(canvas, kScreenW, bx, by, bx + bw, by + bh, 0x101018);
        if (m.progress >= 0) {
            const int fw = static_cast<int>(bw * std::min(1.0f, m.progress));
            FillRect(canvas, kScreenW, bx, by, bx + fw, by + bh, 0x58D854);
            FillRect(canvas, kScreenW, bx, by, bx + fw, by + 3, 0xA8F8A0);                      // un brillo arriba
        } else {
            const int seg = 70, pos = (m.tick * 3) % (bw + seg) - seg;
            FillRect(canvas, kScreenW, bx + std::max(0, pos), by, bx + std::min(bw, pos + seg), by + bh, 0x58D854);
        }
        if (!m.progressText.empty()) DrawText(canvas, kScreenW, (kScreenW - TextWidth(m.progressText)) / 2, by + bh + 10, m.progressText, 0xFCFCFC);
    }
    if (!m.footer.empty()) {
        const std::string footer = Fit(m.footer, kScreenW - 16);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(footer)) / 2, 224, footer, 0x7C7C7C);
    }
    ToRgba(canvas, rgba);
}

void Renderer::RenderLobby(const LobbyView& lIn, std::vector<uint8_t>& rgba) const {
    hits_.clear();
    LobbyView l = lIn;                                                  // con los textos largos ajustados a la pantalla
    l.title = Fit(l.title, kScreenW - 16);
    l.hint = Fit(l.hint, kScreenW - 16);
    l.addHint = Fit(l.addHint, kScreenW - 16);
    l.note = Fit(l.note, kScreenW - 16);
    l.startText = Fit(l.startText, kScreenW - 16);
    l.backText = Fit(l.backText, kScreenW - 16);
    std::vector<uint32_t> canvas(static_cast<size_t>(kScreenW) * kScreenH, 0x000000);
    static const int kPaletteOf[8] = {0, 1, 4, 5, 6, 7, 8, 9};
    static const char* const kNames[8] = {"AMARILLO", "VERDE", "AZUL", "ROJO", "VIOLETA", "NARANJA", "MARRON", "ROSA"};
    const std::string title = !l.title.empty() ? l.title : (l.net ? "SALA DE ESPERA" : "SELECCIÓN DE JUGADORES");
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(title)) / 2, 14, title, 0xFCA044);
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(l.hint)) / 2, 30, l.hint, 0x7C7C7C);
    if (!l.addHint.empty()) DrawText(canvas, kScreenW, (kScreenW - TextWidth(l.addHint)) / 2, 42, l.addHint, 0x58A8F8);
    if (l.teamSize > 0) {                                      // batalla vs en red: un renglon por equipo (los lugares sin persona los juega la IA)
        for (int t = 0; t < 2; ++t) {
            const int rowY = 50 + t * 74;
            const std::string head = l.grid8 ? std::string() : (t == 0 ? "EQUIPO 1" : "EQUIPO 2");
            if (!head.empty()) DrawText(canvas, kScreenW, 22, rowY - 2, head, NesColor(SpritePaletteColor(kPaletteOf[t == 0 ? 0 : 3], 2)));
            for (int k = 0; k < l.teamSize; ++k) {
                const int s = l.grid8 ? t * 4 + k : k * 2 + t;   // el equipo sale de la paridad del lugar en la sala (battle royale: dos filas de 4 lugares)
                const int x0 = 22 + (4 - l.teamSize) * 50 + k * 100;
                const int y0 = rowY + 10;
                const bool in = l.joined[s];
                const uint32_t frame = in ? 0xFCFCFC : 0x4C4C4C;
                FillRect(canvas, kScreenW, x0, y0, x0 + 92, y0 + 2, frame);
                FillRect(canvas, kScreenW, x0, y0 + 60, x0 + 92, y0 + 62, frame);
                FillRect(canvas, kScreenW, x0, y0, x0 + 2, y0 + 62, frame);
                FillRect(canvas, kScreenW, x0 + 90, y0, x0 + 92, y0 + 62, frame);
                const int pal = kPaletteOf[l.color[s] % 8];
                std::string name = in ? l.name[s].substr(0, 11) : std::string("IA");
                DrawText(canvas, kScreenW, x0 + (92 - TextWidth(name)) / 2, y0 + 6, name, in ? (l.mine[s] ? 0xF8D878 : 0xFCFCFC) : 0x7C7C7C);
                if (in) {
                    DrawSprite16(canvas, kScreenW, x0 + 30, y0 + 15, 0x00, pal, 2);
                    std::string st = l.ready[s] ? "LISTO" : (l.mine[s] ? l.meLine2[s] : std::string("ELIGIENDO"));
                    DrawText(canvas, kScreenW, x0 + (92 - TextWidth(st)) / 2, y0 + 51, st, l.ready[s] ? 0x58D854 : 0x7C7C7C);
                } else {                                       // sin jugador: un tanque gris de la IA
                    DrawSprite16(canvas, kScreenW, x0 + 30, y0 + 15, 0x00, 8, 2);
                    DrawText(canvas, kScreenW, x0 + (92 - TextWidth("LIBRE")) / 2, y0 + 51, "LIBRE", 0x4C4C4C);
                }
            }
        }
        if (!l.note.empty()) DrawText(canvas, kScreenW, (kScreenW - TextWidth(l.note)) / 2, 200, l.note, 0xF83800);
        hits_.push_back({(kScreenW - TextWidth(l.startText)) / 2 - 6, 208, (kScreenW + TextWidth(l.startText)) / 2 + 6, 222, 100});
        hits_.push_back({(kScreenW - TextWidth(l.backText)) / 2 - 6, 222, (kScreenW + TextWidth(l.backText)) / 2 + 6, 236, 101});
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(l.startText)) / 2, 212, l.startText, 0xF8D878);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(l.backText)) / 2, 226, l.backText, 0x7C7C7C);
        ToRgba(canvas, rgba);
        return;
    }
    for (int s = 0; s < 4; ++s) {
        const int x0 = 22 + s * 100;
        const int y0 = 56;
        hits_.push_back({x0, y0, x0 + 92, y0 + 128, s});                                  // tocar una casilla = unirse / cambiar de color
        const uint32_t frame = l.joined[s] ? 0xFCFCFC : 0x4C4C4C;
        FillRect(canvas, kScreenW, x0, y0, x0 + 92, y0 + 2, frame);
        FillRect(canvas, kScreenW, x0, y0 + 126, x0 + 92, y0 + 128, frame);
        FillRect(canvas, kScreenW, x0, y0, x0 + 2, y0 + 128, frame);
        FillRect(canvas, kScreenW, x0 + 90, y0, x0 + 92, y0 + 128, frame);
        std::string name = "JUGADOR " + std::to_string(s + 1);
        if (l.net && l.joined[s]) name = l.name[s].substr(0, 11);
        DrawText(canvas, kScreenW, x0 + (l.net ? (92 - TextWidth(name)) / 2 : 10), y0 + 10, name, l.joined[s] ? (l.mine[s] ? 0xF8D878 : 0xFCFCFC) : 0x7C7C7C);
        if (l.joined[s]) {
            const int pal = kPaletteOf[l.color[s] % 8];
            DrawSprite16(canvas, kScreenW, x0 + 22, y0 + 34, 0x00, pal, 3);          // el tanque en su color
            const std::string cn = !l.tag[s].empty() ? l.tag[s] : std::string(kNames[l.color[s] % 8]);
            const int tw = TextWidth(cn);
            DrawText(canvas, kScreenW, x0 + (92 - tw) / 2, y0 + 92, cn, NesColor(SpritePaletteColor(pal, 2)));
            if (l.net) {                                                           // sala en red: "LISTO" o lo que falta
                if (l.ready[s]) {
                    const std::string ok = "LISTO";
                    DrawText(canvas, kScreenW, x0 + (92 - TextWidth(ok)) / 2, y0 + 108, ok, 0x58D854);
                } else if (l.mine[s]) {
                    DrawText(canvas, kScreenW, x0 + (92 - TextWidth(l.upDownText)) / 2, y0 + 104, l.upDownText, 0x7C7C7C);
                    DrawText(canvas, kScreenW, x0 + (92 - TextWidth(l.meLine2[s])) / 2, y0 + 114, l.meLine2[s], 0x7C7C7C);
                } else {
                    DrawText(canvas, kScreenW, x0 + (92 - TextWidth("ELIGIENDO")) / 2, y0 + 108, "ELIGIENDO", 0x7C7C7C);
                }
                continue;
            }
            const size_t sp = l.upDownText.find(' ');
            const std::string line1 = sp == std::string::npos ? l.upDownText : l.upDownText.substr(0, sp);
            const std::string line2 = sp == std::string::npos ? std::string() : l.upDownText.substr(sp + 1);
            DrawText(canvas, kScreenW, x0 + (92 - TextWidth(line1)) / 2, y0 + 104, line1, 0x7C7C7C);
            if (!line2.empty()) DrawText(canvas, kScreenW, x0 + (92 - TextWidth(line2)) / 2, y0 + 114, line2, 0x7C7C7C);
        } else if (l.net) {
            DrawText(canvas, kScreenW, x0 + (92 - TextWidth("ESPERANDO")) / 2, y0 + 52, "ESPERANDO", 0x7C7C7C);
            DrawText(canvas, kScreenW, x0 + (92 - TextWidth("JUGADOR")) / 2, y0 + 64, "JUGADOR", 0x7C7C7C);
        } else {
            DrawText(canvas, kScreenW, x0 + 12, y0 + 44, l.joinLine1.empty() ? "PRESIONA" : l.joinLine1, 0x7C7C7C);
            DrawText(canvas, kScreenW, x0 + 12, y0 + 56, l.joinLine2.empty() ? "DISPARO" : l.joinLine2, 0x7C7C7C);
            const std::string key = l.joinKey[s];
            DrawText(canvas, kScreenW, x0 + (92 - TextWidth(key)) / 2, y0 + 76, key, 0xFCFCFC);
        }
    }
    if (!l.note.empty()) DrawText(canvas, kScreenW, (kScreenW - TextWidth(l.note)) / 2, 192, l.note, 0xF83800);
    hits_.push_back({(kScreenW - TextWidth(l.startText)) / 2 - 6, 202, (kScreenW + TextWidth(l.startText)) / 2 + 6, 216, 100});     // empezar
    hits_.push_back({(kScreenW - TextWidth(l.backText)) / 2 - 6, 218, (kScreenW + TextWidth(l.backText)) / 2 + 6, 234, 101});       // volver
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(l.startText)) / 2, 206, l.startText, 0xF8D878);
    DrawText(canvas, kScreenW, (kScreenW - TextWidth(l.backText)) / 2, 222, l.backText, 0x7C7C7C);
    ToRgba(canvas, rgba);
}


// ---------------------------------------------------------------------------
// Editor de niveles
// ---------------------------------------------------------------------------

namespace {

uint32_t Blend(uint32_t base, uint32_t over, int percent) {
    auto ch = [&](int shift) {
        const int b = (base >> shift) & 0xFF, o = (over >> shift) & 0xFF;
        return static_cast<uint32_t>((b * (100 - percent) + o * percent) / 100) << shift;
    };
    return ch(16) | ch(8) | ch(0);
}

void Rect(std::vector<uint32_t>& c, int x0, int y0, int x1, int y1, uint32_t color) {
    FillRect(c, kScreenW, x0, y0, x1, y1, color);
}

void Frame(std::vector<uint32_t>& c, int x0, int y0, int x1, int y1, uint32_t color, int t = 1) {
    Rect(c, x0, y0, x1, y0 + t, color);
    Rect(c, x0, y1 - t, x1, y1, color);
    Rect(c, x0, y0, x0 + t, y1, color);
    Rect(c, x1 - t, y0, x1, y1, color);
}

void Shade(std::vector<uint32_t>& c, int x0, int y0, int x1, int y1, uint32_t color, int percent) {
    for (int y = std::max(0, y0); y < std::min(kScreenH, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(kScreenW, x1); ++x) {
            uint32_t& px = c[static_cast<size_t>(y) * kScreenW + x];
            px = Blend(px, color, percent);
        }
}

}  // namespace

void Renderer::DrawBlockIcon(std::vector<uint32_t>& canvas, int x, int y, int id, const nes::Battle& battle) const {
    if (id == 15) {                                                  // lava (en los niveles es el 15)
        const uint32_t colors[4] = {LavaColor(0, 0), LavaColor(1, 0), LavaColor(2, 0), LavaColor(3, 0)};
        FillRect(canvas, kScreenW, x, y, x + 16, y + 16, colors[0]);
        for (int k = 0; k < 4; ++k) DrawColoredTile(canvas, kScreenW, x + (k & 1) * 8, y + (k >> 1) * 8, kWaterTile, colors);
        return;
    }
    if (id == nes::Battle::kSandBlockId) {                          // arena: los cuatro mosaicos de la nieve en tonos de arena
        FillRect(canvas, kScreenW, x, y, x + 16, y + 16, SandColor(0));
        const uint32_t colors[4] = {SandColor(0), SandColor(1), SandColor(2), SandColor(3)};
        for (int k = 0; k < 4; ++k) DrawColoredTile(canvas, kScreenW, x + (k & 1) * 8, y + (k >> 1) * 8, kSnowTile, colors);
        return;
    }
    if (id >= 16 && id <= 23) {                                       // ladrillo o hierro de un solo mosaico, en una punta
        const bool steel = id >= 20;
        const int attr = nes::tbl_DABB_nametable_attribute[steel ? 9 : 4] & 3;
        for (int k = 0; k < 4; ++k) {
            const int tile = k == ((id - 16) & 3) ? (steel ? 0x10 : 0x0F) : 0;
            DrawHudTile(canvas, kScreenW, x + (k & 1) * 8, y + (k >> 1) * 8, tile, battle, attr);
        }
        return;
    }
    const int attr = nes::tbl_DABB_nametable_attribute[id & 15] & 3;
    for (int k = 0; k < 4; ++k) {
        DrawHudTile(canvas, kScreenW, x + (k & 1) * 8, y + (k >> 1) * 8, nes::tbl_DACB_block_data[(id & 15) * 4 + k], battle, attr);
    }
}

// Dibujo reducido de un nivel (cada bloque son cs x cs pixeles, 8 o 12): bloques, apariciones, lugares de los jugadores, la base y las aguilas.
void Renderer::DrawLevelPreview(std::vector<uint32_t>& canvas, int x0, int y0, const LevelDef& d, int cs) const {
    auto fill = [&](int x, int y, int w, int h, uint32_t color) {
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx) {
                const int px = x + xx, py = y + yy;
                if (px >= 0 && px < kScreenW && py >= 0 && py < kScreenH) canvas[static_cast<size_t>(py) * kScreenW + px] = color;
            }
    };
    const int h = cs / 2;
    auto u = [&](int v) { return v * cs / 8; };                                           // medidas pensadas para 8 px, a escala
    // que cuarto del bloque ocupa cada id: bits 1 = arriba izq., 2 = arriba der., 4 = abajo izq., 8 = abajo der.
    auto quarters = [](int id, bool& steel) {
        steel = false;
        switch (id) {
            case 4: return 15;
            case 0: return 10;  case 1: return 12;  case 2: return 5;  case 3: return 3;
            case 16: return 1;  case 17: return 2;  case 18: return 4;  case 19: return 8;
            case 9: steel = true; return 15;
            case 5: steel = true; return 10;  case 6: steel = true; return 12;  case 7: steel = true; return 5;  case 8: steel = true; return 3;
            case 20: steel = true; return 1;  case 21: steel = true; return 2;  case 22: steel = true; return 4;  case 23: steel = true; return 8;
            default: return 0;
        }
    };
    for (int r = 0; r < kGridRows; ++r) {
        for (int c = 0; c < kLevelCols; ++c) {
            const int id = d.cells[r][c], x = x0 + c * cs, y = y0 + r * cs;
            bool steel = false;
            const int q = quarters(id, steel);
            if (q != 0) {
                for (int k = 0; k < 4; ++k) {
                    if (!(q & (1 << k))) continue;
                    const int qx = x + (k & 1) * h, qy = y + (k >> 1) * h;
                    fill(qx, qy, h, h, steel ? 0xB4B4B4 : 0xC84C0C);
                    fill(qx, qy + h - 1, h, 1, steel ? 0x6C6C6C : 0x7C2C00);                    // la junta
                    fill(qx + h - 1, qy, 1, h, steel ? 0x6C6C6C : 0x7C2C00);
                }
            } else if (id == 10) {
                fill(x, y, cs, cs, 0x2038EC);
                fill(x, y + u(2), cs, 1, 0x5C94FC); fill(x, y + u(5), cs, 1, 0x5C94FC);
            } else if (id == 11) {
                fill(x, y, cs, cs, 0x007800);
                fill(x + u(1), y + u(1), u(3), u(3), 0x00B800); fill(x + u(4), y + u(4), u(3), u(3), 0x00B800);
            } else if (id == 12) {
                fill(x, y, cs, cs, 0xB8E8FC);
                fill(x + u(1), y + u(1), u(2), u(2), 0xFCFCFC); fill(x + u(5), y + u(5), u(2), u(2), 0xFCFCFC);
            } else if (id == 14) {
                fill(x, y, cs, cs, 0xF8D878);
                fill(x + u(1), y + u(2), u(2), 1, 0xC8A040); fill(x + u(4), y + u(5), u(2), 1, 0xC8A040);
            } else if (id == 15) {
                fill(x, y, cs, cs, 0xD83800);
                fill(x + u(1), y + u(1), u(3), u(2), 0xFC9838); fill(x + u(4), y + u(5), u(3), u(2), 0xFC9838);
            }
        }
    }
    auto marker = [&](int col, int row, uint32_t color) {
        fill(x0 + col * cs + 1, y0 + row * cs + 1, cs - 2, cs - 2, 0xFCFCFC);
        fill(x0 + col * cs + 2, y0 + row * cs + 2, cs - 4, cs - 4, color);
    };
    if (d.versus) {
        for (int t = 0; t < 2; ++t) {
            fill(x0 + (d.vsEagleCol[t] - 1) * cs, y0 + (d.vsEagleRow[t] - 1) * cs, 3 * cs, 3 * cs, 0x606060);
            marker(d.vsEagleCol[t], d.vsEagleRow[t], t == 0 ? 0xF8D878 : 0xD82800);
        }
    } else {
        for (int r = d.baseRow - 1; r <= d.baseRow; ++r)                                      // la base: muro de ladrillo y el aguila abajo al centro
            for (int c = d.baseCol - 1; c <= d.baseCol + 1; ++c) fill(x0 + c * cs, y0 + r * cs, cs, cs, 0xC84C0C);
        fill(x0 + d.baseCol * cs, y0 + d.baseRow * cs, cs, cs, 0xB4B4B4);
        for (int p = 0; p < 4; ++p) marker(d.playerCol[p], d.playerRow[p], 0x58A8F8);
        if (d.boss) marker(d.eagleCol, d.eagleRow, 0xFC9838);
    }
    for (int i = 0; i < d.nSpawns; ++i) marker(d.spawnCol[i], d.spawnRow[i], 0xD82800);
}

void Renderer::RenderEditor(const nes::Battle& stage, const EditorView& v, std::vector<uint8_t>& rgba) const {
    hits_.clear();
    std::vector<uint32_t> canvas;
    RenderNesScreen(stage, canvas);
    const int worldW = stage.MapCols() * 8;
    if (worldW != kScreenW) canvas.resize(static_cast<size_t>(kScreenW) * kScreenH, 0);
    DrawBombing(canvas, kScreenW, stage);
    const bool plain = v.slotOnly || v.page == 2 || v.page == 5;                     // la lista de casilleros va sobre fondo negro
    if (plain) std::fill(canvas.begin(), canvas.end(), 0u);

    if (!plain) {
    // cuadricula tenue sobre el campo
    for (int by = 0; by < kScreenH; by += 16)
        for (int x = 0; x < kScreenW; ++x) {
            uint32_t& px = canvas[static_cast<size_t>(by) * kScreenW + x];
            px = Blend(px, 0xFFFFFF, 14);
        }
    for (int bx = 0; bx < kScreenW; bx += 16)
        for (int y = 0; y < kScreenH; ++y) {
            uint32_t& px = canvas[static_cast<size_t>(y) * kScreenW + bx];
            px = Blend(px, 0xFFFFFF, 14);
        }
    // lugares donde empiezan los jugadores (no se tocan)
    for (int p = 0; p < 4 && !v.versus; ++p) {
        const int x0 = v.playerCol[p] * 16, y0 = v.playerRow[p] * 16;
        Frame(canvas, x0 + 1, y0 + 1, x0 + 15, y0 + 15, 0x58A8F8);
        DrawText(canvas, kScreenW, x0 + 4, y0 + 4, std::to_string(p + 1), 0x58A8F8);
    }
    // la fortaleza del aguila enemiga (el motor ya la dibuja; aca se marca su alcance en la herramienta ÁGUILA)
    if (v.versus) {                                                   // donde empiezan los jugadores de cada equipo (no se mueven)
        for (int t = 0; t < 2; ++t) {
            for (int k = 0; k < 4; ++k) {                                 // los 4 lugares de los miembros del equipo (personas o IA)
                int pc, pr;
                nes::Battle::VsSideBlock(v.vsEagleCol[t], v.vsEagleRow[t], k, pc, pr);
                const int x0 = pc * 16, y0 = pr * 16;
                Frame(canvas, x0 + 1, y0 + 1, x0 + 15, y0 + 15, 0x58A8F8);
                DrawText(canvas, kScreenW, x0 + 4, y0 + 4, "J", 0x58A8F8);
            }
            if (v.tool == 2) Frame(canvas, (v.vsEagleCol[t] - 1) * 16, (v.vsEagleRow[t] - 1) * 16, (v.vsEagleCol[t] + 2) * 16, (v.vsEagleRow[t] + 2) * 16, 0xFC9838, 2);
        }
    }
    if (v.boss && v.tool == 2) {
        const int r = v.eagleReach;
        Frame(canvas, (v.eagleCol - r) * 16, (v.eagleRow - r) * 16, (v.eagleCol + r + 1) * 16, (v.eagleRow + r + 1) * 16, 0xFC9838, 2);
    }
    // puntos de aparicion de enemigos
    for (int i = 0; i < v.nSpawns; ++i) {
        const int x0 = v.spawnCol[i] * 16, y0 = v.spawnRow[i] * 16;
        Rect(canvas, x0 + 2, y0 + 2, x0 + 14, y0 + 14, v.versus ? ((std::abs(v.spawnCol[i] - v.vsEagleCol[0]) + std::abs(v.spawnRow[i] - v.vsEagleRow[0]) <= std::abs(v.spawnCol[i] - v.vsEagleCol[1]) + std::abs(v.spawnRow[i] - v.vsEagleRow[1])) ? 0xB08800 : 0xD82800) : 0xD82800);
        Frame(canvas, x0 + 2, y0 + 2, x0 + 14, y0 + 14, 0xFCFCFC);
        const std::string n = std::to_string(i + 1);
        DrawText(canvas, kScreenW, x0 + 8 - TextWidth(n) / 2, y0 + 4, n, 0xFCFCFC);
    }
    // el bloque elegido hace de puntero: se ve (semitransparente) en la celda donde esta el mouse
    if (v.showCursor) {
        const int cx = v.cursorCol * 16, cy = v.cursorRow * 16;
        if (v.tool == 0) {
            const int id = v.variantIds.empty() ? 13 : v.variantIds[static_cast<size_t>(std::min(v.variant, static_cast<int>(v.variantIds.size()) - 1))];
            std::vector<uint32_t> before(256);
            for (int yy = 0; yy < 16; ++yy)
                for (int xx = 0; xx < 16; ++xx) before[static_cast<size_t>(yy * 16 + xx)] = canvas[static_cast<size_t>(cy + yy) * kScreenW + cx + xx];
            if (id == 13) {                                                       // borrar: una cruz roja
                for (int k = 2; k < 14; ++k) {
                    canvas[static_cast<size_t>(cy + k) * kScreenW + cx + k] = 0xF83800;
                    canvas[static_cast<size_t>(cy + k) * kScreenW + cx + 15 - k] = 0xF83800;
                }
            } else {
                DrawBlockIcon(canvas, cx, cy, id, stage);
                for (int yy = 0; yy < 16; ++yy)
                    for (int xx = 0; xx < 16; ++xx) {
                        uint32_t& px = canvas[static_cast<size_t>(cy + yy) * kScreenW + cx + xx];
                        px = Blend(before[static_cast<size_t>(yy * 16 + xx)], px, 78);
                    }
            }
            Frame(canvas, cx, cy, cx + 16, cy + 16, id == 13 ? 0xF83800 : 0xF8D878, 1);
        } else if (v.tool == 3) {                                                  // puntero: resalta lo que se puede arrastrar
            bool hot = false;
            for (int i = 0; i < v.nSpawns; ++i) hot = hot || (v.spawnCol[i] == v.cursorCol && v.spawnRow[i] == v.cursorRow);
            for (int p = 0; p < 4 && !v.versus; ++p) hot = hot || (v.playerCol[p] == v.cursorCol && v.playerRow[p] == v.cursorRow);
            if (!v.versus && std::abs(v.cursorCol - v.baseCol) <= 1 && v.cursorRow >= v.baseRow - 1 && v.cursorRow <= v.baseRow) {      // la base entera
                Frame(canvas, (v.baseCol - 1) * 16, (v.baseRow - 1) * 16, (v.baseCol + 2) * 16, (v.baseRow + 1) * 16, 0xFC9838, 2);
            }
            if (v.versus) { for (int t = 0; t < 2; ++t) hot = hot || (v.vsEagleCol[t] == v.cursorCol && v.vsEagleRow[t] == v.cursorRow); }
            else if (v.boss) hot = hot || (v.eagleCol == v.cursorCol && v.eagleRow == v.cursorRow);
            if (hot) Frame(canvas, cx, cy, cx + 16, cy + 16, 0xFC9838, 2);
        } else if (v.tool == 1) {
            Shade(canvas, cx + 2, cy + 2, cx + 14, cy + 14, 0xD82800, 70);
            Frame(canvas, cx + 2, cy + 2, cx + 14, cy + 14, 0xFCFCFC);
            Frame(canvas, cx, cy, cx + 16, cy + 16, 0xF83800, 1);
        } else {
            Frame(canvas, cx, cy, cx + 16, cy + 16, 0xFC9838, 2);
        }
    }
    }

    // tira con el bloque elegido (arriba o abajo, del lado opuesto al cursor)
    if (!plain && v.page == 0) {
        const int sy = v.cursorRow >= 8 ? 0 : kScreenH - 12;
        Shade(canvas, 0, sy, kScreenW, sy + 12, 0x000000, 70);
        const std::string hint = v.tool == 0 ? (v.variantIds.size() > 1 ? "FLECHAS O WASD: VARIANTE" : "RUEDA: OTRO BLOQUE")
                                             : (v.tool == 1 ? "CLIC: PONER O QUITAR" : (v.tool == 2 ? "CLIC: MOVER EL ÁGUILA" : "ARRASTRÁ JUGADORES, APARICIONES, BASE Y ÁGUILA"));
        const std::string label = v.tool == 0 ? v.blockLabel : (v.tool == 1 ? "APARICIÓN " + std::to_string(v.nSpawns) + "/" + std::to_string(v.versus ? 4 : kMaxSpawns) : (v.tool == 2 ? "ÁGUILA ENEMIGA" : "PUNTERO"));
        DrawText(canvas, kScreenW, 4, sy + 2, label, 0xF8D878);
        if (v.side < 0.05f) {
            const int hx = kScreenW - 4 - TextWidth(hint);
            if (hx > 8 + TextWidth(label) + 16) DrawText(canvas, kScreenW, hx, sy + 2, hint, 0x9C9C9C);
        }
    }
    // barra lateral desplegable (mouse en el borde derecho)
    if (!plain && v.page == 0 && v.side > 0.01f) {
        const int left = kScreenW - static_cast<int>(kEditorSideW * v.side);
        Rect(canvas, left, 0, kScreenW, kScreenH, 0x101018);
        Rect(canvas, left, 0, left + 1, kScreenH, 0x7C7C7C);
        static const char* const kNames[7] = {"LADRILLO", "HIERRO", "AGUA", "ARBUSTO", "HIELO", "ARENA", "LAVA"};
        static const int kGroupIcon[7] = {4, 9, 10, 11, 12, 14, 15};
        static const char* const kRowText[4] = {"PUNTERO", "APARICIÓN", "ÁGUILA", "MENÚ"};
        for (int r = 0; r < kEditorRows; ++r) {
            if (r == 9 && !v.boss && !v.versus) continue;
            const int ry = EditorRowY(r);
            const bool selected = (r < 7 && v.tool == 0 && v.group == r) || (r == 7 && v.tool == 3) || (r == 8 && v.tool == 1) || (r == 9 && v.tool == 2);
            if (selected) Rect(canvas, left + 1, ry, kScreenW, ry + kEditorRowH, 0x2C2C40);
            else if (r == v.hover) Rect(canvas, left + 1, ry, kScreenW, ry + kEditorRowH, 0x1C1C2C);
            const int ix = left + 1;
            if (r < 7) {
                DrawBlockIcon(canvas, ix, ry, kGroupIcon[r], stage);
                DrawText(canvas, kScreenW, left + 18, ry + 4, kNames[r], selected ? 0xF8D878 : 0xC8C8C8);
            } else {
                if (r == 7) {                                                           // puntero (flecha del mouse)
                    static const char* const kArrow[14] = {"X", "XX", "XWX", "XWWX", "XWWWX", "XWWWWX", "XWWWWWX", "XWWWWWWX", "XWWWWXXXX", "XWWXWX", "XWX XWX", "XX  XWX", "X    XWX", "      XX"};
                    for (int yy = 0; yy < 14; ++yy)
                        for (int xx = 0; kArrow[yy][xx]; ++xx)
                            if (kArrow[yy][xx] != ' ') canvas[static_cast<size_t>(ry + 1 + yy) * kScreenW + ix + 3 + xx] = kArrow[yy][xx] == 'W' ? 0xFCFCFC : 0x101018 + 0x000000;
                } else if (r == 8) {
                    Rect(canvas, ix + 2, ry + 2, ix + 14, ry + 14, 0xD82800);
                    Frame(canvas, ix + 2, ry + 2, ix + 14, ry + 14, 0xFCFCFC);
                } else if (r == 9) {
                    Frame(canvas, ix + 1, ry + 1, ix + 15, ry + 15, 0xFC9838, 2);
                } else {
                    for (int k = 0; k < 3; ++k) Rect(canvas, ix + 2, ry + 3 + k * 4, ix + 14, ry + 5 + k * 4, 0xC8C8C8);
                }
                DrawText(canvas, kScreenW, left + 18, ry + 4, Fit(kRowText[r - 7], 72), selected ? 0xF8D878 : 0xC8C8C8);
            }
        }
        if (v.tool == 0 && !v.variantName.empty()) DrawText(canvas, kScreenW, left + 4, 183, Fit(v.variantName, 88), 0xF8D878);     // la variante (se cambia con las flechas)
        for (size_t i = 0; i < v.statusLines.size(); ++i) DrawText(canvas, kScreenW, left + 4, 197 + static_cast<int>(i) * 9, Fit(v.statusLines[i], 88), i == 0 && v.dirty ? 0xF8D878 : 0x9C9C9C);
        if (!v.warnings.empty()) DrawText(canvas, kScreenW, left + 4, 225, Fit("AVISOS: " + std::to_string(v.warnings.size()), 88), 0xF83800);
        if (v.dirty) Rect(canvas, kScreenW - 8, 2, kScreenW - 3, 7, 0xF8D878);          // cambios sin guardar
    }

    auto panel = [&](int x0, int y0, int x1, int y1) {
        Rect(canvas, x0, y0, x1, y1, 0x080810);
        Frame(canvas, x0, y0, x1, y1, 0xFCFCFC, 2);
    };

    if (v.page != 0) Shade(canvas, 0, 0, kScreenW, kScreenH, 0x000000, 60);
    if (v.page == 1) {                                                // menu
        int widest = 0;
        for (const std::string& it : v.menuItems) widest = std::max(widest, TextWidth(it));
        const int boxW = widest + 100, boxH = static_cast<int>(v.menuItems.size()) * 18 + 40;
        const int bx = (kScreenW - boxW) / 2, by = (kScreenH - boxH) / 2;
        panel(bx, by, bx + boxW, by + boxH);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth("EDITOR DE NIVELES")) / 2, by + 8, "EDITOR DE NIVELES", 0xFCA044);
        for (size_t i = 0; i < v.menuItems.size(); ++i) {
            const bool sel = static_cast<int>(i) == v.menuSel;
            const int tx = bx + (boxW - widest) / 2, ty = by + 26 + static_cast<int>(i) * 18;
            DrawText(canvas, kScreenW, tx, ty, v.menuItems[i], sel ? 0xF8D878 : 0xFCFCFC);
            if (sel) DrawSprite16(canvas, kScreenW, tx - 28, ty - 4, 0x18, 0);
        }
    } else if (v.page == 2) {                                         // casilleros
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(v.slotTitle)) / 2, 10, Fit(v.slotTitle, kScreenW - 16), 0xFCA044);
        const int rows = (v.slotCount + v.slotCols - 1) / v.slotCols;
        const int cellW = std::min(40, 400 / v.slotCols), cellH = std::min(22, 150 / rows);
        const int gx = (kScreenW - cellW * v.slotCols) / 2, gy = 30;
        for (int s = 0; s < v.slotCount; ++s) {
            const int x0 = gx + (s % v.slotCols) * cellW, y0 = gy + (s / v.slotCols) * cellH;
            const int st = s < static_cast<int>(v.slotState.size()) ? v.slotState[static_cast<size_t>(s)] : 0;
            const uint32_t fill = st == 1 ? 0x006800 : (st == 2 ? 0x0030A0 : 0x202028);
            Rect(canvas, x0 + 1, y0 + 1, x0 + cellW - 1, y0 + cellH - 1, fill);
            hits_.push_back({x0, y0, x0 + cellW, y0 + cellH, s + 1});                       // tocar una casilla la elige
            const std::string n = std::to_string(s + 1);
            DrawText(canvas, kScreenW, x0 + (cellW - TextWidth(n)) / 2, y0 + (cellH - 8) / 2, n, st == 0 ? 0x7C7C7C : 0xFCFCFC);
            if (s + 1 == v.slotSel) Frame(canvas, x0, y0, x0 + cellW, y0 + cellH, 0xF8D878, 2);
        }
        const int iy = gy + rows * cellH + 10;
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(Fit(v.slotInfo, kScreenW - 16))) / 2, iy, Fit(v.slotInfo, kScreenW - 16), 0xFCFCFC);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(Fit(v.slotLegend, kScreenW - 16))) / 2, iy + 14, Fit(v.slotLegend, kScreenW - 16), 0x7C7C7C);
        const std::string foot = Fit(v.slotFooter.empty() ? std::string("ACEPTAR VER   BORRAR: SUPR / X   VOLVER") : v.slotFooter, kScreenW - 16);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(foot)) / 2, 224, foot, 0x7C7C7C);
    } else if (v.page == 5) {                                         // vista previa del nivel: las flechas pasan de un nivel a otro
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(Fit(v.slotTitle, kScreenW - 16))) / 2, 6, Fit(v.slotTitle, kScreenW - 16), 0xFCA044);
        const int cs = 12, pw = kLevelCols * cs, ph = kGridRows * cs;
        const int px0 = (kScreenW - pw) / 2, py0 = 16;
        Rect(canvas, px0 - 2, py0 - 2, px0 + pw + 2, py0 + ph + 2, 0x606060);
        Rect(canvas, px0, py0, px0 + pw, py0 + ph, 0x000000);
        if (v.hasPreview) DrawLevelPreview(canvas, px0, py0, v.preview, cs);
        else DrawText(canvas, kScreenW, px0 + (pw - TextWidth("VACÍO")) / 2, py0 + ph / 2 - 4, "VACÍO", 0x7C7C7C);
        const int ay = py0 + ph / 2 - 8;                                                   // flechas a los costados
        DrawText(canvas, kScreenW, 18, ay, "<", 0xF8D878);
        DrawText(canvas, kScreenW, kScreenW - 26, ay, ">", 0xF8D878);
        const std::string info = Fit(v.slotInfo, kScreenW - 16);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(info)) / 2, py0 + ph + 6, info, 0xFCFCFC);
        const std::string legend = Fit(v.slotLegend, kScreenW - 16);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(legend)) / 2, py0 + ph + 17, legend, 0x7C7C7C);
        const std::string foot = Fit(v.slotFooter, kScreenW - 16);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(foot)) / 2, 229, foot, 0xB8F8B8);
    } else if (v.page == 3) {                                         // confirmacion
        const int w = std::max(TextWidth(v.confirmText) + 40, 200), h = 70;
        const int bx = (kScreenW - w) / 2, by = (kScreenH - h) / 2;
        panel(bx, by, bx + w, by + h);
        DrawText(canvas, kScreenW, (kScreenW - TextWidth(v.confirmText)) / 2, by + 14, v.confirmText, 0xFCFCFC);
        DrawText(canvas, kScreenW, bx + w / 2 - 50, by + 44, "SÍ", v.confirmSel == 0 ? 0xF8D878 : 0x7C7C7C);
        DrawText(canvas, kScreenW, bx + w / 2 + 28, by + 44, "NO", v.confirmSel == 1 ? 0xF8D878 : 0x7C7C7C);
    } else if (v.page == 4) {                                         // mensaje
        int widest = 0;
        for (const std::string& l : v.message) widest = std::max(widest, TextWidth(l));
        const int w = widest + 40, h = static_cast<int>(v.message.size()) * 12 + 36;
        const int bx = (kScreenW - w) / 2, by = (kScreenH - h) / 2;
        panel(bx, by, bx + w, by + h);
        for (size_t i = 0; i < v.message.size(); ++i) {
            DrawText(canvas, kScreenW, (kScreenW - TextWidth(v.message[i])) / 2, by + 12 + static_cast<int>(i) * 12, v.message[i], i == 0 ? 0xB8F8B8 : 0xF83800);
        }
    }
    ToRgba(canvas, rgba);
}

} // namespace app
