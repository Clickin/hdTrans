#include "core/dict/tsv.h"

#include <cstring>
#include <string>
#include <vector>

#include "hdtest.h"

using hdTrans::dict::Sense;
using hdTrans::dict::SenseReader;
using hdTrans::dict::TsvError;
using hdTrans::dict::TsvErrorKind;
using hdTrans::dict::TsvReader;

namespace {

// "line:field|field|..." for every row of r, joined by "\n".
std::string Rows(TsvReader& r) {
    std::string out;
    while (r.Next()) {
        if (!out.empty()) out += "\n";
        out += std::to_string(r.Line()) + ":";
        for (size_t i = 0; i < r.Fields().size(); i++) {
            if (i) out += "|";
            out += r.Fields()[i];
        }
    }
    return out;
}

// Serves a string in chunks of at most chunk bytes.
struct Chunks {
    std::string_view text;
    size_t chunk;
    size_t pos = 0;
    bool fail = false;  // fail once the text is served

    static std::ptrdiff_t Read(void* context, char* buf, size_t size) {
        auto* c = static_cast<Chunks*>(context);
        if (c->pos == c->text.size() && c->fail) return -1;
        size_t n = c->text.size() - c->pos;
        if (n > c->chunk) n = c->chunk;
        if (n > size) n = size;
        std::memcpy(buf, c->text.data() + c->pos, n);
        c->pos += n;
        return static_cast<std::ptrdiff_t>(n);
    }
};

std::string Show(const Sense& s) {
    std::string flags;
    if (s.xxx) flags += "xxx";
    if (s.replace) flags += flags.empty() ? "replace" : " replace";
    std::string out;
    for (std::string_view f : {s.key, s.reading, s.ko, s.pos, s.sem, s.gram, s.dict}) {
        out += f;
        out += "|";
    }
    return out + std::to_string(s.rule) + "|" + std::to_string(s.prio) + "|" + std::to_string(s.id) + "|" + flags;
}

struct Read {
    std::vector<std::string> senses;
    std::vector<TsvError> errors;
};

Read ReadSenses(std::string_view text) {
    TsvReader rows(text);
    SenseReader r(rows);
    Read out;
    for (Sense s; r.Next(s);) out.senses.push_back(Show(s));
    out.errors = r.Errors();
    return out;
}

std::string ShowErrors(const std::vector<TsvError>& errors) {
    std::string out;
    for (const auto& e : errors) {
        if (!out.empty()) out += " ";
        out += std::to_string(e.line) + ":" + std::to_string(static_cast<int>(e.kind));
    }
    return out;
}

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += s + "\n";
    return out;
}

}  // namespace

HD_TEST(tsv_lines) {
    struct Case {
        std::string_view in;
        std::string_view comment;
        std::string_view want;
    };
    const Case cases[] = {
        {"", "#", ""},
        {"\n\n", "#", ""},
        {"a\tb\nc", "#", "1:a|b\n2:c"},
        {"a\r\nb\r\n", "#", "1:a\n2:b"},
        {"a\r\r\nb\r", "#", "1:a\r\n2:b"},              // one \r is dropped
        {"\r\n\r\nx\n", "#", "3:x"},                     // a line of \r alone is empty
        {"\xEF\xBB\xBFkey\tko\n", "#", "1:key|ko"},     // byte order mark
        {"\xEF\xBB\xBF# c\nx\n", "#", "2:x"},           // comment after the mark
        {"x\n\xEF\xBB\xBFy\n", "#", "1:x\n2:\xEF\xBB\xBFy"},  // only on the first line
        {"# c\n #x\n\t\n", "#", "2: #x\n3:|"},          // comment only at the start
        {"a\t\t\n\tb\n", "#", "1:a||\n2:|b"},
        {"a\"b\"\t\\t\n", "#", "1:a\"b\"|\\t"},         // no quoting or escapes
        {"// c\n#x\n/x\n", "//", "2:#x\n3:/x"},
        {"#x\n", "", "1:#x"},
    };
    for (const auto& c : cases) {
        TsvReader r(c.in, c.comment);
        HD_CHECK_EQ(Rows(r), std::string(c.want));
        HD_CHECK(!r.Failed());
    }
}

HD_TEST(tsv_streaming_matches_text) {
    std::string text = "\xEF\xBB\xBF# head\r\nkey\tko\r\n\r\n";
    for (int i = 0; i < 2000; i++) text += "k" + std::to_string(i) + "\t" + std::string(static_cast<size_t>(i % 37), 'x') + "\n";
    text += std::string(200000, 'L') + "\tlong\r\n";  // longer than a read chunk
    text += "last\tline";
    TsvReader whole(text);
    const std::string want = Rows(whole);
    for (size_t chunk : {size_t{1}, size_t{2}, size_t{3}, size_t{4096}, size_t{1} << 20}) {
        Chunks src{text, chunk};
        TsvReader r(Chunks::Read, &src);
        HD_CHECK(Rows(r) == want);
        HD_CHECK(!r.Failed());
    }
}

HD_TEST(tsv_read_error) {
    const std::string text = "key\tko\tpos\tid\n学生\t학생\tN110\t1\n";
    Chunks src{text, 7, 0, true};
    TsvReader rows(Chunks::Read, &src);
    SenseReader r(rows);
    int n = 0;
    for (Sense s; r.Next(s);) n++;
    HD_CHECK_EQ(n, 1);
    HD_CHECK(rows.Failed());
    HD_CHECK_EQ(ShowErrors(r.Errors()), std::string("0:0"));
}

HD_TEST(tsv_read_senses) {
    const auto got = ReadSenses(
        "\xEF\xBB\xBF# test dictionary\n"
        "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\tsrc\n"
        "学生\t학생\tN110\tVHnUAq\t\tGN\t\t\t\t\tcore.1\tx\n"
        "学生\t학도\tN110\t\t\tSO\t12\txxx\t\t\tcore.2\n"
        "学生\t생도\tN110\t\tWbHd\tGN\t\t\t2\t\tcore.3\n"
        "頑張\t분발&AH@\tM427\t\t\tGN\t\tkana llm\t\tがんば\tcore.4\n"
        "秘密\t비밀\tN110\t\t\t\t\thidden\t\t\tcore.5\n");
    HD_CHECK_EQ(Join(got.senses), Join({
                                      "学生||학생|N110|VHnUAq||GN|0|0|3|",
                                      "学生||학도|N110|||SO|12|0|4|xxx",
                                      "学生||생도|N110||WbHd|GN|0|2|5|",
                                      "頑張|がんば|분발&AH@|M427|||GN|0|0|6|",
                                  }));
    HD_CHECK(got.errors.empty());
}

HD_TEST(tsv_bad_files) {
    struct Case {
        std::string_view in;
        std::string_view errors;  // line:TsvErrorKind
        size_t senses;
    };
    const Case cases[] = {
        {"key\tko\tpos\n学生\t학생\tN110\n", "1:4", 0},                     // no id column
        {"# c\n\nko\tpos\tid\n학생\tN110\t1\n", "3:1", 0},                 // no key column; stops
        {"key\tko\tpos\tid\n学生\t학생\tN110\tx\textra\n", "2:5", 0},      // more fields than the header
        {"key\tko\tpos\tflags\tid\n学生\t학생\tN110\tadd\tx\n", "2:7", 0},  // unknown flag
        {"key\tko\tpos\trule\tid\n学生\t학생\tN110\tL12\tx\n", "2:6", 0},   // rule not a number
        {"key\tko\tpos\tprio\tid\n学生\t학생\tN110\t1.5\tx\n", "2:6", 0},
        {"key\tko\tpos\trule\tflags\tid\n学\t학\tN110\tL\tadd\tx\n", "2:7", 0},  // the last error is recorded
        {"key\tko\tpos\tflags\tid\n\t학\tN110\t\t1\n学\t학\tN110\thidden\t\n", "2:8 3:8", 0},
        {"key\tko\tpos\tid\n学\t학\tN110\n犬\t개\tN110\t2\n", "2:8", 1},     // missing id field
        {"key\tko\tid\tpos\n学\t학\t1\n", "", 1},                          // fewer fields: empty
        {"# only comments\n\n", "", 0},
    };
    for (const auto& c : cases) {
        const auto got = ReadSenses(c.in);
        HD_CHECK_EQ(ShowErrors(got.errors), std::string(c.errors));
        HD_CHECK_EQ(got.senses.size(), c.senses);
    }
}

HD_TEST(tsv_sense_fields) {
    const auto got = ReadSenses(
        "id\tflags\treading\tprio\trule\tpos\tko\tkey\n"  // any column order
        "jisjk:1234\t\t\t\t\tN110\t가\t甲\n"
        "x:-5\tkana\tこう\t-1\t4294967297\tN110\t가\t甲\n"  // rule keeps its low 32 bits
        "+7\tkana\t乙\t\t\tN110\t을\t乙\n"                 // reading equal to the key
        "99999999999\tkana\t\t\t\tN110\t을\t乙\n"         // id keeps its low 32 bits; empty reading
        "99999999999999999999\t\t\t\t\tN110\t을\t乙\n"    // not a 64-bit number: the line number
        "a:\t\t\t\t\tN110\t을\t乙\n"
        "a:b:12\txxx\u3000replace\t\t\t\tN110\t병\t丙\n"   // flags split at any white space
        "\t\t\t\t\t名詞-普通名詞-一般\t병\t丙\n"                // no id
        "core.1\t xxx  xxx \t\t\t\t名詞-普通名詞-一般\t병\t丙\n");
    HD_CHECK_EQ(Join(got.senses), Join({
                                      "甲||가|N110||||0|0|1234|",
                                      "甲|こう|가|N110||||1|-1|-5|",
                                      "乙||을|N110||||0|0|7|",
                                      "乙||을|N110||||0|0|1215752191|",
                                      "乙||을|N110||||0|0|6|",
                                      "乙||을|N110||||0|0|7|",
                                      "丙||병|N110||||0|0|12|xxx replace",
                                      "丙||병|名詞-普通名詞-一般||||0|0|10|xxx",
                                  }));
    HD_CHECK_EQ(ShowErrors(got.errors), std::string("9:8"));
}

HD_TEST(tsv_parse_int32) {
    using hdTrans::dict::internal::ParseInt32;
    struct Case {
        std::string_view in;
        bool ok;
        int32_t want;
    };
    const Case cases[] = {
        {"0", true, 0},
        {"+12", true, 12},
        {"-12", true, -12},
        {"007", true, 7},
        {"2147483648", true, INT32_MIN},
        {"9223372036854775807", true, -1},
        {"-9223372036854775808", true, 0},
        {"9223372036854775808", false, 0},
        {"-9223372036854775809", false, 0},
        {"", false, 0},
        {"-", false, 0},
        {"+", false, 0},
        {"1_000", false, 0},
        {" 1", false, 0},
        {"1 ", false, 0},
        {"0x10", false, 0},
        {"１", false, 0},
    };
    for (const auto& c : cases) {
        int32_t v = 0;
        HD_CHECK_EQ(ParseInt32(c.in, v), c.ok);
        if (c.ok) HD_CHECK_EQ(v, c.want);
    }
}

// Rows whose pos is a part-of-speech tag keep it as written; the tag is resolved
// against the pos-tags table by the dictionary loader.
HD_TEST(tsv_pos_tags_pass_through) {
    const auto got = ReadSenses(
        "key\tko\tpos\tsem\tgram\tid\n"
        "佐藤\t사토\t名詞-固有名詞-人名-姓\t\t\t1\n"
        "佐藤\t사토\tnoun-proper-name-surname\tQPNVHn\t\t2\n"
        "書\t쓰&AA@\t動詞-一般,五段-カ行,終止形-一般\t\t\t3\n"
        "書\t쓰&AA@\tverb-general,godan-ka\t\tPn\t4\n"
        "勉強\t공부\t名詞-サ変接続,*,*\t\t\t6\n"
        "猫\t고양이\tN110\t\t\t13\n");
    HD_CHECK_EQ(Join(got.senses), Join({
                                      "佐藤||사토|名詞-固有名詞-人名-姓||||0|0|1|",
                                      "佐藤||사토|noun-proper-name-surname|QPNVHn|||0|0|2|",
                                      "書||쓰&AA@|動詞-一般,五段-カ行,終止形-一般||||0|0|3|",
                                      "書||쓰&AA@|verb-general,godan-ka||Pn||0|0|4|",
                                      "勉強||공부|名詞-サ変接続,*,*||||0|0|6|",
                                      "猫||고양이|N110||||0|0|13|",
                                  }));
    HD_CHECK(got.errors.empty());
}
