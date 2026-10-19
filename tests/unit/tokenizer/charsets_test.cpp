#include "core/tokenizer/charsets.h"

#include "fnv.h"
#include "hdtest.h"

using namespace hdTrans::tokenizer;

namespace {

uint64_t Fingerprint(bool (*pred)(char32_t)) {
    hdtest::Fnv h;
    for (char32_t c = 0; c <= 0x10FFFF; c++) h.Byte(pred(c) ? 1 : 0);
    return h.h;
}

}  // namespace

// The expected values are the hashes of the sets the data was generated from.
HD_TEST(charsets_fingerprints) {
    HD_CHECK_EQ(Fingerprint(IsNonKanjiHan), uint64_t{0xe9d9f6f842f59029ull});
    HD_CHECK_EQ(Fingerprint(IsCp932Symbol), uint64_t{0x73d57d9ef645e651ull});
    HD_CHECK_EQ(Fingerprint(IsSjisHighLead), uint64_t{0xc390093eef0adbaaull});
    HD_CHECK_EQ(internal::NonKanjiHanTable().size(), size_t{362});
    HD_CHECK_EQ(internal::Cp932SymbolTable().size(), size_t{438});
    HD_CHECK_EQ(internal::SjisHighLeadTable().size(), size_t{2353});
}

HD_TEST(charsets_sorted) {
    for (const auto t : {internal::NonKanjiHanTable(), internal::Cp932SymbolTable(), internal::SjisHighLeadTable()}) {
        for (size_t i = 1; i < t.size(); i++) HD_CHECK(t[i - 1] < t[i]);
    }
}

HD_TEST(charsets_samples) {
    HD_CHECK(IsNonKanjiHan(U'々'));
    HD_CHECK(IsNonKanjiHan(U'仝'));
    HD_CHECK(IsNonKanjiHan(U'髙'));
    HD_CHECK(!IsNonKanjiHan(U'高'));
    HD_CHECK(IsCp932Symbol(U'。'));
    HD_CHECK(IsCp932Symbol(U'Ω'));
    HD_CHECK(IsCp932Symbol(U'＃'));
    HD_CHECK(!IsCp932Symbol(U'あ'));
    HD_CHECK(!IsCp932Symbol(U'가'));
    HD_CHECK(IsSjisHighLead(U'丨'));
    HD_CHECK(IsSjisHighLead(U'ⅰ'));
    HD_CHECK(!IsSjisHighLead(U'一'));
}
