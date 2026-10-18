#!/usr/bin/env bash
# Compila el juego para Android (arm64) y arma el APK firmado con la clave de depuracion.
# Necesita: JDK 17, Android SDK (build-tools 34, plataforma 34) y NDK 26 (ver LEEME_ANDROID.txt).
#   ANDROID_DEV (por defecto C:/android-dev) = carpeta con jdk-*/ y sdk/
set -e
DEV="${ANDROID_DEV:-/c/android-dev}"
JDK="$(ls -d "$DEV"/jdk-* | head -1)"
SDK="$DEV/sdk"
NDK="$(ls -d "$SDK"/ndk/* | head -1)"
BT="$SDK/build-tools/34.0.0"
SRC="$(cd "$(dirname "$0")/.." && pwd)"
ABI="${ABI:-arm64-v8a}"                 # arm64-v8a (telefonos) o x86_64 (emulador)
OUT="$DEV/apk-out-$ABI"
export JAVA_HOME="$JDK" PATH="$JDK/bin:/c/msys64/mingw64/bin:$PATH"

cmake -S "$SRC" -B "$DEV/build-$ABI" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-24 -DPLATFORM=Android -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_MAKE_PROGRAM=/c/msys64/mingw64/bin/ninja.exe > /dev/null
cmake --build "$DEV/build-$ABI"

rm -rf "$OUT" && mkdir -p "$OUT/stage/lib/$ABI" "$OUT/stage/assets"
cp "$DEV/build-$ABI/libmain.so" "$OUT/stage/lib/$ABI/"
cp "$SRC/assets/chr.bin" "$SRC/assets/icon.png" "$SRC/assets/plane.png" "$OUT/stage/assets/"
cp -r "$SRC/assets/sounds" "$OUT/stage/assets/sounds"

"$BT/aapt2.exe" compile --dir "$SRC/android/res" -o "$OUT/res.zip"
"$BT/aapt2.exe" link -o "$OUT/base.apk" -I "$SDK/platforms/android-34/android.jar" --manifest "$SRC/android/AndroidManifest.xml" \
      -R "$OUT/res.zip" -A "$OUT/stage/assets" --min-sdk-version 24 --target-sdk-version 34 --version-code 1 --version-name 1.0
python - "$OUT" "$ABI" <<'PY'
import sys, zipfile
out, abi = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out + "/base.apk", "a", zipfile.ZIP_DEFLATED) as z:
    z.write(out + "/stage/lib/" + abi + "/libmain.so", "lib/" + abi + "/libmain.so")
PY
"$BT/zipalign.exe" -f -p 4 "$OUT/base.apk" "$OUT/aligned.apk"
KS="$DEV/debug.keystore"
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias debug -keyalg RSA -keysize 2048 \
      -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
"$BT/apksigner.bat" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out "$OUT/final.apk" "$OUT/aligned.apk"
if [ "$ABI" = "arm64-v8a" ]; then cp "$OUT/final.apk" "$SRC/dist/BattleCityNES.apk"; echo "APK: $SRC/dist/BattleCityNES.apk"; else echo "APK de prueba ($ABI): $OUT/final.apk"; fi
