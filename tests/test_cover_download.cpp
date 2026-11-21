#include "app/cover_download.h"
#include "test_harness.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

using namespace retro3do;
namespace fs = std::filesystem;

TEST(cover_manifest_keeps_only_safe_image_names) {
    const auto entries = CoverDownloader::parse_manifest(
        "# a3DO cover pack\n"
        "gex.jpg\t1200\r\n"
        "nighttrap_jp.png\t99\n"
        "../evil.jpg\t10\n"
        "sub/dir.jpg\t10\n"
        ".hidden.jpg\t10\n"
        "notes.txt\t10\n"
        "zero.jpg\t0\n"
        "junk.jpg\t12abc\n"
        "no-tab.jpg 10\n");
    CHECK_EQ(entries.size(), size_t{2});
    CHECK(entries[0].name == "gex.jpg");
    CHECK_EQ(entries[0].bytes, 1200LL);
    CHECK(entries[1].name == "nighttrap_jp.png");
}

namespace {

CoverDownloader::Status wait_for(CoverDownloader& d) {
    for (int i = 0; i < 500 && !d.take_finished(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return d.status();
}

}  // namespace

TEST(cover_download_fetches_only_what_is_missing) {
    const fs::path dir = fs::temp_directory_path() / "a3do_cover_download_test";
    fs::remove_all(dir);

    // A pack of three; the server sends "bad.jpg" at the wrong size.
    const std::map<std::string, std::string> server = {
        {"covers.txt", "a.jpg\t3\nb.jpg\t4\nbad.jpg\t5\n"},
        {"a.jpg", "aaa"}, {"b.jpg", "bbbb"}, {"bad.jpg", "xx"},
    };
    int requests = 0;
    const auto fetch = [&](const std::string& url, const std::string& path) -> std::string {
        ++requests;
        const std::string base = "https://example.org/pack/";
        if (url.rfind(base, 0) != 0) return "wrong url";
        const auto found = server.find(url.substr(base.size()));
        if (found == server.end()) return "not found on the server";
        std::ofstream(path, std::ios::binary) << found->second;
        return {};
    };

    CoverDownloader d;
    CHECK(d.start("https://example.org/pack", dir.string(), fetch));
    CoverDownloader::Status s = wait_for(d);
    CHECK(!s.running);
    CHECK_EQ(s.total, 3);
    CHECK_EQ(s.fetched, 2);
    CHECK_EQ(s.failed, 1);
    CHECK(fs::file_size(dir / "a.jpg") == 3);
    CHECK(!fs::exists(dir / "bad.jpg"));
    CHECK(!fs::exists(dir / ".bad.jpg.part"));
    CHECK(!fs::exists(dir / ".covers.txt"));

    // Again: only the manifest and the one that failed are asked for.
    requests = 0;
    CHECK(d.start("https://example.org/pack/", dir.string(), fetch));
    s = wait_for(d);
    CHECK_EQ(s.fetched, 0);
    CHECK_EQ(requests, 2);
    fs::remove_all(dir);
}

TEST(cover_download_removes_covers_the_pack_renamed_but_not_the_users) {
    const fs::path dir = fs::temp_directory_path() / "a3do_cover_rename_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "mine.jpg", std::ios::binary) << "user";

    std::map<std::string, std::string> server = {
        {"covers.txt", "familyfued.jpg\t2\ngex.jpg\t3\n"},
        {"familyfued.jpg", "ff"}, {"gex.jpg", "gex"},
    };
    const auto fetch = [&](const std::string& url, const std::string& path) -> std::string {
        const auto found = server.find(url.substr(url.rfind('/') + 1));
        if (found == server.end()) return "not found on the server";
        std::ofstream(path, std::ios::binary) << found->second;
        return {};
    };

    CoverDownloader d;
    CHECK(d.start("https://example.org/pack", dir.string(), fetch));
    wait_for(d);
    CHECK(fs::exists(dir / "familyfued.jpg"));

    // The pack fixes the typo.
    server = {{"covers.txt", "familyfeud.jpg\t2\ngex.jpg\t3\n"},
              {"familyfeud.jpg", "ff"}, {"gex.jpg", "gex"}};
    CHECK(d.start("https://example.org/pack", dir.string(), fetch));
    const CoverDownloader::Status s = wait_for(d);
    CHECK_EQ(s.fetched, 1);
    CHECK(fs::exists(dir / "familyfeud.jpg"));
    CHECK(!fs::exists(dir / "familyfued.jpg"));
    CHECK(fs::exists(dir / "gex.jpg"));
    CHECK(fs::exists(dir / "mine.jpg"));
    fs::remove_all(dir);
}
