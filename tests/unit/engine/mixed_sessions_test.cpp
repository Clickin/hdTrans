#include <array>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "hdTrans/engine.h"
#include "hdtest.h"

HD_TEST(engine_mixed_session_modes_with_reload) {
    const auto started = std::chrono::steady_clock::now();
    const char* golden = std::getenv("HDTRANS_GOLDEN");
    const char* dicts = std::getenv("HDTRANS_DICTS");
    HD_REQUIRE(golden != nullptr && *golden != 0);
    HD_REQUIRE(dicts != nullptr && *dicts != 0);
    std::ifstream input(std::string(golden) + "/corpus2.each.in");
    std::ifstream reference(std::string(golden) + "/corpus2.each.expected");
    HD_REQUIRE(input.is_open() && reference.is_open());
    std::vector<std::string> lines(64), referenceLines(64);
    for (std::size_t i = 0; i < lines.size(); i++) {
        HD_REQUIRE(static_cast<bool>(std::getline(input, lines[i])));
        HD_REQUIRE(static_cast<bool>(std::getline(reference, referenceLines[i])));
    }
    std::array<std::string, 2> documents;
    for (std::size_t i = 0; i < lines.size(); i++) {
        auto& document = documents[i / 32];
        if (!document.empty()) document += '\n';
        document += lines[i];
    }
    hdTrans::Engine engine;
    HD_REQUIRE(engine.Open({dicts}));
    std::array<std::vector<std::string>, 2> expected;
    std::array<std::array<std::string, 2>, 2> expectedDocuments;
    for (std::size_t mode = 0; mode < expected.size(); mode++) {
        for (const auto& line : lines) {
            hdTrans::Session session(engine, mode == 1);
            expected[mode].push_back(session.Translate(line));
        }
        for (std::size_t i = 0; i < documents.size(); i++) {
            hdTrans::Session session(engine, mode == 1);
            expectedDocuments[mode][i] = session.Translate(documents[i]);
            HD_CHECK(!expectedDocuments[mode][i].empty());
        }
    }
    HD_CHECK(expected[1] == referenceLines);
    HD_CHECK(expected[0] != expected[1]);
    HD_CHECK(expectedDocuments[0] != expectedDocuments[1]);
    constexpr int kThreads = 8;
    std::barrier start(kThreads + 1);
    std::array<int, kThreads> mismatches{};
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; t++) {
        workers.emplace_back([&, t] {
            start.arrive_and_wait();
            for (int round = 0; round < 2; round++) {
                const std::size_t mode = static_cast<std::size_t>((t + round) % 2);
                for (std::size_t i = 0; i < lines.size(); i++) {
                    hdTrans::Session session(engine, mode == 1);
                    if (session.Translate(lines[i]) != expected[mode][i]) mismatches[t]++;
                }
                for (std::size_t i = 0; i < documents.size(); i++) {
                    hdTrans::Session session(engine, mode == 1);
                    if (session.Translate(documents[i]) != expectedDocuments[mode][i]) mismatches[t]++;
                    session.Reset();
                    if (session.Translate(documents[i]) != expectedDocuments[mode][i]) mismatches[t]++;
                }
            }
        });
    }
    start.arrive_and_wait();
    for (int round = 0; round < 3; round++) HD_CHECK(engine.Reload());
    for (auto& worker : workers) worker.join();
    for (const int count : mismatches) HD_CHECK_EQ(count, 0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::printf("mixed session modes: %lld ms\n", static_cast<long long>(elapsed));
}
