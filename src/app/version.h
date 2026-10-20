#pragma once

// Version del juego (la ve el jugador en la pantalla de titulo y la usa el buscador de actualizaciones).
// Se cambia SOLO al publicar una entrega (tools/publish_release.py lo hace): MAYOR.MENOR.PARCHE.
// BCNES_VERSION_CODE es el numero de version de Android (siempre creciente): MAYOR * 10000 + MENOR * 100 + PARCHE.
#define BCNES_VERSION "1.0.5"
#define BCNES_VERSION_CODE 10005

// Repositorio de donde salen las actualizaciones (releases con etiquetas pc-vX.Y.Z y android-vX.Y.Z).
#define BCNES_UPDATE_REPO "Dann1994/Blattle-city"
