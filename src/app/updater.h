#pragma once

// Buscador de actualizaciones: al abrir el juego pregunta a GitHub (releases del repositorio) si hay una version mas nueva.
//   PC:      descarga el instalador (BattleCityNES-Setup-X.Y.Z.exe) y lo ejecuta en silencio; el juego se cierra, se actualiza y vuelve a abrirse.
//   Android: descarga el APK con el DownloadManager del sistema y abre la pantalla de instalacion de Android (el sistema siempre pide confirmar).
// Las entregas se publican como releases con etiqueta  pc-vX.Y.Z  y  android-vX.Y.Z  (ver tools/publish_release.py).

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace app {

std::string AppVersion();                                         // "1.0.0"
int CompareVersions(const std::string& a, const std::string& b);  // -1 si a < b, 0 si iguales, 1 si a > b (componentes numericos: 1.10.0 > 1.9.0)

struct ReleaseAsset {
    std::string name, url;
    long long size = 0;
};
struct ReleaseInfo {
    std::string tag, version, name, body;
    std::vector<ReleaseAsset> assets;
};

// Del JSON de la API de releases (lista) elige la mas nueva cuya etiqueta sea  <prefix>v<numeros>  (por ejemplo "pc-" -> pc-v1.2.0).
bool ParseLatestRelease(const std::string& json, const std::string& prefix, ReleaseInfo& out);
// El archivo a descargar: en PC el instalador (*Setup*.exe); en Android el *.apk.
const ReleaseAsset* PickAsset(const ReleaseInfo& release, bool android);

class Updater {
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Downloaded, Failed };
    Updater();
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    void StartCheck();                       // pregunta en segundo plano (no bloquea el juego)
    void StartDownload();                    // descarga la version nueva (solo si state() == Available)
    void Cancel();                           // corta una descarga en curso
    void Poll();                             // una vez por cuadro (Android consulta al DownloadManager)
    State state() const { return state_.load(); }
    ReleaseInfo release() const;
    float progress() const { return progress_.load(); }      // 0..1 (-1 si no se sabe)
    std::string error() const;
    // Ya descargada: en PC lanza el instalador (el juego tiene que cerrarse enseguida: devuelve true); en Android abre la pantalla de instalacion.
    bool Install();
    // Para pruebas: otra direccion de la API (por ejemplo un servidor local) en vez de https://api.github.com/repos/<repo>/releases.
    static std::string ApiUrl();
    // Solo pruebas de pantalla: fuerza un estado (y la version nueva) sin tocar la red.
    void DebugForce(State s, const std::string& version, float progress = 0, const std::string& err = std::string());

private:
    void CheckThread();
    void DownloadThread();
    void Fail(const std::string& why);
    std::atomic<State> state_{State::Idle};
    std::atomic<float> progress_{0};
    std::atomic<bool> cancel_{false};
    mutable std::mutex mu_;
    ReleaseInfo release_;
    std::string error_, file_;
    std::thread worker_;
    long long androidId_ = -1;
};

} // namespace app
