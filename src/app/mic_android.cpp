// Microfono de Android: AudioRecord por JNI (8 kHz, mono, 16 bits) en un hilo propio. Pide el permiso RECORD_AUDIO la primera vez.
#ifdef __ANDROID__

#include "voice.h"

#include <algorithm>
#include <jni.h>
#include <android_native_app_glue.h>

extern "C" struct android_app* GetAndroidApp(void);          // de raylib

namespace app {
namespace {

class AndroidMic : public MicCapture {
public:
    ~AndroidMic() override { Stop(); }
    bool Start() override {
        if (run_) return true;
        android_app* a = GetAndroidApp();
        if (!a || !a->activity) return false;
        JavaVM* vm = a->activity->vm;
        JNIEnv* e = nullptr;
        bool attached = false;
        if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&e, nullptr) != JNI_OK) return false;
            attached = true;
        }
        const bool ok = Open(e, a->activity->clazz);
        if (attached) vm->DetachCurrentThread();
        if (!ok) return false;
        run_ = true;
        th_ = std::thread([this, vm] { Loop(vm); });
        return true;
    }
    void Stop() override {
        if (!run_) return;
        run_ = false;
        if (th_.joinable()) th_.join();
        Close();
    }
    bool Running() const override { return run_; }

private:
    bool Open(JNIEnv* e, jobject activity) {
        jclass cAct = e->GetObjectClass(activity);
        jmethodID check = e->GetMethodID(cAct, "checkSelfPermission", "(Ljava/lang/String;)I");
        jstring perm = e->NewStringUTF("android.permission.RECORD_AUDIO");
        if (check && e->CallIntMethod(activity, check, perm) != 0) {           // sin permiso: lo pide (la persona responde en el cuadro del sistema)
            e->ExceptionClear();
            jmethodID req = e->GetMethodID(cAct, "requestPermissions", "([Ljava/lang/String;I)V");
            if (req && !asked_) {
                jobjectArray arr = e->NewObjectArray(1, e->FindClass("java/lang/String"), perm);
                e->CallVoidMethod(activity, req, arr, 77);
                e->ExceptionClear();
                asked_ = true;
            }
            return false;
        }
        e->ExceptionClear();
        jclass cRec = e->FindClass("android/media/AudioRecord");
        if (!cRec) { e->ExceptionClear(); return false; }
        jmethodID getMin = e->GetStaticMethodID(cRec, "getMinBufferSize", "(III)I");
        const jint minBuf = e->CallStaticIntMethod(cRec, getMin, kVoiceRate, 16, 2);          // CHANNEL_IN_MONO, ENCODING_PCM_16BIT
        if (e->ExceptionCheck()) { e->ExceptionClear(); return false; }
        const jint size = std::max<jint>(minBuf, kVoiceFrame * 2 * 8);
        jmethodID ctor = e->GetMethodID(cRec, "<init>", "(IIIII)V");
        jobject rec = e->NewObject(cRec, ctor, 1, kVoiceRate, 16, 2, size);                  // fuente MIC
        if (e->ExceptionCheck() || !rec) { e->ExceptionClear(); return false; }
        jmethodID state = e->GetMethodID(cRec, "getState", "()I");
        if (e->CallIntMethod(rec, state) != 1) { e->ExceptionClear(); return false; }       // STATE_INITIALIZED
        e->CallVoidMethod(rec, e->GetMethodID(cRec, "startRecording", "()V"));
        if (e->ExceptionCheck()) { e->ExceptionClear(); return false; }
        rec_ = e->NewGlobalRef(rec);
        return true;
    }
    void Loop(JavaVM* vm) {
        JNIEnv* e = nullptr;
        if (vm->AttachCurrentThread(&e, nullptr) != JNI_OK) { run_ = false; return; }
        jclass cRec = e->GetObjectClass(rec_);
        jmethodID read = e->GetMethodID(cRec, "read", "([SII)I");
        jshortArray arr = e->NewShortArray(kVoiceFrame);
        int16_t buf[kVoiceFrame];
        while (run_) {
            const jint n = e->CallIntMethod(rec_, read, arr, 0, kVoiceFrame);
            if (e->ExceptionCheck()) { e->ExceptionClear(); break; }
            if (n <= 0) continue;
            e->GetShortArrayRegion(arr, 0, n, buf);
            if (onFrame) onFrame(buf, n);
        }
        run_ = false;
        vm->DetachCurrentThread();
    }
    void Close() {
        android_app* a = GetAndroidApp();
        if (!a || !rec_) return;
        JavaVM* vm = a->activity->vm;
        JNIEnv* e = nullptr;
        bool attached = false;
        if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&e, nullptr) != JNI_OK) return;
            attached = true;
        }
        jclass cRec = e->GetObjectClass(rec_);
        e->CallVoidMethod(rec_, e->GetMethodID(cRec, "stop", "()V"));
        e->CallVoidMethod(rec_, e->GetMethodID(cRec, "release", "()V"));
        e->ExceptionClear();
        e->DeleteGlobalRef(rec_);
        rec_ = nullptr;
        if (attached) vm->DetachCurrentThread();
    }
    jobject rec_ = nullptr;
    bool asked_ = false;
    std::atomic<bool> run_{false};
    std::thread th_;
};

} // namespace

std::unique_ptr<MicCapture> CreateMicCapture() { return std::make_unique<AndroidMic>(); }

} // namespace app

#endif
