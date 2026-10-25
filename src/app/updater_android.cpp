// Android: red y descarga de actualizaciones con las clases del sistema (JNI), sin codigo Java propio.
//   - consulta a GitHub con HttpURLConnection;
//   - descarga el APK con el DownloadManager (notificacion del sistema, reintentos) y abre la pantalla de instalacion de Android.
#ifdef __ANDROID__

#include <jni.h>
#include <android_native_app_glue.h>

#include <string>

extern "C" struct android_app* GetAndroidApp(void);          // de raylib

namespace app {

namespace {

// Engancha el hilo actual a la JVM mientras dura el objeto.
class Jni {
public:
    Jni() {
        android_app* a = GetAndroidApp();
        if (!a || !a->activity) return;
        vm_ = a->activity->vm;
        activity_ = a->activity->clazz;
        if (vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) == JNI_EDETACHED) {
            if (vm_->AttachCurrentThread(&env_, nullptr) == JNI_OK) attached_ = true;
            else env_ = nullptr;
        }
    }
    ~Jni() { if (attached_ && vm_) vm_->DetachCurrentThread(); }
    JNIEnv* env() const { return env_; }
    jobject activity() const { return activity_; }
    bool ok() const { return env_ != nullptr && activity_ != nullptr; }
    // true si hubo una excepcion de Java (la limpia)
    bool Threw() const {
        if (!env_->ExceptionCheck()) return false;
        env_->ExceptionClear();
        return true;
    }

private:
    JavaVM* vm_ = nullptr;
    JNIEnv* env_ = nullptr;
    jobject activity_ = nullptr;
    bool attached_ = false;
};

jstring Str(JNIEnv* e, const std::string& s) { return e->NewStringUTF(s.c_str()); }

} // namespace

bool AndroidHttpGet(const std::string& url, std::string& out, std::string& err) {
    Jni j;
    if (!j.ok()) { err = "SIN ACCESO A ANDROID"; return false; }
    JNIEnv* e = j.env();
    jclass cUrl = e->FindClass("java/net/URL");
    jclass cConn = e->FindClass("java/net/HttpURLConnection");
    jclass cIn = e->FindClass("java/io/InputStream");
    if (!cUrl || !cConn || !cIn) { j.Threw(); err = "SIN ACCESO A LA RED"; return false; }
    jobject u = e->NewObject(cUrl, e->GetMethodID(cUrl, "<init>", "(Ljava/lang/String;)V"), Str(e, url));
    if (j.Threw() || !u) { err = "DIRECCIÓN INVÁLIDA"; return false; }
    jobject conn = e->CallObjectMethod(u, e->GetMethodID(cUrl, "openConnection", "()Ljava/net/URLConnection;"));
    if (j.Threw() || !conn) { err = "NO SE PUDO CONECTAR"; return false; }
    e->CallVoidMethod(conn, e->GetMethodID(cConn, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"), Str(e, "User-Agent"), Str(e, "BattleCityNES-Android"));
    e->CallVoidMethod(conn, e->GetMethodID(cConn, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"), Str(e, "Accept"), Str(e, "application/vnd.github+json"));
    e->CallVoidMethod(conn, e->GetMethodID(cConn, "setConnectTimeout", "(I)V"), 10000);
    e->CallVoidMethod(conn, e->GetMethodID(cConn, "setReadTimeout", "(I)V"), 10000);
    const jint code = e->CallIntMethod(conn, e->GetMethodID(cConn, "getResponseCode", "()I"));
    if (j.Threw()) { err = "SIN CONEXIÓN"; return false; }
    if (code != 200) { err = "EL SERVIDOR RESPONDIÓ " + std::to_string(code); return false; }
    jobject in = e->CallObjectMethod(conn, e->GetMethodID(cConn, "getInputStream", "()Ljava/io/InputStream;"));
    if (j.Threw() || !in) { err = "SIN RESPUESTA"; return false; }
    jbyteArray buf = e->NewByteArray(8192);
    const jmethodID read = e->GetMethodID(cIn, "read", "([B)I");
    for (;;) {
        const jint n = e->CallIntMethod(in, read, buf);
        if (j.Threw()) { err = "SE CORTÓ LA CONEXIÓN"; return false; }
        if (n < 0) break;
        jbyte tmp[8192];
        e->GetByteArrayRegion(buf, 0, n, tmp);
        out.append(reinterpret_cast<const char*>(tmp), static_cast<size_t>(n));
        if (out.size() > 8 * 1024 * 1024) { err = "RESPUESTA DEMASIADO GRANDE"; return false; }
    }
    e->CallVoidMethod(in, e->GetMethodID(cIn, "close", "()V"));
    j.Threw();
    return true;
}

// Pone en cola la descarga en el DownloadManager del sistema. Devuelve el id (o -1).
long long AndroidDownloadStart(const std::string& url, const std::string& title, std::string& err) {
    Jni j;
    if (!j.ok()) { err = "SIN ACCESO A ANDROID"; return -1; }
    JNIEnv* e = j.env();
    jclass cCtx = e->FindClass("android/content/Context");
    jclass cUri = e->FindClass("android/net/Uri");
    jclass cReq = e->FindClass("android/app/DownloadManager$Request");
    jclass cDm = e->FindClass("android/app/DownloadManager");
    if (!cCtx || !cUri || !cReq || !cDm) { j.Threw(); err = "SIN DOWNLOADMANAGER"; return -1; }
    jobject dm = e->CallObjectMethod(j.activity(), e->GetMethodID(cCtx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"), Str(e, "download"));
    if (j.Threw() || !dm) { err = "SIN DOWNLOADMANAGER"; return -1; }
    jobject uri = e->CallStaticObjectMethod(cUri, e->GetStaticMethodID(cUri, "parse", "(Ljava/lang/String;)Landroid/net/Uri;"), Str(e, url));
    jobject req = e->NewObject(cReq, e->GetMethodID(cReq, "<init>", "(Landroid/net/Uri;)V"), uri);
    if (j.Threw() || !req) { err = "DIRECCIÓN INVÁLIDA"; return -1; }
    e->CallObjectMethod(req, e->GetMethodID(cReq, "setTitle", "(Ljava/lang/CharSequence;)Landroid/app/DownloadManager$Request;"), Str(e, title));
    e->CallObjectMethod(req, e->GetMethodID(cReq, "setMimeType", "(Ljava/lang/String;)Landroid/app/DownloadManager$Request;"), Str(e, "application/vnd.android.package-archive"));
    e->CallObjectMethod(req, e->GetMethodID(cReq, "setNotificationVisibility", "(I)Landroid/app/DownloadManager$Request;"), 1);      // visible mientras baja y al terminar
    e->CallObjectMethod(req, e->GetMethodID(cReq, "setDestinationInExternalFilesDir", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Landroid/app/DownloadManager$Request;"),
                        j.activity(), static_cast<jstring>(nullptr), Str(e, "BattleCityNES-update.apk"));
    j.Threw();
    const jlong id = e->CallLongMethod(dm, e->GetMethodID(cDm, "enqueue", "(Landroid/app/DownloadManager$Request;)J"), req);
    if (j.Threw()) { err = "NO SE PUDO EMPEZAR LA DESCARGA"; return -1; }
    return static_cast<long long>(id);
}

bool AndroidDownloadPoll(long long id, int& status, long long& soFar, long long& total) {
    Jni j;
    if (!j.ok()) return false;
    JNIEnv* e = j.env();
    jclass cCtx = e->FindClass("android/content/Context");
    jclass cDm = e->FindClass("android/app/DownloadManager");
    jclass cQuery = e->FindClass("android/app/DownloadManager$Query");
    jclass cCursor = e->FindClass("android/database/Cursor");
    if (!cCtx || !cDm || !cQuery || !cCursor) { j.Threw(); return false; }
    jobject dm = e->CallObjectMethod(j.activity(), e->GetMethodID(cCtx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"), Str(e, "download"));
    if (j.Threw() || !dm) return false;
    jobject q = e->NewObject(cQuery, e->GetMethodID(cQuery, "<init>", "()V"));
    jlongArray ids = e->NewLongArray(1);
    const jlong one = static_cast<jlong>(id);
    e->SetLongArrayRegion(ids, 0, 1, &one);
    e->CallObjectMethod(q, e->GetMethodID(cQuery, "setFilterById", "([J)Landroid/app/DownloadManager$Query;"), ids);
    jobject cur = e->CallObjectMethod(dm, e->GetMethodID(cDm, "query", "(Landroid/app/DownloadManager$Query;)Landroid/database/Cursor;"), q);
    if (j.Threw() || !cur) return false;
    bool ok = false;
    if (e->CallBooleanMethod(cur, e->GetMethodID(cCursor, "moveToFirst", "()Z"))) {
        const jmethodID idx = e->GetMethodID(cCursor, "getColumnIndex", "(Ljava/lang/String;)I");
        const jmethodID getInt = e->GetMethodID(cCursor, "getInt", "(I)I");
        const jmethodID getLong = e->GetMethodID(cCursor, "getLong", "(I)J");
        status = e->CallIntMethod(cur, getInt, e->CallIntMethod(cur, idx, Str(e, "status")));
        soFar = e->CallLongMethod(cur, getLong, e->CallIntMethod(cur, idx, Str(e, "bytes_so_far")));
        total = e->CallLongMethod(cur, getLong, e->CallIntMethod(cur, idx, Str(e, "total_size")));
        ok = !j.Threw();
    }
    e->CallVoidMethod(cur, e->GetMethodID(cCursor, "close", "()V"));
    j.Threw();
    return ok;
}

// Abre la pantalla de instalacion de Android con el APK descargado.
bool AndroidInstall(long long id, std::string& err) {
    Jni j;
    if (!j.ok()) { err = "SIN ACCESO A ANDROID"; return false; }
    JNIEnv* e = j.env();
    jclass cCtx = e->FindClass("android/content/Context");
    jclass cDm = e->FindClass("android/app/DownloadManager");
    jclass cIntent = e->FindClass("android/content/Intent");
    jclass cAct = e->FindClass("android/app/Activity");
    if (!cCtx || !cDm || !cIntent || !cAct) { j.Threw(); err = "SIN ACCESO A ANDROID"; return false; }
    jobject dm = e->CallObjectMethod(j.activity(), e->GetMethodID(cCtx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"), Str(e, "download"));
    if (j.Threw() || !dm) { err = "SIN DOWNLOADMANAGER"; return false; }
    jobject uri = e->CallObjectMethod(dm, e->GetMethodID(cDm, "getUriForDownloadedFile", "(J)Landroid/net/Uri;"), static_cast<jlong>(id));
    if (j.Threw() || !uri) { err = "NO SE ENCONTRÓ EL ARCHIVO DESCARGADO"; return false; }
    jobject intent = e->NewObject(cIntent, e->GetMethodID(cIntent, "<init>", "(Ljava/lang/String;)V"), Str(e, "android.intent.action.VIEW"));
    e->CallObjectMethod(intent, e->GetMethodID(cIntent, "setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;"), uri, Str(e, "application/vnd.android.package-archive"));
    e->CallObjectMethod(intent, e->GetMethodID(cIntent, "addFlags", "(I)Landroid/content/Intent;"), 0x00000001 | 0x10000000);    // leer el archivo + nueva tarea
    e->CallVoidMethod(j.activity(), e->GetMethodID(cAct, "startActivity", "(Landroid/content/Intent;)V"), intent);
    if (j.Threw()) { err = "NO SE PUDO ABRIR LA INSTALACIÓN"; return false; }
    return true;
}

// Modo inmersivo: se esconden la barra de navegacion de Android (la de abajo, que sacaba del juego al tocarla sin querer) y la de estado. Con
// IMMERSIVE_STICKY, deslizar desde el borde las muestra un momento y se vuelven a ir solas. Se llama al abrir y cada tanto (por si el sistema las devuelve).
void AndroidEnterImmersive() {
    Jni j;
    if (!j.ok()) return;
    JNIEnv* e = j.env();
    jclass cAct = e->GetObjectClass(j.activity());
    jmethodID getWindow = e->GetMethodID(cAct, "getWindow", "()Landroid/view/Window;");
    if (!getWindow) { j.Threw(); return; }
    jobject win = e->CallObjectMethod(j.activity(), getWindow);
    if (j.Threw() || !win) return;
    jclass cWin = e->GetObjectClass(win);
    jobject decor = e->CallObjectMethod(win, e->GetMethodID(cWin, "getDecorView", "()Landroid/view/View;"));
    if (j.Threw() || !decor) return;
    jclass cView = e->GetObjectClass(decor);
    jmethodID setVis = e->GetMethodID(cView, "setSystemUiVisibility", "(I)V");
    if (!setVis) { j.Threw(); return; }
    // LAYOUT_STABLE 0x100 | LAYOUT_HIDE_NAVIGATION 0x200 | LAYOUT_FULLSCREEN 0x400 | HIDE_NAVIGATION 0x2 | FULLSCREEN 0x4 | IMMERSIVE_STICKY 0x1000
    e->CallVoidMethod(decor, setVis, static_cast<jint>(0x100 | 0x200 | 0x400 | 0x2 | 0x4 | 0x1000));
    j.Threw();
}

} // namespace app

#endif
