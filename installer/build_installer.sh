#!/usr/bin/env bash
# Arma el instalador de PC: dist/BattleCityNES-Setup-<version>.exe  (la version sale de src/app/version.h).
# Necesita Inno Setup 6 (por defecto en C:/android-dev/tools/innosetup; ISCC=ruta/ISCC.exe para otra ubicacion).
# Antes hay que tener compilado build/bin (battlecitynes.exe y level_editor.exe) y la carpeta dist/BattleCityNES con assets y LEEME.txt.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISCC="${ISCC:-/c/android-dev/tools/innosetup/ISCC.exe}"
VERSION="$(grep -o 'BCNES_VERSION "[0-9.]*"' "$ROOT/src/app/version.h" | grep -o '[0-9][0-9.]*')"
cp "$ROOT/build/bin/battlecitynes.exe" "$ROOT/build/bin/level_editor.exe" "$ROOT/dist/BattleCityNES/"
"$ISCC" "//DAppVersion=$VERSION" "$(cygpath -w "$ROOT/installer/BattleCityNES.iss")"
echo "Instalador: $ROOT/dist/BattleCityNES-Setup-$VERSION.exe"
