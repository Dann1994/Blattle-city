#pragma once

// Configuracion del usuario: teclas de cada jugador, botones del mando, vibracion,
// fuego amigo y tipo de sonido. Se guarda en un archivo de texto sencillo
// ("clave=valor") junto al ejecutable. Los codigos de teclas y de botones son los de
// raylib; esta clase no depende de ella.

#include <string>

namespace app {

struct Config {
    static constexpr int kActions = 6;       // arriba, abajo, izquierda, derecha, disparo, disparo 2
    enum Action { Up, Down, Left, Right, Fire, Fire2 };

    int key[4][kActions] = {};               // tecla de cada accion por ranura de jugador (0 = sin asignar)
    int padFire = 0;                         // botones del mando (ids de raylib)
    int padFire2 = 0;
    int padCancel = 0;                       // volver atras / cancelar en los menus
    int padStart = 0;                        // inicio: pausa y continuar
    int version = 0;                         // 2 = mando con B para volver; los archivos viejos se restablecen
    bool vibration = true;
    std::string playerName;                  // nombre para identificarse en la red local (modo LAN)
    bool turbo = false;                      // mantener el disparo apretado dispara sin parar
    bool fullscreen = false;                 // pantalla completa (ventana sin bordes)
    int hiScore = 20000;                     // record guardado
    int friendlyFire = 1;                    // 0 desactivado, 1 paraliza, 2 daña
    bool useSamples = true;                  // sonido grabado (true) o sintetizador de la NES (false)
    // campania: dificultad (0 facil, 1 normal, 2 dificil, 3 personalizada) y sus ajustes; batalla vs: vidas
    int diffPreset = 1;
    int cuFriendly = 1;                      // personalizada: fuego amigo (0 no, 1 paraliza, 2 daña)
    int cuEvery = 5;                         // personalizada: se gana 1 continue cada 1, 5 o 10 niveles
    bool cuShared = false;                   // personalizada: los items afectan a todos
    int cuAi = 1;                            // personalizada: dificultad de la IA (0 facil, 1 normal, 2 dificil)
    int cuLives = 3;                         // personalizada: vidas
    int vsLives = 3;                         // batalla vs y battle royale a muerte: vidas (1..10)
    bool voiceChat = true;                   // chat de voz en las partidas en red
    bool autoUpdate = true;                  // al abrir el juego, buscar actualizaciones

    // Valores por defecto de teclas y mando: los carga el programa (conoce los codigos).
    int defaultKey[4][kActions] = {};
    int defaultPad[4] = {0, 0, 0, 0};        // fire, fire2, cancel, start

    void ResetKeys() {
        for (int p = 0; p < 4; ++p)
            for (int a = 0; a < kActions; ++a) key[p][a] = defaultKey[p][a];
    }
    void ResetPad() {
        padFire = defaultPad[0];
        padFire2 = defaultPad[1];
        padCancel = defaultPad[2];
        padStart = defaultPad[3];
        vibration = true;
    }

    bool Load(const std::string& path);      // false si no existe o no se pudo leer
    bool Save(const std::string& path) const;
};

} // namespace app
