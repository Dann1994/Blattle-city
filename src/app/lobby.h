#pragma once

// Seleccion de jugadores del modo local: cada jugador se une con su boton de
// disparo y elige su color con arriba/abajo. No se pueden repetir colores: los que
// ya tiene otro jugador no aparecen disponibles.

namespace app {

constexpr int kColorCount = 8;

// Nombres de los 8 colores (sin tildes: la fuente de la NES no las tiene).
inline constexpr const char* kColorNames[kColorCount] = {"AMARILLO", "VERDE", "AZUL", "ROJO", "VIOLETA", "NARANJA", "MARRON", "ROSA"};

class Lobby {
public:
    Lobby();

    // El jugador de la ranura (0..3) se une con el color libre mas cercano a su ranura. Devuelve false si ya estaba.
    bool Join(int slot);
    // El jugador deja la seleccion (el primero no puede irse si es el unico).
    bool Leave(int slot);
    // Cambia el color al siguiente (+1) o anterior (-1) entre los que no usa otro jugador.
    void CycleColor(int slot, int direction);

    // MODO BATALLA VS: cada jugador esta en el equipo 0 o 1 (por defecto alternados) y lo cambia con arriba/abajo.
    int Team(int slot) const { return team_[slot & 3]; }
    static int TeamColor(int team) { return team ? 3 : 0; }      // amarillo y rojo
    void ToggleTeam(int slot) { if (slot >= 0 && slot < 4 && joined_[slot]) team_[slot] ^= 1; }
    int TeamCount(int team) const;
    bool VersusReady(int maxPerTeam = 2) const;  // los dos equipos con al menos 1 jugador y hasta `maxPerTeam` (el resto lo juega la IA)
    bool Joined(int slot) const { return joined_[slot]; }
    int Color(int slot) const { return color_[slot]; }
    bool ColorTaken(int color, int exceptSlot) const;
    int Count() const;
    // Ranuras unidas en orden: el jugador 1 del motor es la primera, etc.
    int SlotOfPlayer(int index) const;

private:
    bool joined_[4] = {false, false, false, false};
    int color_[4] = {0, 1, 2, 3};
    int team_[4] = {0, 1, 0, 1};
};

// Reparto automatico de mandos: cada mando conectado (ids 0-3) se asigna al primer jugador
// libre (ranura 0, 1, 2, 3), y al desconectarse libera su ranura. padOfSlot[s] = id del mando o -1.
void AssignPads(const bool available[4], int padOfSlot[4]);

} // namespace app
