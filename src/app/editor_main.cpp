// level_editor.exe: el editor de niveles como herramienta aparte. Con el se agregan niveles al MODO PRINCIPAL del juego
// (los archivos quedan en niveles/principal/ junto al ejecutable: los niveles 1-40 se pueden reemplazar y del 41 al 99 se suman a la
// serie). Con --propios edita en cambio los 40 casilleros de la seccion de niveles propios.
//
// Uso:   level_editor.exe [--propios] [--shot archivo.png --nivel N [--especial]]    (--shot dibuja una imagen y sale)

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "editor.h"
#include "editor_input.h"
#include "levels.h"
#include "renderer.h"

namespace {

std::string FindChr(const char* argv0) {
    std::vector<std::string> candidates = {"assets/chr.bin", "../assets/chr.bin", "../../assets/chr.bin", "../../../assets/chr.bin"};
    const std::string dir = argv0 ? argv0 : "";
    const size_t slash = dir.find_last_of("/\\");
    if (slash != std::string::npos) {
        const std::string base = dir.substr(0, slash + 1);
        candidates.push_back(base + "assets/chr.bin");
        candidates.push_back(base + "../assets/chr.bin");
        candidates.push_back(base + "../../assets/chr.bin");
    }
#ifdef BCNES_ASSETS_DIR
    candidates.push_back(std::string(BCNES_ASSETS_DIR) + "chr.bin");
#endif
    for (const std::string& c : candidates) {
        FILE* f = std::fopen(c.c_str(), "rb");
        if (f) {
            std::fclose(f);
            return c;
        }
    }
    return "assets/chr.bin";
}

}  // namespace

int main(int argc, char** argv) {
    bool own = false, boss = false;
    std::string shot, keys;
    int level = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--propios") own = true;
        else if (a == "--especial") boss = true;
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--nivel" && i + 1 < argc) level = std::atoi(argv[++i]);
        else if (a == "--teclas" && i + 1 < argc) keys = argv[++i];
    }
    {
        const std::string exe = argv[0];
        const size_t slash = exe.find_last_of("/\\");
        app::SetLevelRoot((slash == std::string::npos ? std::string() : exe.substr(0, slash + 1)) + "niveles/");
    }
    app::Renderer renderer;
    if (!renderer.LoadChr(FindChr(argv[0]))) {
        std::fprintf(stderr, "No se encontro assets/chr.bin (los graficos de la ROM)\n");
        return 2;
    }
    app::LevelEditor editor(own ? app::LevelEditor::Target::Own : app::LevelEditor::Target::Main);
    editor.SetHelp("ESC MENÚ  TAB HERRAM.");
    if (level > 0) editor.LoadFromSlot(level);
    else if (boss) editor.NewLevel(true);

    std::vector<uint8_t> rgba;
    if (!shot.empty()) {
        // --teclas "menu,down,accept,...": toques de prueba antes de sacar la imagen
        size_t from = 0;
        while (from < keys.size()) {
            size_t to = keys.find(',', from);
            if (to == std::string::npos) to = keys.size();
            const std::string t = keys.substr(from, to - from);
            from = to + 1;
            app::EditorKeys k;
            if (t == "left") k.left = true;
            else if (t == "right") k.right = true;
            else if (t == "up") k.up = true;
            else if (t == "down") k.down = true;
            else if (t == "place") k.place = true;
            else if (t == "erase") k.erase = true;
            else if (t == "accept") k.accept = true;
            else if (t == "back") k.back = true;
            else if (t == "menu") k.menu = true;
            else if (t == "tool") k.tool = true;
            else if (t == "next") k.nextItem = true;
            else if (t == "prev") k.prevItem = true;
            editor.Step(k);
            editor.Step(app::EditorKeys{});
        }                                                   // una imagen y listo (para revisar el dibujo)
        app::EditorView view;
        editor.BuildView(view);
        renderer.RenderEditor(editor.Stage(), view, rgba);
        Image img{rgba.data(), app::kScreenW, app::kScreenH, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ExportImage(img, shot.c_str());
        return 0;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1296, 720, own ? "Editor de niveles propios - Battle City NES" : "Editor de niveles del modo principal - Battle City NES");
    SetWindowMinSize(432, 240);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    Image blank = GenImageColor(app::kScreenW, app::kScreenH, BLACK);
    ImageFormat(&blank, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Texture2D tex = LoadTextureFromImage(blank);
    UnloadImage(blank);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    while (!WindowShouldClose() && !editor.WantsExit()) {
        const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
        const float scale = std::min(sw / app::kScreenW, sh / app::kScreenH);
        const float dw = app::kScreenW * scale, dh = app::kScreenH * scale;
        const float dx = (sw - dw) / 2, dy = (sh - dh) / 2;
        editor.Step(app::GatherEditorKeys(dx, dy, scale));
        app::EditorView view;
        editor.BuildView(view);
        renderer.RenderEditor(editor.Stage(), view, rgba);
        UpdateTexture(tex, rgba.data());
        BeginDrawing();
        ClearBackground(Color{16, 16, 24, 255});
        DrawTexturePro(tex, Rectangle{0, 0, static_cast<float>(app::kScreenW), static_cast<float>(app::kScreenH)}, Rectangle{dx, dy, dw, dh},
                       Vector2{0, 0}, 0.0f, WHITE);
        EndDrawing();
    }
    UnloadTexture(tex);
    CloseWindow();
    return 0;
}
