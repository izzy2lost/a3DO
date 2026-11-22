// The HTTP requests rcheevos builds, sent on whatever the platform offers:
// HttpURLConnection through the activity on Android, the system's curl
// command elsewhere (as the cover downloader does).
#pragma once

#include <string>

namespace retro3do::cheevos {

struct HttpReply {
    int status = 0;        // 0: the server was never reached (rcheevos retries)
    std::string body;      // the reply, or why it failed when status is 0
};

// Call once on the main thread before http_post is used from any other
// thread. (On Android a worker thread cannot look up the app's own classes.)
void prepare_http();

// Empty when requests can be made here, otherwise why not.
std::string http_unavailable();

// Blocking. GET when `body` is empty, otherwise a POST of `body`.
HttpReply http_post(const std::string& url, const std::string& body,
                    const std::string& content_type, const std::string& user_agent);

}  // namespace retro3do::cheevos
