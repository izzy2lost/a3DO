#include "cheevos/cheevos_http.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace retro3do::cheevos {

#if defined(__ANDROID__)

namespace {
jclass g_activity = nullptr;   // global ref, taken on the main thread
jmethodID g_request = nullptr;
}  // namespace

void prepare_http() {
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
    g_request = env->GetStaticMethodID(
        g_activity, "nativeCheevosRequest",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)[B");
    if (g_request == nullptr) env->ExceptionClear();
}

std::string http_unavailable() {
    return g_request != nullptr ? std::string() : "the app's network bridge is missing";
}

// A3DOActivity.nativeCheevosRequest answers with the status in four
// big-endian bytes, then the body (or, for status 0, the reason).
HttpReply http_post(const std::string& url, const std::string& body,
                    const std::string& content_type, const std::string& user_agent) {
    HttpReply reply;
    // SDL attaches a native thread on first use and detaches it as it ends.
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr || g_request == nullptr) {
        reply.body = "no Java environment";
        return reply;
    }
    jstring args[4] = {env->NewStringUTF(url.c_str()), env->NewStringUTF(body.c_str()),
                       env->NewStringUTF(content_type.c_str()),
                       env->NewStringUTF(user_agent.c_str())};
    auto bytes = static_cast<jbyteArray>(env->CallStaticObjectMethod(
        g_activity, g_request, args[0], args[1], args[2], args[3]));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        bytes = nullptr;
    }
    for (jstring s : args) env->DeleteLocalRef(s);
    const jsize size = bytes != nullptr ? env->GetArrayLength(bytes) : 0;
    if (size < 4) {
        if (bytes != nullptr) env->DeleteLocalRef(bytes);
        reply.body = "the request failed";
        return reply;
    }
    std::string raw(static_cast<size_t>(size), '\0');
    env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(raw.data()));
    env->DeleteLocalRef(bytes);
    const auto* head = reinterpret_cast<const unsigned char*>(raw.data());
    reply.status = static_cast<int>((head[0] << 24) | (head[1] << 16) | (head[2] << 8) | head[3]);
    reply.body = raw.substr(4);
    return reply;
}

#else

namespace {

// Single-quoted for the shell. Windows' cmd has no single quotes, but
// nothing passed here contains a double quote, so double quotes do there.
std::string shell_quote(const std::string& text) {
#if defined(_WIN32)
    return "\"" + text + "\"";
#else
    std::string out = "'";
    for (const char c : text) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
#endif
}

#if defined(_WIN32)
#define A3DO_POPEN _popen
#define A3DO_PCLOSE _pclose
#else
#define A3DO_POPEN popen
#define A3DO_PCLOSE pclose
#endif

}  // namespace

void prepare_http() {}

std::string http_unavailable() { return {}; }

// The body goes in a file curl reads, so it never appears on a command line
// another user could see; curl writes the reply and then, on a line of its
// own, the status code.
HttpReply http_post(const std::string& url, const std::string& body,
                    const std::string& content_type, const std::string& user_agent) {
    HttpReply reply;
    if (url.rfind("https://", 0) != 0) {
        reply.body = "not an https:// address";
        return reply;
    }
    std::string body_file;
    if (!body.empty()) {
        std::error_code error;
        static std::mt19937_64 random{std::random_device{}()};
        body_file = (std::filesystem::temp_directory_path(error) /
                     ("a3do-cheevos-" + std::to_string(random()) + ".txt"))
                        .string();
        std::FILE* f = std::fopen(body_file.c_str(), "wb");
        if (f == nullptr) {
            reply.body = "could not stage the request";
            return reply;
        }
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    }
    std::string command = "curl -sS --proto =https --max-time 30 -A " + shell_quote(user_agent) +
                          " -w \"\\n%{http_code}\"";
    if (!body.empty()) {
        command += " -H " + shell_quote("Content-Type: " + (content_type.empty()
                                                           ? std::string("application/x-www-form-urlencoded")
                                                           : content_type));
        command += " --data-binary @" + shell_quote(body_file);
    }
    command += " " + shell_quote(url) + " 2>&1";

    std::string out;
    if (std::FILE* pipe = A3DO_POPEN(command.c_str(), "r")) {
        char chunk[4096];
        size_t n = 0;
        while ((n = std::fread(chunk, 1, sizeof chunk, pipe)) > 0) out.append(chunk, n);
        A3DO_PCLOSE(pipe);
    } else {
        out.clear();
    }
    if (!body_file.empty()) std::remove(body_file.c_str());

    const size_t line = out.find_last_of('\n');
    const std::string code = line == std::string::npos ? out : out.substr(line + 1);
    const int status = std::atoi(code.c_str());
    if (line == std::string::npos || status <= 0) {
        reply.body = out.empty() ? "curl is not installed"
                                 : (line == std::string::npos ? out : out.substr(0, line));
        while (!reply.body.empty() && (reply.body.back() == '\n' || reply.body.back() == '\r'))
            reply.body.pop_back();
        return reply;
    }
    reply.status = status;
    reply.body = out.substr(0, line);
    return reply;
}

#endif

}  // namespace retro3do::cheevos
