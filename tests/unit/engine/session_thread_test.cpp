// Sessions of one Engine translating on several threads at once, with a Reload while they
// translate. Every thread must produce the same output for the same text, and the output
// must match a single-threaded run.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "hdTrans/engine.h"
#include "hdTrans/dict/snapshot_file.h"
#include "hdTrans/engine/driver.h"
#include "hdTrans/engine/raw.h"
#include "hdTrans/text/lines.h"
#include "hdtest.h"
#include "golden.h"
#include "tokenizer/stage_records.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define HDTRANS_TEST_INSTRUMENTED 1
#endif
#endif

namespace {

using hdtest::DictsDir;

struct SessionCacheVariable {
#ifdef _WIN32
    std::wstring name, previous;
#else
    std::string name, previous;
#endif
    bool present = false;

    SessionCacheVariable(std::string_view key, const std::filesystem::path* value) : name(key.begin(), key.end()) {
#ifdef _WIN32
        SetLastError(ERROR_SUCCESS);
        const DWORD size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
        present = size != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        if (size != 0) {
            previous.resize(size);
            previous.resize(GetEnvironmentVariableW(name.c_str(), previous.data(), size));
        }
        HD_CHECK(SetEnvironmentVariableW(name.c_str(), value ? value->c_str() : nullptr) != 0 ||
                 (!value && GetLastError() == ERROR_ENVVAR_NOT_FOUND));
#else
        const char* old = std::getenv(name.c_str());
        present = old != nullptr;
        if (old != nullptr) previous = old;
        HD_CHECK((value ? setenv(name.c_str(), value->c_str(), 1) : unsetenv(name.c_str())) == 0);
#endif
    }

    ~SessionCacheVariable() {
#ifdef _WIN32
        HD_CHECK(SetEnvironmentVariableW(name.c_str(), present ? previous.c_str() : nullptr) != 0 ||
                 (!present && GetLastError() == ERROR_ENVVAR_NOT_FOUND));
#else
        HD_CHECK((present ? setenv(name.c_str(), previous.c_str(), 1) : unsetenv(name.c_str())) == 0);
#endif
    }
};

std::vector<std::string> RebuiltTranslation(const hdTrans::dict::Snapshot& snap,
                                          hdTrans::internal::Generated& generated,
                                          const std::vector<std::string>& retained,
                                          const std::string& line,
                                          const hdTrans::internal::TranslationOptions& options) {
    hdTrans::internal::Memory memory;
    hdTrans::pipeline::RememberDocument(snap, retained, memory.document);
    return hdTrans::internal::TranslateDocument(snap, generated, {&line, 1}, memory, {}, options);
}

}  // namespace

HD_TEST(engine_session_threads) {
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const std::string dicts = DictsDir();
    std::error_code ec;
    HD_REQUIRE(!dicts.empty());
    HD_REQUIRE(std::filesystem::exists(dicts, ec));
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    const std::string text = "今日はいい天気ですね。\n「ごめんね、待った？」\n彼は静かに頷いた。\n";

    std::string expected;
    {
        hdTrans::Session session(engine);
        expected = session.Translate(text);
    }
    HD_CHECK_EQ(expected, std::string("오늘은 좋은 날씨군요.\n「미안해요, 기다렸어?」\n그는 조용하게 끄덕였다.\n"));

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
// against the output of one thread.
HD_TEST(engine_session_threads_stress) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const char* golden = std::getenv("HDTRANS_GOLDEN");
    const std::string dicts = DictsDir();
    HD_REQUIRE(golden != nullptr && *golden != 0);
    HD_REQUIRE(!dicts.empty());
    std::FILE* f = std::fopen((std::string(golden) + "/corpus2.each.in").c_str(), "rb");
    HD_REQUIRE(f != nullptr);
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
    HD_REQUIRE(lines.size() == 1000);
    std::string frozen;
    HD_REQUIRE(stagetest::ReadFile(std::string(golden) + "/corpus2.each.expected", frozen));
    const auto frozenLines = stagetest::Lines(frozen);
    HD_REQUIRE(frozenLines.size() >= lines.size());
    const std::vector<std::string> expected = translateAll();
    HD_REQUIRE(expected.size() == lines.size());
    for (size_t i = 0; i < expected.size(); ++i) HD_CHECK_EQ(expected[i], std::string(frozenLines[i]));
    constexpr int kThreads = 4;
    std::vector<std::vector<std::string>> got(kThreads);
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; t++) {
        workers.emplace_back([&got, &translateAll, t] { got[static_cast<std::size_t>(t)] = translateAll(); });
    }
    for (std::thread& w : workers) w.join();
    int differing = 0;
    for (const auto& out : got) {
        HD_REQUIRE(out.size() == expected.size());
        for (std::size_t i = 0; i < out.size(); i++) {
            if (out[i] != expected[i]) differing++;
        }
    }
    HD_CHECK_EQ(differing, 0);
}

HD_TEST(engine_session_call_context) {
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
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
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
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
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
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
    HD_REQUIRE(!dicts.empty());
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
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    CheckWordImprovement(&hdTrans::word::Improvements::perLevelRetries, "やなぎなぎ", "이네", "야나기나기");
    CheckWordImprovement(&hdTrans::word::Improvements::perLevelRetries, "（独り言）しなきゃにゃ",
                          "(혼잣말) 하지 않으면", "(혼잣말) 인");
}

HD_TEST(engine_forward_terminal) {
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    CheckWordImprovement(&hdTrans::word::Improvements::forwardTerminal, "食べたい、食べたい、食べたーい！",
                          "먹고 싶다, 먹고 싶다, 먹고 싶다!", "먹고 싶은, 먹고 싶다, 먹고 싶다!");
}

HD_TEST(engine_reference_session_builtin_isolation) {
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
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
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine;
    HD_REQUIRE(engine.Open({dicts}));
    hdTrans::Session improved(engine);
    hdTrans::Session reference(engine, true);
    HD_CHECK_EQ(improved.Translate("何度も言った"), "몇 번이나 말했다");
    HD_CHECK_EQ(reference.Translate("何度も言った"), "몇번이나 말했다");
    HD_CHECK_EQ(improved.Translate("몇번"), "몇번");
}

HD_TEST(engine_causative_passive) {
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
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

HD_TEST(engine_session_incremental_history) {
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    hdTrans::Session session(engine);
    session.Translate("光さん、\n光さん、\n光さん、");
    HD_CHECK_EQ(session.Translate("光が来た。"), "히카루가 왔다.");
    session.Reset();
    HD_CHECK_EQ(session.Translate("光が来た。"), "빛이 왔다.");

    const char* golden = std::getenv("HDTRANS_GOLDEN");
    HD_REQUIRE(golden != nullptr && *golden != 0);
    std::ifstream input(std::string(golden) + "/corpus2.each.in", std::ios::binary);
    HD_REQUIRE(input.good());
    std::string history, line;
    for (int i = 0; i < 2048; i++) {
        HD_REQUIRE(static_cast<bool>(std::getline(input, line)));
        if (i > 0) history += '\n';
        history += line;
    }
    session.Reset();
    session.Translate(history);
    std::vector<double> times;
    for (int i = 0; i < 20; i++) {
        const auto start = std::chrono::steady_clock::now();
        HD_CHECK_EQ(session.Translate("今日はいい天気ですね。"), "오늘은 좋은 날씨군요.");
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(times.begin(), times.end());
    std::printf("session after 2048 lines: median %.3f ms, max %.3f ms\n", times[times.size() / 2], times.back());
#if defined(NDEBUG) && !defined(HDTRANS_TEST_INSTRUMENTED) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    HD_CHECK(times[times.size() / 2] < 20.0);
#else
    std::printf("session timing assertion inactive: Debug or sanitizer build\n");
#endif
}

HD_TEST(engine_session_memory_bytes) {
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    const auto snap = engine.Dictionaries();
    std::shared_ptr<hdTrans::pipeline::DocumentHistory> history;
    const std::vector<std::string> cue{"「あたしが行くわよ」"};
    const std::vector<std::string> probe{"お兄様！"};
    hdTrans::pipeline::RememberDocument(*snap, cue, history);
    HD_CHECK(hdTrans::pipeline::BuildDocument(*snap, probe, false, &history)->female);
    const std::vector<std::string> padding(33, std::string(32768, ' '));
    hdTrans::pipeline::RememberDocument(*snap, padding, history);
    HD_CHECK(!hdTrans::pipeline::BuildDocument(*snap, probe, false, &history)->female);
    auto document = padding;
    document.insert(document.begin(), cue.front());
    HD_CHECK(hdTrans::pipeline::BuildDocument(*snap, document)->female);
}

HD_TEST(engine_megabyte_line) {
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    std::string line;
    while (line.size() < 1024 * 1024) line += "あいうえ。 ";
    std::string expected;
    for (const std::string_view piece : hdTrans::text::TranslationPieces(line)) {
        hdTrans::Session fresh(engine);
        expected += fresh.Translate(piece);
    }
    const auto start = std::chrono::steady_clock::now();
    hdTrans::Session session(engine);
    HD_CHECK_EQ(session.Translate(line), expected);
    std::printf("megabyte line: %.3f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    const std::string ascii(1024 * 1024, 'a');
    HD_CHECK_EQ(hdTrans::internal::TranslateRaw(engine, ascii), ascii);
    std::string symbols;
    while (symbols.size() < 100000) symbols += "0123456789., ";
    const std::string translated = session.Translate(symbols);
    for (char c : std::string_view("0123456789.,")) {
        HD_CHECK_EQ(std::count(translated.begin(), translated.end(), c), std::count(symbols.begin(), symbols.end(), c));
    }
}

HD_TEST(engine_session_history_rebuild) {
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
    const char* golden = std::getenv("HDTRANS_GOLDEN");
    HD_REQUIRE(golden != nullptr && *golden != 0);
    std::ifstream input(std::string(golden) + "/corpus2.each.in", std::ios::binary);
    HD_REQUIRE(input.good());
    std::vector<std::string> corpus;
    for (int i = 0; i < 32; i++) {
        std::string line;
        HD_REQUIRE(static_cast<bool>(std::getline(input, line)));
        corpus.push_back(std::move(line));
    }
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    const auto snap = engine.Dictionaries();
    const auto generated = hdTrans::internal::NewGenerated(*snap);
    const std::vector<std::string> prefix = {
        "竜宮（りゅう）", "竜宮（・・）", "竜宮（たつみや）", "光さん、",
        "|風音《かざね》", "|風音《かのん》", "「お兄様、俺だ」", "「あたしが行くわよ」",
        "お兄様！", "空を見上げて", "星を探す", "七里三河守頼周が来た。", "七里頼周が来た。"};
    for (bool reference : {true, false}) {
        hdTrans::internal::TranslationOptions options;
        options.reference = reference;
        hdTrans::internal::Memory incremental;
        std::vector<std::string> retained;
        std::size_t bytes = 0;
        for (std::size_t call = 0; call < 2160; call++) {
            std::string line;
            if (call < prefix.size()) line = prefix[call];
            else if (call == 2049) line = "竜宮が来た。";
            else if (call % 41 == 0) line = "お兄様！";
            else if (call % 43 == 0) line = "空を見上げて";
            else if (call % 47 == 0) line = "七里頼周が来た。";
            else if (call % 53 == 0) line = "光さん、";
            else line = corpus[(call * 17) % corpus.size()];
            const auto expected = RebuiltTranslation(*snap, *generated, retained, line, options);
            const auto got = hdTrans::internal::TranslateDocument(*snap, *generated, {&line, 1}, incremental, {}, options);
            HD_REQUIRE(got.size() == 1 && expected.size() == 1);
            if (got != expected) {
                hdtest::Fail(__FILE__, __LINE__, "history call " + std::to_string(call) + ": " + got[0] + " != " + expected[0]);
                return;
            }
            if (call == 2049) HD_CHECK_EQ(got[0], "타츠미야가 왔다.");
            retained.push_back(line);
            bytes += line.size();
            while (retained.size() > 2048 || bytes > 1024 * 1024) {
                bytes -= retained.front().size();
                retained.erase(retained.begin());
            }
        }
    }
}

HD_TEST(engine_session_piece_history) {
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    const auto snap = engine.Dictionaries();
    const auto generated = hdTrans::internal::NewGenerated(*snap);
    hdTrans::internal::TranslationOptions options;
    options.reference = true;
    hdTrans::internal::Memory memory;
    std::vector<std::string> prefix(2047);
    prefix.front() = "「あたしが行くわよ」";
    hdTrans::pipeline::RememberDocument(*snap, prefix, memory.document);
    const std::string line = std::string(16381, 'a') + "て" + std::string(65536, 'a') + "。";
    HD_REQUIRE(hdTrans::internal::TranslateDocument(*snap, *generated, {&line, 1}, memory, {}, options).size() == 1);
    const auto document = hdTrans::pipeline::BuildDocument(*snap, {}, false, &memory.document);
    HD_CHECK(document->female);
    HD_CHECK(document->verse.empty());
    const std::string probe = "お兄様！";
    HD_CHECK_EQ(hdTrans::internal::TranslateDocument(*snap, *generated, {&probe, 1}, memory, {}, options)[0], "오빠!");
    HD_CHECK_EQ(hdTrans::internal::TranslateDocument(*snap, *generated, {&probe, 1}, memory, {}, options)[0], "형님!");
}

HD_TEST(engine_quoted_pieces) {
    const std::string dicts = DictsDir();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open({dicts}));
    std::string line;
    const std::string unit = "「今日は天気です。明日も晴れるでしょう。」と彼は言った。";
    while (line.size() < 70000) line += unit;
    std::string expected;
    for (const auto piece : hdTrans::text::TranslationPieces(line)) {
        HD_CHECK_EQ(piece.size() % unit.size(), std::size_t{0});
        hdTrans::Session fresh(engine);
        expected += fresh.Translate(piece);
    }
    hdTrans::Session session(engine);
    HD_CHECK_EQ(session.Translate(line), expected);
}

HD_TEST(engine_session_fallback_history) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path temporary = fs::temp_directory_path(ec);
    HD_REQUIRE(!ec);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{temporary / ("hdtrans-session-fallback-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    HD_REQUIRE(fs::create_directory(cleanup.path, ec) && !ec);
    const SessionCacheVariable cache("HDTRANS_CACHE_DIR", &cleanup.path);
    const SessionCacheVariable enabled("HDTRANS_SNAPSHOT", nullptr);
    const SessionCacheVariable lazy("HDTRANS_VERIFY_DICTS", nullptr);
    const auto pathText = [](const fs::path& path) {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    };
    const std::vector<std::string> folders{pathText(fs::path(__FILE__).parent_path().parent_path() /
                                                 "dict" / "testdata" / "folder")};
    hdTrans::Engine textEngine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(textEngine.Open(folders));
    HD_REQUIRE(!textEngine.Dictionaries()->Mapped());
    hdTrans::Session text(textEngine);
    const std::string cue = std::string(2046, '\n') + "「あたし」";
    const std::string probe = "学生 お兄様！";
    text.Translate(cue);
    const std::string expected = text.Translate(probe);
    HD_CHECK_EQ(expected, "학생님 오빠!");

    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open(folders));
    HD_REQUIRE(engine.Open(folders));
    auto snapshot = engine.Dictionaries();
    HD_REQUIRE(snapshot->Mapped());
    hdTrans::Session session(engine);
    session.Translate(cue);
    HD_REQUIRE(snapshot->Healthy());

    const auto index = hdTrans::dict::internal::SnapshotPath(folders, pathText(cleanup.path));
    const std::string resource = hdTrans::dict::internal::SnapshotResourcePath(index);
    HD_REQUIRE(!resource.empty());
    const fs::path file(std::u8string(reinterpret_cast<const char8_t*>(resource.data()), resource.size()));
    std::ifstream input(file, std::ios::binary);
    HD_REQUIRE(input.good());
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    HD_REQUIRE(bytes.size() >= 256);
    const auto field = [&](std::size_t at, std::size_t width) {
        uint64_t value = 0;
        for (std::size_t i = 0; i < width; i++) {
            value |= static_cast<uint64_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
        }
        return value;
    };
    const uint64_t sections = field(40, 4);
    HD_REQUIRE(sections <= (bytes.size() - 256) / 64);
    std::size_t offset = 0;
    for (std::size_t i = 0; i < sections; i++) {
        const std::size_t at = 256 + 64 * i;
        // The base layer's byte column is checked when a matching record is read.
        if (field(at, 4) == 17 && field(at + 4, 4) == 0) {
            offset = static_cast<std::size_t>(field(at + 8, 8));
        }
    }
    HD_REQUIRE(offset != 0 && offset < bytes.size());
    std::fstream edit(file, std::ios::in | std::ios::out | std::ios::binary);
    HD_REQUIRE(edit.good());
    edit.seekp(static_cast<std::streamoff>(offset));
    edit.put(static_cast<char>(static_cast<unsigned char>(bytes[offset]) ^ 1u));
    edit.close();
    HD_REQUIRE(edit.good());
    HD_CHECK_EQ(session.Translate(probe), expected);
    HD_CHECK(!snapshot->Healthy());
    const std::weak_ptr<const hdTrans::dict::Snapshot> cachedFallback = snapshot->Fallback();
    for (int call = 0; call < 3; call++) {
        const auto cached = cachedFallback.lock();
        HD_REQUIRE(cached != nullptr);
        HD_CHECK(cached == snapshot->Fallback());
        auto start = std::chrono::steady_clock::now();
        const std::string wanted = text.Translate("お兄様！");
        const double normalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        start = std::chrono::steady_clock::now();
        HD_CHECK_EQ(session.Translate("お兄様！"), wanted);
        const double fallbackMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("session fallback call %d: %.3f ms, text %.3f ms\n", call + 1, fallbackMs, normalMs);
#if defined(NDEBUG) && !defined(HDTRANS_TEST_INSTRUMENTED) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
        HD_CHECK(fallbackMs < std::max(20.0, normalMs * 20.0));
#endif
    }
#if !defined(NDEBUG) || defined(HDTRANS_TEST_INSTRUMENTED) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    std::printf("fallback timing assertion inactive: Debug or sanitizer build\n");
#endif
    HD_REQUIRE(engine.Reload());
    snapshot.reset();
    session.Reset();
    hdTrans::Session fresh(textEngine);
    HD_CHECK_EQ(session.Translate("お兄様！"), fresh.Translate("お兄様！"));
}
