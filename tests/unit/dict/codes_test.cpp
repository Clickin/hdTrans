#include "hdTrans/dict/codes.h"

#include <string>

#include "hdtest.h"

using hdTrans::dict::DictCode;
using hdTrans::dict::GramCodes;
using hdTrans::dict::PartCode;
using hdTrans::dict::PosCodes;
using hdTrans::dict::SemCodes;
using hdTrans::dict::WordClass;

namespace {

std::string Pos(std::string_view pos) {
    std::string_view part, sem, gram;
    if (!PosCodes(pos, part, sem, gram)) return "unknown";
    return std::string(part) + "|" + std::string(sem) + "|" + std::string(gram);
}

}  // namespace

HD_TEST(PartCodeBytes) {
    const PartCode p("M429");
    HD_CHECK_EQ(p.Tag(), 'M');
    HD_CHECK_EQ(p.ClassDigit(), '4');
    HD_CHECK_EQ(p.Sub(), '2');
    HD_CHECK_EQ(p.Kind(), '9');
    HD_CHECK(p.Class() == WordClass::Verb);
    HD_CHECK_EQ(p.VerbKind(), 9);
    HD_CHECK_EQ(std::string(p.Text()), "M429");
    HD_CHECK_EQ(std::string(p.Code()), "429");
    HD_CHECK(p.TagIn("MHG"));
    HD_CHECK(!p.TagIn("NIK"));
    HD_CHECK(p.LetterTag());

    // Kinds outside the digits keep their arithmetic: ')' is -7, '?' is 15.
    HD_CHECK_EQ(PartCode("H7U)").VerbKind(), -7);
    HD_CHECK_EQ(PartCode("M42?").VerbKind(), 15);
    // An ending's branch reaches 'A' (17).
    HD_CHECK_EQ(PartCode("S5A0").EndingBranch(), 17);
    HD_CHECK(PartCode("1110").Class() == WordClass::Noun);
    HD_CHECK(!PartCode("1110").LetterTag());
    HD_CHECK(PartCode("NA00").Class() == WordClass::Other);
}

HD_TEST(PartCodeOddLengths) {
    const PartCode empty;
    HD_CHECK(empty.Empty());
    HD_CHECK_EQ(empty.At(0), 0);
    HD_CHECK(empty.Class() == WordClass::Other);
    HD_CHECK(!empty.TagIn("NIK"));
    HD_CHECK_EQ(std::string(empty.Code()), "");

    // A missing class byte counts as 0 for the class comparisons.
    const PartCode one("N");
    HD_CHECK_EQ(one.Size(), size_t{1});
    HD_CHECK_EQ(one.At(1), 0);
    HD_CHECK(one.SameClass(PartCode("M")));
    HD_CHECK(!one.SameClass(PartCode("N1")));

    // A longer text keeps its length and its first four bytes.
    const PartCode longer("N1100X");
    HD_CHECK_EQ(longer.Size(), size_t{6});
    HD_CHECK_EQ(std::string(longer.Text()), "N110");
    HD_CHECK(!(longer == PartCode("N110")));

    // Bytes are C chars: a byte above 0x7F sorts before the letters.
    const PartCode high("\xEDN10");
    HD_CHECK(high.At(0) < PartCode("A110").At(0));
    HD_CHECK(!high.LetterTag());
}

HD_TEST(SemCodesKeepTheText) {
    // A place sense written with a space: the chunks are "QCT" and " VP", the rest "l".
    const SemCodes place("QCT VPl");
    HD_CHECK_EQ(std::string(place.Text()), "QCT VPl");
    HD_CHECK(!place.Aligned());
    HD_CHECK_EQ(place.Count(), size_t{2});
    HD_CHECK_EQ(std::string(place.At(1)), " VP");
    HD_CHECK_EQ(std::string(place.Rest()), "l");
    HD_CHECK(place.Contains("VPl"));
    HD_CHECK(!place.HasCode("VPl"));
    HD_CHECK(place.HasCode("QCT"));

    // The first code is not any code.
    const SemCodes person("VHnQPN");
    HD_CHECK(!person.HasPrefix("QPN"));
    HD_CHECK(person.Contains("QPN"));
    HD_CHECK(SemCodes("QPNVHn").HasPrefix("QPN"));

    // QAA..QAH is a byte range of whole codes.
    HD_CHECK(SemCodes("VSsQAC").HasCodeIn("QAA", "QAH"));
    HD_CHECK(!SemCodes("VSsQAI").HasCodeIn("QAA", "QAH"));
    HD_CHECK(!SemCodes("VQAC").HasCodeIn("QAA", "QAH"));

    SemCodes x("QPN");
    x.Append("XXX");
    HD_CHECK_EQ(std::string(x.Text()), "QPNXXX");
    HD_CHECK_EQ(std::string(x.Last()), "XXX");
}

HD_TEST(GramCodesOddTexts) {
    // A three-byte fallback code: Nj is in it, Nd is not.
    const GramCodes njd("Njd");
    HD_CHECK(njd.Contains("Nj"));
    HD_CHECK(!njd.Contains("Nd"));
    HD_CHECK_EQ(std::string(njd.Rest()), "d");

    // A trailing space stays.
    HD_CHECK_EQ(std::string(GramCodes("Wb ").Text()), "Wb ");
    HD_CHECK(GramCodes("WaWb").HasPrefix("Wa"));
    HD_CHECK(!GramCodes("WbWa").HasPrefix("Wa"));
    // The odd rule chunk "n" is true for any code holding a lower-case n.
    HD_CHECK(GramCodes("PnHd").Contains("n"));

    // The passive rewrite relies on Ka being last.
    GramCodes voice("HdKa");
    HD_CHECK(voice.CutSuffix("Ka"));
    voice.Append("Pi");
    HD_CHECK_EQ(std::string(voice.Text()), "HdPi");
    GramCodes notLast("KaHd");
    HD_CHECK(!notLast.CutSuffix("Ka"));
    HD_CHECK_EQ(std::string(notLast.Text()), "KaHd");

    // The digit after the first J at any offset, when 0..7.
    HD_CHECK_EQ(GramCodes("J1J2").PunctDigit(), '1');
    HD_CHECK_EQ(GramCodes("NJ2").PunctDigit(), '2');
    HD_CHECK_EQ(GramCodes("JsJ2").PunctDigit(), 0);
    HD_CHECK_EQ(GramCodes("J8").PunctDigit(), 0);
    HD_CHECK_EQ(GramCodes("WaJ").PunctDigit(), 0);
    HD_CHECK_EQ(GramCodes("Wa").PunctDigit(), 0);
}

HD_TEST(DictCodes) {
    HD_CHECK(DictCode("SO").SourceOnly());
    HD_CHECK(!DictCode("So").SourceOnly());
    HD_CHECK(DictCode("Gn") == std::string_view("Gn"));
    HD_CHECK(!(DictCode("Gn") == DictCode("gn")));
    HD_CHECK(DictCode() == std::string_view(""));
}

HD_TEST(PosTags) {
    // Part codes are kept as written, whatever they hold.
    HD_CHECK_EQ(Pos("N110"), "N110||");
    HD_CHECK_EQ(Pos("M42?"), "M42?||");
    HD_CHECK_EQ(Pos("N1100X"), "N1100X||");
    HD_CHECK_EQ(Pos(""), "||");
    // UniDic, English and IPAdic tags take the codes of the first row naming them.
    HD_CHECK_EQ(Pos("名詞-固有名詞-人名-姓"), "N130|QPN|Wa");
    HD_CHECK_EQ(Pos("noun-proper-name-surname"), "N130|QPN|Wa");
    HD_CHECK_EQ(Pos("名詞-固有名詞-地名-一般"), "N130|QCTVPl|");
    HD_CHECK_EQ(Pos("noun-common-general"), "N110||");
    HD_CHECK_EQ(Pos("副詞"), "E300||");
    HD_CHECK_EQ(Pos("adverb"), "E300||");
    HD_CHECK_EQ(Pos("verb-general,godan-ra"), "M428||");
    HD_CHECK_EQ(Pos("動詞-一般,五段-ラ行"), "M428||");
    HD_CHECK_EQ(Pos("動詞-一般,五段-ラ行,終止形-一般"), "M428||");
    HD_CHECK_EQ(Pos("verb-general,godan-ra,terminal-general"), "M428||");
    HD_CHECK_EQ(Pos("verb-general,godan-ra,*"), "M428||");
    HD_CHECK_EQ(Pos("verb-general,godan-ra,連用形-一般"), "unknown");
    // A cType of "*" is no cType.
    HD_CHECK_EQ(Pos("adverb,*"), "E300||");
    // Tags without a part code, unknown tags and lower-case values are unknown.
    HD_CHECK_EQ(Pos("aux_symbol-comma"), "unknown");
    HD_CHECK_EQ(Pos("foo-bar"), "unknown");
    HD_CHECK_EQ(Pos("n110"), "unknown");
    HD_CHECK_EQ(Pos("N1,10"), "unknown");
}

HD_TEST(GramCodesPunctuationDigits) {
    const struct { const char* attrs; int digit; } cases[] = {
        {"J0", '0'}, {"J1", '1'}, {"J2", '2'}, {"J3", '3'}, {"J4", '4'},
        {"J5", '5'}, {"J6", '6'}, {"J7", '7'}, {"J8", 0},
        {"NJ7Wa", '7'}, {"J7J1", '7'}, {"J8J7", 0}, {"JsJ7", 0},
        {"J", 0}, {"", 0}, {"J/", 0}, {"J9", 0}, {"J00", '0'},
    };
    for (const auto& c : cases) HD_CHECK_EQ(static_cast<int>(GramCodes(c.attrs).PunctDigit()), c.digit);
}
