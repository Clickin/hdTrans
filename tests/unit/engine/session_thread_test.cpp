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
#include "hdTrans/engine/driver.h"
#include "hdTrans/engine/raw.h"
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
    hdTrans::Engine engine(true);
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
    hdTrans::Engine engine(true);
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

HD_TEST(engine_session_call_context) {
    const std::string dicts = DictsDir();
    if (dicts.empty()) return;
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    struct Case {
        std::vector<std::string> lines;
        const char* firstLinewise;
        const char* firstDocument;
        bool equal;
    };
    const Case cases[] = {
        {{"今日はいい天気ですね。", "彼は静かに頷いた。"},
         "오늘은 좋은 날씨군요.", "오늘은 좋은 날씨군요.", true},
        {{"光が来た。", "光さん、", "光さん、", "光さん、"}, "빛이 왔다.", "히카루가 왔다.", false},
        {{"お兄様！", "「あたしが行くわよ」"}, "형님!", "오빠!", false},
        {{"空を見上げて", "星を探す", "空を見上げて"}, "하늘을 올려다봐", "하늘을 올려다보고", false},
        {{"七里頼周が来た。", "七里三河守頼周が来た。"},
         "시치리뢰주가 왔다.", "나나사토 요리치카가 왔다.", false},
        {{"|竜宮レナ《りゅうぐうれな》", "|竜宮《たつみや》", "竜宮レナが来た。"},
         "류우구우 레나", "타츠미야 레나", false},
        {{"「あたしが行くわよ」", "お兄様！", "「お兄様、俺だ」"},
         "「내가 갈 거야」", "「내가 갈 거야」", false},
    };
    for (const Case& c : cases) {
        hdTrans::Session linewise(engine);
        std::string input, streamed;
        for (std::size_t i = 0; i < c.lines.size(); i++) {
            if (i > 0) {
                input += '\n';
                streamed += '\n';
            }
            input += c.lines[i];
            const std::string got = linewise.Translate(c.lines[i]);
            if (i == 0) HD_CHECK_EQ(got, c.firstLinewise);
            streamed += got;
            // Each call sees its retained history and every line of that call.
            hdTrans::Session replay(engine);
            const std::string expected = replay.Translate(input);
            const std::size_t nl = expected.find_last_of('\n');
            HD_CHECK_EQ(got, nl == std::string::npos ? expected : expected.substr(nl + 1));
        }
        hdTrans::Session document(engine);
        const std::string batch = document.Translate(input);
        HD_CHECK_EQ(batch.substr(0, batch.find('\n')), c.firstDocument);
        HD_CHECK_EQ(streamed == batch, c.equal);
    }
}

HD_TEST(engine_session_memory_window) {
    const std::string dicts = DictsDir();
    if (dicts.empty()) return;
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    hdTrans::Session session(engine);
    session.Translate("「あたしが行くわよ」");
    HD_CHECK_EQ(session.Translate("お兄様！"), "오빠!");
    session.Translate(std::string(2048, '\n'));
    HD_CHECK_EQ(session.Translate("お兄様！"), "형님!");
    session.Translate("「あたしが行くわよ」");
    HD_CHECK_EQ(session.Translate("お兄様！"), "오빠!");
    session.Reset();
    HD_CHECK_EQ(session.Translate("お兄様！"), "형님!");
}

HD_TEST(engine_reference_mode) {
    const std::string dicts = DictsDir();
    if (dicts.empty()) return;
    hdTrans::Engine improved;
    hdTrans::Engine reference(true);
    HD_REQUIRE(improved.Open({dicts}));
    HD_REQUIRE(reference.Open({dicts}));
    const std::string text = "寝たかった。";
    hdTrans::Session normal(improved), explicitReference(improved, true), inherited(reference);
    HD_CHECK_EQ(normal.Translate(text), "잠 싶었다.");
    HD_CHECK_EQ(explicitReference.Translate(text), "잠 싶었다.");
    HD_CHECK_EQ(inherited.Translate(text), "잠 싶었다.");
    explicitReference.Reset();
    inherited.Reset();
    HD_CHECK_EQ(explicitReference.Translate(text), "잠 싶었다.");
    HD_CHECK_EQ(inherited.Translate(text), "잠 싶었다.");
    HD_CHECK_EQ(hdTrans::internal::TranslateRaw(reference, text), "잠 싶었다.");
    HD_CHECK_EQ(hdTrans::internal::TranslateRaw(improved, text), "잠 싶었다.");

    const auto snap = improved.Dictionaries();
    const auto generated = hdTrans::internal::NewGenerated(*snap);
    hdTrans::internal::TranslationOptions options;
    options.reference = true;
    options.wordImprovements.desireWindow = true;
    hdTrans::internal::Memory memory;
    HD_CHECK_EQ(hdTrans::internal::TranslateDocument(*snap, *generated, {&text, 1}, memory, {}, options)[0],
                "잠 싶었다.");
    options.reference = false;
    hdTrans::internal::Memory enabled;
    HD_CHECK_EQ(hdTrans::internal::TranslateDocument(*snap, *generated, {&text, 1}, enabled, {}, options)[0],
                "자고 싶었다.");
}

namespace {

void CheckWordImprovement(bool hdTrans::word::Improvements::* flag, const std::string& text,
                          const std::string& reference, const std::string& improved) {
    const std::string dicts = DictsDir();
    if (dicts.empty()) return;
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    const auto snap = engine.Dictionaries();
    const auto generated = hdTrans::internal::NewGenerated(*snap);
    for (bool enabled : {false, true}) {
        hdTrans::internal::TranslationOptions options;
        options.wordImprovements = hdTrans::word::Improvements::Reference();
        options.wordImprovements.*flag = enabled;
        hdTrans::internal::Memory memory;
        HD_CHECK_EQ(hdTrans::internal::TranslateDocument(*snap, *generated, {&text, 1}, memory, {}, options)[0],
                    enabled ? improved : reference);
    }
}

}  // namespace

HD_TEST(engine_per_level_retries) {
    CheckWordImprovement(&hdTrans::word::Improvements::perLevelRetries, "やなぎなぎ", "이네", "야나기나기");
    CheckWordImprovement(&hdTrans::word::Improvements::perLevelRetries, "（独り言）しなきゃにゃ",
                          "(혼잣말) 하지 않으면", "(혼잣말) 인");
}

HD_TEST(engine_forward_terminal) {
    CheckWordImprovement(&hdTrans::word::Improvements::forwardTerminal, "食べたい、食べたい、食べたーい！",
                          "먹고 싶다, 먹고 싶다, 먹고 싶다!", "먹고 싶은, 먹고 싶다, 먹고 싶다!");
}

HD_TEST(engine_reference_session_builtin_isolation) {
    const std::string dicts = DictsDir();
    if (dicts.empty()) return;
    hdTrans::Engine engine;
    HD_REQUIRE(engine.Open({dicts}));
    hdTrans::Session improved(engine);
    hdTrans::Session reference(engine, true);
    HD_CHECK_EQ(improved.Translate("がぼっ！"), "커헉!");
    HD_CHECK_EQ(reference.Translate("がぼっ！"), "가봇!");
    reference.Reset();
    HD_CHECK_EQ(reference.Translate("がぼっ！"), "가봇!");
    HD_CHECK_EQ(improved.Translate("がぼっ！"), "커헉!");
}

HD_TEST(engine_reference_session_cleanup_isolation) {
    const std::string dicts = DictsDir();
    if (dicts.empty()) return;
    hdTrans::Engine engine;
    HD_REQUIRE(engine.Open({dicts}));
    hdTrans::Session improved(engine);
    hdTrans::Session reference(engine, true);
    HD_CHECK_EQ(improved.Translate("何度も言った"), "몇 번이나 말했다");
    HD_CHECK_EQ(reference.Translate("何度も言った"), "몇번이나 말했다");
    HD_CHECK_EQ(improved.Translate("몇번"), "몇번");
}

HD_TEST(engine_causative_passive) {
    const std::string dicts = DictsDir();
    if (dicts.empty()) return;
    hdTrans::Engine engine;
    HD_REQUIRE(engine.Open({dicts}));
    const struct { const char* in; const char* want; } cases[] = {
        {"手紙を書かせられた。", "편지를 쓰게 되었다."},
        {"本を読まされる。", "책을 읽게 된다."},
        {"高い壺を買わされた。", "비싼 항아리를 사게 되었다."},
        {"何本も組み合わされた柱", "몇개나 짜 맞춰진 기둥"},
        {"彼に手紙を書かせた。", "그에게 편지를 쓰게 했다."},
    };
    for (const auto& c : cases) {
        hdTrans::Session session(engine);
        HD_CHECK_EQ(session.Translate(c.in), std::string(c.want));
    }
}
