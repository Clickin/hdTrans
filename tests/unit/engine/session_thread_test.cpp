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

// Four threads, each translating the first 1000 lines of corpus2.each one session per line,
// against the output of one thread. Skipped without the expectation set.
HD_TEST(engine_session_threads_stress) {
    const char* golden = std::getenv("HDTRANS_GOLDEN");
    const std::string dicts = DictsDir();
    if (golden == nullptr || *golden == 0 || dicts.empty()) {
        std::printf("skip engine_session_threads_stress: no HDTRANS_GOLDEN\n");
        return;
    }
    std::FILE* f = std::fopen((std::string(golden) + "/corpus2.each.in").c_str(), "rb");
    if (f == nullptr) {
        std::printf("skip engine_session_threads_stress: no corpus2.each.in\n");
        return;
    }
    std::string text;
    char buf[1 << 14];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    std::vector<std::string> lines;
    for (size_t start = 0; start < text.size() && lines.size() < 1000;) {
        const size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    hdTrans::Engine engine;
    HD_REQUIRE(engine.Open({dicts}));
    auto translateAll = [&engine, &lines] {
        std::vector<std::string> out;
        out.reserve(lines.size());
        for (const std::string& line : lines) {
            hdTrans::Session session(engine);
            out.push_back(session.Translate(line));
        }
        return out;
    };
    const std::vector<std::string> expected = translateAll();
    constexpr int kThreads = 4;
    std::vector<std::vector<std::string>> got(kThreads);
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; t++) {
        workers.emplace_back([&got, &translateAll, t] { got[static_cast<std::size_t>(t)] = translateAll(); });
    }
    for (std::thread& w : workers) w.join();
    int differing = 0;
    for (const auto& out : got) {
        for (std::size_t i = 0; i < out.size(); i++) {
            if (out[i] != expected[i]) differing++;
        }
    }
    HD_CHECK_EQ(differing, 0);
}
