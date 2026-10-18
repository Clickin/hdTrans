#include "hdTrans/dict/trie.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "hdTrans/dict/tsv.h"
#include "hdtest.h"

using hdTrans::dict::Trie;

// Counts the allocations of the test program, to check that lookups make none.
namespace {
size_t allocations = 0;
}  // namespace

void* operator new(size_t size) {
    allocations++;
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

namespace {

// A dictionary of keys with one value each, as the trie and the value of each key
// number.
struct Dict {
    Trie trie;
    std::vector<std::string> keys;    // sorted
    std::vector<std::string> values;  // by key number
};

Dict Build(std::vector<std::pair<std::string, std::string>> entries) {
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Dict d;
    std::vector<std::string> values;
    for (const auto& e : entries) {
        if (!d.keys.empty() && d.keys.back() == e.first) continue;  // the first value of a key
        d.keys.push_back(e.first);
        values.push_back(e.second);
    }
    std::vector<std::string_view> views(d.keys.begin(), d.keys.end());
    std::vector<int32_t> order;
    d.trie = Trie::Build(views, &order);
    d.values.resize(order.size());
    for (size_t k = 0; k < order.size(); k++) d.values[k] = values[static_cast<size_t>(order[k])];
    return d;
}

std::string Quote(std::string_view s) { return "\"" + std::string(s) + "\""; }

// The keys in Each order with their values, then for each query the keys that
// prefix it as length:value.
std::string Dump(const Dict& d, const std::vector<std::string>& queries) {
    std::vector<std::string> out;
    d.trie.Each([&](std::string_view key, int k) { out.push_back(Quote(key) + "=" + d.values[static_cast<size_t>(k)]); });
    for (const auto& q : queries) {
        std::string ms;
        d.trie.Prefixes(q, [&](int k, size_t n) {
            if (!ms.empty()) ms += ",";
            ms += std::to_string(n) + ":" + d.values[static_cast<size_t>(k)];
        });
        out.push_back(Quote(q) + ">" + ms);
    }
    std::string s;
    for (const auto& o : out) s += (s.empty() ? "" : " ") + o;
    return s;
}

// splitmix64
struct Rng {
    uint64_t s;
    uint64_t Next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
        return z ^ (z >> 31);
    }
    size_t N(size_t n) { return static_cast<size_t>(Next() % n); }
};

bool RuneStart(char b) { return (static_cast<unsigned char>(b) & 0xC0) != 0x80; }

// Every key of keys that is a prefix of q ending at a code point boundary of q, as
// (length, key), shortest first.
std::vector<std::pair<size_t, std::string>> BrutePrefixes(const std::map<std::string, int>& keys, const std::string& q) {
    std::vector<std::pair<size_t, std::string>> out;
    for (size_t n = 0; n <= q.size(); n++) {
        if (n < q.size() && !RuneStart(q[n])) continue;
        if (keys.count(q.substr(0, n))) out.push_back({n, q.substr(0, n)});
    }
    return out;
}

std::string SourceDir() {
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return slash == std::string::npos ? "." : f.substr(0, slash);
}

}  // namespace

HD_TEST(trie_dict_cases) {
    struct Case {
        const char* name;
        std::vector<std::pair<std::string, std::string>> entries;
        std::vector<std::string> queries;
        std::string want;
    };
    const Case cases[] = {
        {"empty", {}, {"", "a"}, R"(""> "a">)"},
        {"empty key", {{"", "e"}, {"a", "a"}}, {"ab"}, R"(""=e "a"=a "ab">0:e,1:a)"},
        {"first value of a key",
         {{"学生", "학생"}, {"学", "학"}, {"学生", "배우"}},
         {"学生です", "学", "生"},
         R"("学"=학 "学生"=학생 "学生です">3:학,6:학생 "学">3:학 "生">)"},
        {"code-point order across surrogates",
         {{"\U00020BB7野家", "요시노야"}, {"\U00020BB7", "길"}, {"\uFF5E", "물결"}, {"\uE000", "사용"},
          {"\U00010000", "선형"}, {"\uD7FF", "끝"}},
         {"\U00020BB7野家だ", "\U00020BB8", "\uFF5E"},
         "\"\uD7FF\"=끝 \"\uE000\"=사용 \"\uFF5E\"=물결 \"\U00010000\"=선형 \"\U00020BB7\"=길 \"\U00020BB7野家\"=요시노야 "
         "\"\U00020BB7野家だ\">4:길,10:요시노야 \"\U00020BB8\"> \"\uFF5E\">3:물결"},
        // Keys hold U+FFFD for invalid bytes; a lookup reads an invalid byte as U+FFFD.
        {"invalid UTF-8 is U+FFFD",
         {{"a\uFFFDb", "x"}, {"a\uFFFDc", "y"}},
         {"a\xFE" "bc", "a\uFFFDb", "a\xE3\x81" "b"},
         "\"a\uFFFDb\"=x \"a\uFFFDc\"=y \"a\xFE" "bc\">3:x \"a\uFFFDb\">5:x \"a\xE3\x81" "b\">"},
    };
    for (const auto& c : cases) {
        const std::string got = Dump(Build(c.entries), c.queries);
        if (got != c.want) std::fprintf(stderr, "%s:\n got %s\nwant %s\n", c.name, got.c_str(), c.want.c_str());
        HD_CHECK_EQ(got, c.want);
    }
}

HD_TEST(trie_empty) {
    const Trie none;
    HD_CHECK_EQ(none.Find(""), -1);
    HD_CHECK_EQ(none.LongestPrefix("abc").key, -1);
    HD_CHECK_EQ(none.KeyCount(), size_t{0});
    const Trie built = Trie::Build(std::span<const std::string_view>(), nullptr);
    HD_CHECK_EQ(built.Find(""), -1);
    HD_CHECK_EQ(built.NodeCount(), size_t{1});
    int calls = 0;
    built.Each([&](std::string_view, int) { calls++; });
    built.Prefixes("a", [&](int, size_t) { calls++; });
    HD_CHECK_EQ(calls, 0);
}

// Compares the trie with a map on random dictionaries whose keys share many
// prefixes and mix 1 to 4 byte code points.
HD_TEST(trie_random) {
    const char32_t alphabet[] = {U'a', U'あ', U'学', 0xFFFD, 0xE000, 0x20BB7, 0x20BB8, 0x10000};
    Rng rng{12};
    auto word = [&] {
        std::string w;
        for (size_t n = rng.N(6); n > 0; n--) hdTrans::text::AppendRune(w, alphabet[rng.N(std::size(alphabet))]);
        return w;
    };
    for (size_t size : {1, 7, 8, 9, 100, 3000}) {
        std::map<std::string, int> want;  // key -> index in sorted order
        for (size_t i = 0; i < size; i++) want[word()] = 0;
        std::vector<std::string> keys;
        for (auto& [k, v] : want) {
            v = static_cast<int>(keys.size());
            keys.push_back(k);
        }
        std::vector<std::string_view> views(keys.begin(), keys.end());
        std::vector<int32_t> order;
        const Trie t = Trie::Build(views, &order);
        HD_CHECK_EQ(t.KeyCount(), keys.size());
        HD_REQUIRE(order.size() == keys.size());
        std::vector<int32_t> seen = order;
        std::sort(seen.begin(), seen.end());
        for (size_t i = 0; i < seen.size(); i++) HD_CHECK_EQ(seen[i], static_cast<int32_t>(i));

        std::vector<std::string> each;
        bool numbersMatch = true;
        t.Each([&](std::string_view key, int k) {
            each.emplace_back(key);
            numbersMatch = numbersMatch && keys[static_cast<size_t>(order[static_cast<size_t>(k)])] == key;
        });
        HD_CHECK(each == keys);
        HD_CHECK(numbersMatch);

        for (int i = 0; i < 2000; i++) {
            const std::string q = word() + word();
            const int k = t.Find(q);
            const auto it = want.find(q);
            if (it == want.end()) {
                HD_CHECK_EQ(k, -1);
            } else {
                HD_REQUIRE(k >= 0);
                HD_CHECK_EQ(order[static_cast<size_t>(k)], it->second);
            }
            std::vector<std::pair<size_t, std::string>> got;
            t.Prefixes(q, [&](int key, size_t n) { got.push_back({n, keys[static_cast<size_t>(order[static_cast<size_t>(key)])]}); });
            const auto brute = BrutePrefixes(want, q);
            HD_CHECK(got == brute);
            const auto m = t.LongestPrefix(q);
            if (brute.empty()) {
                HD_CHECK_EQ(m.key, -1);
            } else {
                HD_CHECK_EQ(m.length, brute.back().first);
                HD_CHECK_EQ(keys[static_cast<size_t>(order[static_cast<size_t>(m.key)])], brute.back().second);
            }
        }
    }
}

// Keys of testdata/sample_keys.tsv: every lookup at every position of some text
// agrees with a scan of the key list, in order, without allocating.
HD_TEST(trie_sample_keys) {
    const std::string path = SourceDir() + "/testdata/sample_keys.tsv";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    HD_REQUIRE(f != nullptr);
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);

    std::map<std::string, int> want;
    hdTrans::dict::TsvReader rows(text);
    while (rows.Next()) want[std::string(rows.Fields()[0])] = 0;
    HD_REQUIRE(want.size() > 100 && want.size() <= 200);
    std::vector<std::string> keys;
    for (auto& [k, v] : want) {
        v = static_cast<int>(keys.size());
        keys.push_back(k);
    }
    std::vector<std::string_view> views(keys.begin(), keys.end());
    std::vector<int32_t> order;
    const size_t beforeBuild = allocations;
    const Trie t = Trie::Build(views, &order);
    HD_CHECK(allocations > beforeBuild);  // the counter sees the library's allocations
    auto keyOf = [&](int k) { return keys[static_cast<size_t>(order[static_cast<size_t>(k)])]; };

    struct Case {
        std::string_view text;
        std::vector<std::string> prefixes;  // at the start of text, shortest first
    };
    const Case cases[] = {
        {"東京都庁へ行く", {"東", "東京", "東京都"}},
        {"東京タワーの近く", {"東", "東京", "東京タワー"}},
        {"大学生です", {"大", "大学", "大学生"}},
        {"大丈夫です", {"大", "大丈夫"}},
        {"日本語の本", {"日", "日本", "日本語"}},
        {"ありがとうございました", {"ありがとう"}},
        {"ありがとうございます。", {"ありがとう", "ありがとうございます"}},
        {"ではない", {"では"}},
        {"では無い", {"では", "では無い"}},
        {"CDプレーヤー", {"CD", "CDプレーヤー"}},
        {"ＯＫです", {"ＯＫ"}},
        {"ﾃﾚﾋﾞ番組", {"ﾃﾚﾋﾞ"}},
        {"𠮷野家の牛丼", {"𠮷", "𠮷野", "𠮷野家"}},
        {"𠮷田さん", {"𠮷", "𠮷田"}},
        {"𩸽の干物", {"𩸽", "𩸽の干物"}},
        {"𠀋", {"𠀋"}},
        {"𠮸", {}},
        {"～だ", {"～"}},
        {"〜だ", {"〜"}},
        {"一人暮らし", {"一", "一人", "一人暮らし"}},
        {"花火大会", {"花", "花火", "花火大会"}},
        {"コーヒーカップ", {"コーヒー", "コーヒーカップ"}},
        {"カメラマン", {"カメラ", "カメラマン"}},
        {"東北地方の", {"東", "東北", "東北地方"}},
        {"", {}},
        {"x", {}},
    };
    for (const auto& c : cases) {
        std::vector<std::string> got;
        t.Prefixes(c.text, [&](int k, size_t n) {
            got.push_back(keyOf(k));
            HD_CHECK_EQ(n, keyOf(k).size());
        });
        HD_CHECK(got == c.prefixes);
        if (got != c.prefixes) std::fprintf(stderr, "prefixes of %.*s\n", static_cast<int>(c.text.size()), c.text.data());
    }

    const std::string sentences[] = {
        "東京都の大学生は日本語と中国語を学ぶ。",
        "私たちは𠮷野家で𩸽の干物を食べた。",
        "おはようございます、山田先生。今日は花火大会ですね！",
        "CDプレーヤーとＯＫボタン、ﾃﾚﾋﾞとカメラマン。",
        "\xFF東\xE3\x81京",
    };
    std::vector<std::pair<size_t, std::string>> got;
    got.reserve(16);
    for (const auto& s : sentences) {
        for (size_t at = 0; at <= s.size(); at++) {
            if (at < s.size() && !RuneStart(s[at])) continue;
            const std::string rest = s.substr(at);
            got.clear();
            const size_t before = allocations;
            int found = 0;
            t.Prefixes(rest, [&](int, size_t) { found++; });
            const auto longest = t.LongestPrefix(rest);
            const int exact = t.Find(rest);
            HD_CHECK_EQ(allocations, before);
            (void)longest;
            (void)exact;
            t.Prefixes(rest, [&](int k, size_t n) { got.push_back({n, keyOf(k)}); });
            const auto brute = BrutePrefixes(want, rest);
            HD_CHECK(got == brute);
            HD_CHECK_EQ(found, static_cast<int>(brute.size()));
        }
    }
    for (const auto& k : keys) {
        const size_t before = allocations;
        const int n = t.Find(k);
        HD_CHECK_EQ(allocations, before);
        HD_REQUIRE(n >= 0);
        HD_CHECK_EQ(keyOf(n), k);
    }
}

HD_TEST(trie_borrowed_arrays_and_moves) {
    const std::string_view keys[] = {"", "東", "東京", "東京都", "𠮷", "𠮷野家"};
    Trie owned = Trie::Build(keys, nullptr);
    const Trie::ViewData arrays = owned.View();
    const size_t before = allocations;
    Trie borrowed = Trie::FromView(arrays);
    HD_CHECK_EQ(allocations, before);
    HD_CHECK(borrowed.View().tree.data() == arrays.tree.data());
    HD_CHECK(borrowed.View().label.data() == arrays.label.data());
    HD_CHECK_EQ(borrowed.MemoryBytes(), owned.MemoryBytes());

    Trie moved = std::move(owned);
    owned = std::move(moved);
    for (const auto key : keys) HD_CHECK_EQ(borrowed.Find(key), owned.Find(key));
    HD_CHECK_EQ(borrowed.Find("東北"), -1);
    HD_CHECK_EQ(borrowed.LongestPrefix("東京都庁").length, size_t{9});
    std::vector<std::string> got;
    borrowed.Each([&](std::string_view key, int) { got.emplace_back(key); });
    HD_CHECK(got == std::vector<std::string>(std::begin(keys), std::end(keys)));
    borrowed = Trie::FromView(owned.View());
    HD_CHECK_EQ(borrowed.Find("𠮷野家"), owned.Find("𠮷野家"));
}
