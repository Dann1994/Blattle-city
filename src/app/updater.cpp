#include "updater.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>

#include "version.h"

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>
#endif

namespace app {

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

std::string AppVersion() { return BCNES_VERSION; }

static std::vector<long> VersionParts(const std::string& v) {
    std::vector<long> out;
    long cur = 0;
    bool any = false;
    for (char c : v) {
        if (std::isdigit(static_cast<unsigned char>(c))) { cur = cur * 10 + (c - '0'); any = true; }
        else if (c == '.') { out.push_back(any ? cur : 0); cur = 0; any = false; }
        else break;
    }
    if (any) out.push_back(cur);
    return out;
}

int CompareVersions(const std::string& a, const std::string& b) {
    const std::vector<long> pa = VersionParts(a), pb = VersionParts(b);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// JSON minimo (lo justo para leer la lista de releases de GitHub)
// ---------------------------------------------------------------------------

namespace {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
    const Json* Get(const std::string& key) const {
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string Str(const std::string& key) const { const Json* j = Get(key); return j && j->type == String ? j->s : std::string(); }
    bool Flag(const std::string& key) const { const Json* j = Get(key); return j && j->type == Bool && j->b; }
    double Num(const std::string& key) const { const Json* j = Get(key); return j && j->type == Number ? j->n : 0; }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& t) : t_(t) {}
    bool Parse(Json& out) { Ws(); if (!Value(out, 0)) return false; Ws(); return true; }

private:
    const std::string& t_;
    size_t p_ = 0;
    void Ws() { while (p_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[p_]))) ++p_; }
    bool Lit(const char* w) {
        const size_t n = std::char_traits<char>::length(w);
        if (t_.compare(p_, n, w) != 0) return false;
        p_ += n;
        return true;
    }
    static void Utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    bool Str(std::string& out) {
        if (p_ >= t_.size() || t_[p_] != '"') return false;
        ++p_;
        while (p_ < t_.size()) {
            const char c = t_[p_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (p_ >= t_.size()) return false;
            const char e = t_[p_++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (p_ + 4 > t_.size()) return false;
                    unsigned cp = static_cast<unsigned>(std::strtoul(t_.substr(p_, 4).c_str(), nullptr, 16));
                    p_ += 4;
                    Utf8(out, cp);
                    break;
                }
                default: out += e; break;
            }
        }
        return false;
    }
    bool Value(Json& j, int depth) {
        if (depth > 20 || p_ >= t_.size()) return false;
        const char c = t_[p_];
        if (c == '{') {
            ++p_;
            j.type = Json::Object;
            Ws();
            if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
            for (;;) {
                Ws();
                std::string key;
                if (!Str(key)) return false;
                Ws();
                if (p_ >= t_.size() || t_[p_++] != ':') return false;
                Ws();
                Json v;
                if (!Value(v, depth + 1)) return false;
                j.obj.push_back({key, std::move(v)});
                Ws();
                if (p_ >= t_.size()) return false;
                if (t_[p_] == ',') { ++p_; continue; }
                if (t_[p_] == '}') { ++p_; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++p_;
            j.type = Json::Array;
            Ws();
            if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
            for (;;) {
                Ws();
                Json v;
                if (!Value(v, depth + 1)) return false;
                j.arr.push_back(std::move(v));
                Ws();
                if (p_ >= t_.size()) return false;
                if (t_[p_] == ',') { ++p_; continue; }
                if (t_[p_] == ']') { ++p_; return true; }
                return false;
            }
        }
        if (c == '"') { j.type = Json::String; return Str(j.s); }
        if (Lit("true")) { j.type = Json::Bool; j.b = true; return true; }
        if (Lit("false")) { j.type = Json::Bool; j.b = false; return true; }
        if (Lit("null")) { j.type = Json::Null; return true; }
        char* end = nullptr;
        j.n = std::strtod(t_.c_str() + p_, &end);
        if (end == t_.c_str() + p_) return false;
        j.type = Json::Number;
        p_ = static_cast<size_t>(end - t_.c_str());
        return true;
    }
};

bool IsVersionText(const std::string& v) {
    if (v.empty() || !std::isdigit(static_cast<unsigned char>(v[0]))) return false;
    for (char c : v) if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') return false;
    return true;
}

} // namespace

bool ParseLatestRelease(const std::string& json, const std::string& prefix, ReleaseInfo& out) {
    Json root;
    JsonParser parser(json);
    if (!parser.Parse(root) || root.type != Json::Array) return false;
    bool found = false;
    for (const Json& r : root.arr) {
        if (r.type != Json::Object || r.Flag("draft") || r.Flag("prerelease")) continue;
        const std::string tag = r.Str("tag_name");
        const std::string head = prefix + "v";
        if (tag.compare(0, head.size(), head) != 0) continue;
        const std::string version = tag.substr(head.size());
        if (!IsVersionText(version)) continue;
        if (found && CompareVersions(version, out.version) <= 0) continue;
        ReleaseInfo info;
        info.tag = tag;
        info.version = version;
        info.name = r.Str("name");
        info.body = r.Str("body");
        if (const Json* assets = r.Get("assets")) {
            for (const Json& a : assets->arr) {
                ReleaseAsset ra;
                ra.name = a.Str("name");
                ra.url = a.Str("browser_download_url");
                ra.size = static_cast<long long>(a.Num("size"));
                if (!ra.name.empty() && !ra.url.empty()) info.assets.push_back(ra);
            }
        }
        out = info;
        found = true;
    }
    return found;
}

const ReleaseAsset* PickAsset(const ReleaseInfo& release, bool android) {
    for (const ReleaseAsset& a : release.assets) {
        std::string n = a.name;
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (android) { if (n.size() > 4 && n.compare(n.size() - 4, 4, ".apk") == 0) return &a; }
        else if (n.find("setup") != std::string::npos && n.size() > 4 && n.compare(n.size() - 4, 4, ".exe") == 0) return &a;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Red: WinINet en Windows, JNI (updater_android.cpp) en Android
// ---------------------------------------------------------------------------

#ifdef __ANDROID__
bool AndroidHttpGet(const std::string& url, std::string& out, std::string& err);
long long AndroidDownloadStart(const std::string& url, const std::string& title, std::string& err);
// status: 1 pendiente, 2 en curso, 4 en pausa, 8 listo, 16 fallo
bool AndroidDownloadPoll(long long id, int& status, long long& soFar, long long& total);
bool AndroidInstall(long long id, std::string& err);
#endif

namespace {

#ifdef _WIN32
struct InetHandle {
    HINTERNET h = nullptr;
    explicit InetHandle(HINTERNET x = nullptr) : h(x) {}
    ~InetHandle() { if (h) InternetCloseHandle(h); }
    InetHandle(const InetHandle&) = delete;
    InetHandle& operator=(const InetHandle&) = delete;
};

// Abre la direccion y devuelve el manejador con la respuesta lista para leer (200 = bien).
bool InetOpen(InetHandle& net, InetHandle& req, const std::string& url, std::string& err, long long* length) {
    net.h = InternetOpenA(("BattleCityNES/" + AppVersion()).c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net.h) { err = "SIN CONEXIÓN"; return false; }
    DWORD timeout = 10000;
    InternetSetOptionA(net.h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof timeout);
    InternetSetOptionA(net.h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof timeout);
    const char* headers = "Accept: application/vnd.github+json\r\n";
    req.h = InternetOpenUrlA(net.h, url.c_str(), headers, static_cast<DWORD>(-1),
                             INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
    if (!req.h) { err = "NO SE PUDO CONECTAR"; return false; }
    DWORD code = 0, len = sizeof code;
    if (HttpQueryInfoA(req.h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &len, nullptr) && code != 200) {
        err = "EL SERVIDOR RESPONDIÓ " + std::to_string(code);
        return false;
    }
    if (length) {
        DWORD cl = 0;
        len = sizeof cl;
        *length = HttpQueryInfoA(req.h, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &cl, &len, nullptr) ? static_cast<long long>(cl) : -1;
    }
    return true;
}
#endif

bool HttpGet(const std::string& url, std::string& out, std::string& err) {
#ifdef _WIN32
    InetHandle net, req;
    if (!InetOpen(net, req, url, err, nullptr)) return false;
    char buf[8192];
    for (;;) {
        DWORD got = 0;
        if (!InternetReadFile(req.h, buf, sizeof buf, &got)) { err = "SE CORTÓ LA CONEXIÓN"; return false; }
        if (got == 0) break;
        out.append(buf, got);
        if (out.size() > 8 * 1024 * 1024) { err = "RESPUESTA DEMASIADO GRANDE"; return false; }
    }
    return true;
#elif defined(__ANDROID__)
    return AndroidHttpGet(url, out, err);
#else
    (void)url; (void)out;
    err = "NO DISPONIBLE EN ESTA PLATAFORMA";
    return false;
#endif
}

#ifdef _WIN32
std::string TempDir() {
    char buf[MAX_PATH + 1] = {};
    const DWORD n = GetTempPathA(MAX_PATH, buf);
    return std::string(buf, n);
}
#endif

} // namespace

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------

std::string Updater::ApiUrl() {
    if (const char* env = std::getenv("BCNES_UPDATE_API")) if (*env) return env;       // pruebas
    return std::string("https://api.github.com/repos/") + BCNES_UPDATE_REPO + "/releases?per_page=30";
}

Updater::Updater() = default;

Updater::~Updater() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

ReleaseInfo Updater::release() const {
    std::lock_guard<std::mutex> lk(mu_);
    return release_;
}

std::string Updater::error() const {
    std::lock_guard<std::mutex> lk(mu_);
    return error_;
}

void Updater::Fail(const std::string& why) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        error_ = why;
    }
    state_ = State::Failed;
}

void Updater::StartCheck() {
    const State s = state_.load();
    if (s == State::Checking || s == State::Downloading) return;
    if (worker_.joinable()) worker_.join();
    state_ = State::Checking;
    progress_ = 0;
    cancel_ = false;
    worker_ = std::thread([this] { CheckThread(); });
}

void Updater::CheckThread() {
    std::string body, err;
    if (!HttpGet(ApiUrl(), body, err)) { Fail(err); return; }
    ReleaseInfo best;
#ifdef __ANDROID__
    const bool android = true;
#else
    const bool android = false;
#endif
    if (!ParseLatestRelease(body, android ? "android-" : "pc-", best)) { state_ = State::UpToDate; return; }      // todavia no hay entregas con version
    if (CompareVersions(best.version, AppVersion()) <= 0) { state_ = State::UpToDate; return; }
    if (!PickAsset(best, android)) { Fail("LA ENTREGA " + best.version + " NO TIENE ARCHIVO PARA DESCARGAR"); return; }
    {
        std::lock_guard<std::mutex> lk(mu_);
        release_ = best;
    }
    state_ = State::Available;
}

void Updater::StartDownload() {
    if (state_.load() != State::Available) return;
    if (worker_.joinable()) worker_.join();
    state_ = State::Downloading;
    progress_ = 0;
    cancel_ = false;
#ifdef __ANDROID__
    const ReleaseInfo r = release();
    const ReleaseAsset* a = PickAsset(r, true);
    std::string err;
    androidId_ = a ? AndroidDownloadStart(a->url, "Battle City NES " + r.version, err) : -1;
    if (androidId_ < 0) Fail(err.empty() ? "NO SE PUDO EMPEZAR LA DESCARGA" : err);
#else
    worker_ = std::thread([this] { DownloadThread(); });
#endif
}

void Updater::DownloadThread() {
#ifdef _WIN32
    const ReleaseInfo r = release();
    const ReleaseAsset* a = PickAsset(r, false);
    if (!a) { Fail("SIN ARCHIVO PARA DESCARGAR"); return; }
    const std::string path = TempDir() + "BattleCityNES-Setup-" + r.version + ".exe";
    std::string err;
    InetHandle net, req;
    long long length = -1;
    if (!InetOpen(net, req, a->url, err, &length)) { Fail(err); return; }
    if (length <= 0) length = a->size;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { Fail("NO SE PUDO GUARDAR EL ARCHIVO"); return; }
    char buf[65536];
    long long done = 0;
    for (;;) {
        if (cancel_) { f.close(); std::remove(path.c_str()); state_ = State::Idle; return; }
        DWORD got = 0;
        if (!InternetReadFile(req.h, buf, sizeof buf, &got)) { Fail("SE CORTÓ LA DESCARGA"); return; }
        if (got == 0) break;
        f.write(buf, got);
        done += got;
        progress_ = length > 0 ? static_cast<float>(std::min<long long>(done, length)) / static_cast<float>(length) : -1.0f;
    }
    f.close();
    if (a->size > 0 && done != a->size) { Fail("LA DESCARGA QUEDÓ INCOMPLETA"); return; }
    {
        std::lock_guard<std::mutex> lk(mu_);
        file_ = path;
    }
    progress_ = 1;
    state_ = State::Downloaded;
#endif
}

void Updater::DebugForce(State s, const std::string& version, float progress, const std::string& err) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        release_.version = version;
        error_ = err;
    }
    progress_ = progress;
    state_ = s;
}

void Updater::Cancel() {
    cancel_ = true;
    if (state_.load() == State::Downloading) {
        if (worker_.joinable()) worker_.join();
        if (state_.load() == State::Downloading) state_ = State::Available;
    }
}

void Updater::Poll() {
#ifdef __ANDROID__
    if (state_.load() != State::Downloading || androidId_ < 0) return;
    int status = 0;
    long long soFar = 0, total = 0;
    if (!AndroidDownloadPoll(androidId_, status, soFar, total)) { Fail("SE PERDIÓ LA DESCARGA"); return; }
    if (total > 0) progress_ = static_cast<float>(soFar) / static_cast<float>(total);
    if (status == 8) { progress_ = 1; state_ = State::Downloaded; }
    else if (status == 16) Fail("FALLÓ LA DESCARGA");
#endif
}

bool Updater::Install() {
    if (state_.load() != State::Downloaded) return false;
#ifdef _WIN32
    std::string path;
    {
        std::lock_guard<std::mutex> lk(mu_);
        path = file_;
    }
    // el instalador va por usuario (sin pedir permisos), cierra y reemplaza el juego y lo vuelve a abrir
    const HINSTANCE r = ShellExecuteA(nullptr, "open", path.c_str(), "/SILENT /SUPPRESSMSGBOXES /CLOSEAPPLICATIONS /NOCANCEL /NORESTART", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(r) <= 32) { Fail("NO SE PUDO ABRIR EL INSTALADOR"); return false; }
    return true;
#elif defined(__ANDROID__)
    std::string err;
    if (!AndroidInstall(androidId_, err)) { Fail(err.empty() ? "NO SE PUDO ABRIR LA INSTALACIÓN" : err); return false; }
    return false;                                            // el juego sigue abierto: Android pide confirmar y reemplaza la app
#else
    return false;
#endif
}

} // namespace app
