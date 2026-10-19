#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "core/engine.h"
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
    constexpr std::size_t kLines = 128, kDocuments = 8;
    constexpr int kThreads = 8, kReloads = 3;
    std::vector<std::string> lines(kLines), referenceLines(kLines);
    for (std::size_t i = 0; i < lines.size(); i++) {
        HD_REQUIRE(static_cast<bool>(std::getline(input, lines[i])));
        HD_REQUIRE(static_cast<bool>(std::getline(reference, referenceLines[i])));
    }
    hdTrans::Engine engine;
    HD_REQUIRE(engine.Open({dicts}));
    std::array<std::vector<std::string>, 2> expected;
    for (std::size_t mode = 0; mode < expected.size(); mode++) {
        for (const auto& line : lines) {
            hdTrans::Session session(engine, mode == 1);
            expected[mode].push_back(session.Translate(line));
        }
    }
    HD_CHECK(expected[1] == referenceLines);
    std::size_t anchor = 0;
    while (anchor < lines.size() && expected[0][anchor] == expected[1][anchor]) anchor++;
    HD_REQUIRE(anchor < lines.size());
    std::array<std::string, kDocuments> documents;
    for (std::size_t i = 0; i < documents.size(); i++) {
        documents[i] = lines[anchor];
        for (std::size_t j = i * kLines / kDocuments; j < (i + 1) * kLines / kDocuments; j++) {
            documents[i] += '\n';
            documents[i] += lines[j];
        }
    }
    std::array<std::array<std::string, kDocuments>, 2> expectedDocuments;
    for (std::size_t mode = 0; mode < expected.size(); mode++) {
        for (std::size_t i = 0; i < documents.size(); i++) {
            hdTrans::Session session(engine, mode == 1);
            expectedDocuments[mode][i] = session.Translate(documents[i]);
            HD_CHECK(!expectedDocuments[mode][i].empty());
        }
    }
    for (std::size_t i = 0; i < documents.size(); i++) {
        HD_CHECK(expectedDocuments[0][i] != expectedDocuments[1][i]);
    }
    std::barrier start(kThreads + 1);
    std::atomic<int> running{0}, inFlight{0}, completed{0}, reloads{0};
    std::array<int, kThreads> mismatches{}, observedReloads{};
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; t++) {
        workers.emplace_back([&, t] {
            running++;
            start.arrive_and_wait();
            auto check = [&](hdTrans::Session& session, const std::string& text, const std::string& want) {
                inFlight++;
                if (session.Translate(text) != want) mismatches[t]++;
                observedReloads[t] = reloads.load();
                completed++;
                inFlight--;
            };
            for (int round = 0; round < 2; round++) {
                const std::size_t mode = static_cast<std::size_t>((t + round) % 2);
                for (std::size_t i = 0; i < lines.size(); i++) {
                    hdTrans::Session session(engine, mode == 1);
                    check(session, lines[i], expected[mode][i]);
                }
                for (std::size_t i = 0; i < documents.size(); i++) {
                    hdTrans::Session session(engine, mode == 1);
                    check(session, documents[i], expectedDocuments[mode][i]);
                    session.Reset();
                    check(session, documents[i], expectedDocuments[mode][i]);
                }
            }
            while (observedReloads[t] < kReloads) {
                const std::size_t mode = static_cast<std::size_t>(t % 2);
                hdTrans::Session session(engine, mode == 1);
                check(session, lines[anchor], expected[mode][anchor]);
            }
            running--;
        });
    }
    start.arrive_and_wait();
    int overlaps = 0;
    for (int round = 0; round < kReloads; round++) {
        while (completed.load() == 0 || inFlight.load() == 0) std::this_thread::yield();
        HD_CHECK_EQ(running.load(), kThreads);
        const int before = completed.load();
        HD_CHECK(engine.Reload());
        if (completed.load() > before) overlaps++;
        HD_CHECK_EQ(running.load(), kThreads);
        reloads++;
    }
    for (auto& worker : workers) worker.join();
    HD_CHECK(overlaps > 0);
    for (int t = 0; t < kThreads; t++) {
        HD_CHECK_EQ(mismatches[t], 0);
        HD_CHECK_EQ(observedReloads[t], kReloads);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::printf("mixed session modes: %lld ms; documents=%zu reload-overlaps=%d\n",
                static_cast<long long>(elapsed), documents.size(), overlaps);
}
