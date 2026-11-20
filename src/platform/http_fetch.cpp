#include "http_fetch.h"

#include <SDL3/SDL.h>

#if defined(__ANDROID__)
#include <jni.h>
#elif defined(_WIN32)
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace retro3do {

namespace {

bool is_https(const std::string& url) { return url.rfind("https://", 0) == 0; }

}  // namespace

#if defined(__ANDROID__)

namespace {
jclass g_activity = nullptr;   // global ref, taken on the main thread
}

void http_fetch_prepare() {
    if (g_activity != nullptr) return;
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) return;
    jclass local = env->FindClass("com/izzy2lost/a3do/A3DOActivity");
    if (local == nullptr) {
        env->ExceptionClear();
        return;
    }
    g_activity = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
}

std::string http_fetch(const std::string& url, const std::string& path) {
    if (!is_https(url)) return "not an https:// address";
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr || g_activity == nullptr) return "no Java environment";
    jmethodID method = env->GetStaticMethodID(
        g_activity, "nativeFetchUrl", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (method == nullptr) {
        env->ExceptionClear();
        return "download support missing";
    }
    jstring jurl = env->NewStringUTF(url.c_str());
    jstring jpath = env->NewStringUTF(path.c_str());
    jstring result = static_cast<jstring>(env->CallStaticObjectMethod(g_activity, method, jurl, jpath));
    env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(jpath);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return "download failed";
    }
    std::string out;
    if (result != nullptr) {
        const char* chars = env->GetStringUTFChars(result, nullptr);
        if (chars != nullptr) {
            out = chars;
            env->ReleaseStringUTFChars(result, chars);
        }
        env->DeleteLocalRef(result);
    }
    return out;
}

#else

void http_fetch_prepare() {}

std::string http_fetch(const std::string& url, const std::string& path) {
    if (!is_https(url)) return "not an https:// address";
    // -f: an HTTP error is a failure, not a saved error page.
    // --proto =https: redirects may not drop to plain http.
#if defined(_WIN32)
    const std::string quoted_path = "\"" + path + "\"";
    const std::string quoted_url = "\"" + url + "\"";
    const intptr_t code = _spawnlp(_P_WAIT, "curl.exe", "curl.exe", "-fsSL", "--proto", "=https",
                                   "--max-time", "120", "-o", quoted_path.c_str(),
                                   quoted_url.c_str(), nullptr);
    if (code == -1) return "curl is not installed";
    if (code != 0) return "curl exited with " + std::to_string(code);
    return {};
#else
    const char* argv[] = {"curl", "-fsSL", "--proto", "=https", "--max-time", "120",
                          "-o", path.c_str(), url.c_str(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, "curl", nullptr, nullptr, const_cast<char**>(argv), environ) != 0) {
        return "curl is not installed";
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return "curl did not finish";
    if (!WIFEXITED(status)) return "curl was stopped";
    const int code = WEXITSTATUS(status);
    if (code == 22) return "not found on the server";
    if (code == 6 || code == 7) return "no connection";
    if (code != 0) return "curl exited with " + std::to_string(code);
    return {};
#endif
}

#endif

}  // namespace retro3do
