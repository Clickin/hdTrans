// Sessions of one Engine translating on several threads at once, with a Reload while they
// translate. Every thread must produce the same output for the same text, and the output
// must match a single-threaded run. Skipped without dictionaries.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "hdTrans/engine.h"
#include "hdtest.h"

namespace {

std::string DictsDir() {
    if (const char* d = std::getenv("HDTRANS_DICTS"); d != nullptr && *d != 0) return d;
    if (const char* g = std::getenv("HDTRANS_GOLDEN"); g != nullptr && *g != 0) return std::string(g) + "/dicts/base";
    return {};
}

}  // namespace

HD_TEST(engine_session_threads) {
    const std::string dicts = DictsDir();
    std::error_code ec;
    if (dicts.empty() || !std::filesystem::exists(dicts, ec)) {
        std::printf("skip engine_session_threads: no dictionaries (HDTRANS_DICTS)\n");
        return;
    }
    hdTrans::Engine engine;
    HD_REQUIRE(engine.Open({dicts}));
    const std::string text = "今日はいい天気ですね。\n「ごめんね、待った？」\n彼は静かに頷いた。\n";

    std::string expected;
    {
        hdTrans::Session session(engine);
        expected = session.Translate(text);
    }
    HD_CHECK(!expected.empty());

    constexpr int kThreads = 4;
    constexpr int kRounds = 3;
    std::vector<std::string> results(kThreads * kRounds);
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; t++) {
        workers.emplace_back([&engine, &text, &results, t] {
            for (int r = 0; r < kRounds; r++) {
                hdTrans::Session session(engine);
                results[static_cast<std::size_t>(t * kRounds + r)] = session.Translate(text);
            }
        });
    }
    // Reloads swap the dictionaries under the sessions; each session keeps its own.
    workers.emplace_back([&engine] {
        for (int r = 0; r < 2; r++) engine.Reload();
    });
    for (std::thread& w : workers) w.join();

    int differing = 0;
    for (const std::string& got : results) {
        if (got != expected) differing++;
    }
    HD_CHECK_EQ(differing, 0);
}
