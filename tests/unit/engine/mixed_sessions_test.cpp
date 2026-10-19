#include <array>
#include <atomic>
#include <barrier>
#include <filesystem>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "core/engine.h"
#include "hdtest.h"

HD_TEST(engine_mixed_session_modes_with_reload) {
    const auto folder = std::filesystem::path(__FILE__).parent_path().parent_path() /
                        "dict" / "testdata" / "folder";
    constexpr std::size_t kLines = 128, kDocuments = 8;
    constexpr int kThreads = 8, kReloads = 3;
    const std::string samples[] = {
        "がぼっ！", "学生", "「あたし」", "お兄様！", "123, 456.", "ASCII", "", "学生\r",
    };
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < kLines; i++) lines.push_back(samples[i % std::size(samples)]);
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({folder.string()}));
    std::array<std::vector<std::string>, 2> expected;
    for (std::size_t mode = 0; mode < expected.size(); mode++) {
        for (const auto& line : lines) {
            hdTrans::Session session(engine, mode == 1);
            expected[mode].push_back(session.Translate(line));
        }
    }
    constexpr std::size_t kAnchor = 0;
    HD_CHECK_EQ(expected[0][kAnchor], "커헉!");
    HD_CHECK_EQ(expected[1][kAnchor], "가봇!");
    HD_CHECK_EQ(expected[0][1], "학생님");
    HD_CHECK_EQ(expected[1][1], "학생님");
    std::array<std::string, kDocuments> documents;
    for (std::size_t i = 0; i < documents.size(); i++) {
        documents[i] = lines[kAnchor];
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
        HD_CHECK(expectedDocuments[0][i].starts_with("커헉!\n"));
        HD_CHECK(expectedDocuments[1][i].starts_with("가봇!\n"));
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
                check(session, lines[kAnchor], expected[mode][kAnchor]);
            }
            running--;
        });
    }
    start.arrive_and_wait();
    for (int round = 0; round < kReloads; round++) {
        while (completed.load() == 0 || inFlight.load() == 0) std::this_thread::yield();
        HD_CHECK_EQ(running.load(), kThreads);
        HD_CHECK(engine.Reload());
        HD_CHECK_EQ(running.load(), kThreads);
        reloads++;
    }
    for (auto& worker : workers) worker.join();
    for (int t = 0; t < kThreads; t++) {
        HD_CHECK_EQ(mismatches[t], 0);
        HD_CHECK_EQ(observedReloads[t], kReloads);
    }
}
