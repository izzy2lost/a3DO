// Fetch one HTTPS URL into a file, on whatever the platform offers.
//
// Android goes through HttpURLConnection in the activity; the desktop runs
// the system's curl, which ships with Linux, macOS and Windows 10 and later.
// That keeps a TLS stack out of the native build for what is a handful of
// downloads.
#pragma once

#include <string>

namespace retro3do {

// Call once on the main thread before http_fetch is used from any other
// thread. (On Android a worker thread cannot look up the app's own classes.)
void http_fetch_prepare();

// Blocking. Returns "" on success or a short reason. Only https:// URLs.
std::string http_fetch(const std::string& url, const std::string& path);

}  // namespace retro3do
