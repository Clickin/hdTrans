#include "hdTrans/dict/snapshot.h"

#include <string>
#include <vector>

#include "hdtest.h"

using namespace hdTrans::dict;

namespace {

std::string SourceDir() {
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return slash == std::string::npos ? "." : f.substr(0, slash);
}

std::string Folder() { return SourceDir() + "/testdata/folder"; }

std::string Show(const Candidate& c) {
    std::string s = "[" + std::string(c.source) + "] " + std::string(c.target) + " " + c.codes->partText + "|" +
                    std::string(c.codes->sem.Text()) + "|" + std::string(c.codes->gram.Text()) + "|" +
                    std::string(c.codes->dict.Text()) + " r" + std::to_string(c.rule) + " id" + std::to_string(c.id);
    if (c.user) s += c.user->nameLayer ? " name" : " plain";
    return s;
}

// "count N" and a line per candidate.
std::string Show(const Candidates& cs) {
    std::string s = "count " + std::to_string(cs.count);
    for (const Candidate& c : cs.list) s += "\n" + Show(c);
    return s;
}

std::string Show(const std::vector<Candidate>& cs) {
    std::string s;
    for (const Candidate& c : cs) s += (s.empty() ? "" : "\n") + Show(c);
    return s;
}

std::string Targets(const std::vector<Record>& rs) {
    std::string s;
    for (const Record& r : rs) s += (s.empty() ? "" : " ") + std::string(r.target);
    return s;
}

const std::string kHeader = "key\tko\tpos\tsem\tgram\tdict\tid\n";

File MakeFile(std::string name, std::string text) {
    File f;
    f.name = std::move(name);
    f.text = std::move(text);
    return f;
}

// A plain entry with codes of table.
UserEntry Entry(CodeTable& table, std::string key, std::string target, std::string_view part, int line) {
    UserEntry e;
    e.key = std::move(key);
    e.target = std::move(target);
    e.codes = table.At(table.Intern(part, "", "", "GN"));
    e.line = line;
    return e;
}

}  // namespace

HD_TEST(SnapshotFolderLayers) {
    const LoadResult r = LoadFolders({Folder(), Folder() + "/over"});
    HD_REQUIRE(r.snapshot != nullptr);
    const Snapshot& s = *r.snapshot;
    // The line with an unknown tag is skipped and listed; the load takes place.
    HD_REQUIRE(r.errors.size() == 1);
    HD_CHECK(r.errors[0].kind == LoadErrorKind::UnknownPos);
    HD_CHECK_EQ(r.errors[0].line, 17);
    HD_CHECK(r.errors[0].file.ends_with("base.tsv"));
    HD_CHECK(s.HasDictionary());
    HD_REQUIRE(s.PlainFiles().size() == 1);
    HD_CHECK(s.PlainFiles()[0].name.ends_with("plain.tsv"));
    HD_CHECK(s.PlainFiles()[0].text.starts_with("// A plain dictionary"));
    HD_REQUIRE(s.Warnings().size() == 1);
    HD_CHECK(s.Warnings()[0].ends_with("names.tsv: name senses dropped: 1 keys with 、 or 。, 1 inflected keys, 1 long Korean"));
    HD_CHECK_EQ(s.Base().Len(), size_t{17});
    HD_REQUIRE(s.User() != nullptr);
    HD_CHECK_EQ(s.User()->Len(), size_t{2});

    const LookupContext none;
    // more.tsv shadows the base senses of 学生 of class 1, words.tsv adds one with prio
    // 1, sub/ is not read; user-dictionary records come first by tag.
    HD_CHECK_EQ(Show(s.GetWordInfo("学生です", LookupMode::General, none)),
                "count 7\n"
                "[学] 유저학 E300|||GN r0 id0\n"
                "[学] 학교 I110|||PN r0 id28\n"
                "[学] 배울&AG@ M42;|||GN r0 id27\n"
                "[学生] 유저학생 N110|||GN r0 id0\n"
                "[学生] 학우 N110|||GN r0 id1\n"
                "[学生] 생도 N110|||GN r0 id1\n"
                "[学] 학 N110|||BZ r0 id26");
    // The later folder shadows the class-1 senses of 行 of every earlier file; the verb
    // senses stay, ordered by kind (? 9 8 7).
    HD_CHECK_EQ(Show(s.GetWordInfo("行くの", LookupMode::General, none)),
                "count 5\n"
                "[行く] 가&AE@ M42?|||GN r0 id18\n"
                "[行] 갈&AB@ M429|||GN r0 id5\n"
                "[行] 가&AA@ M428|||GN r0 id3\n"
                "[行] 다닐&AC@ M427|||GN r0 id6\n"
                "[行] 최종행 N110|||GN r0 id1");
    // words.tsv replace shadows; the base place sense of 霧島 takes the respelling of
    // names-places.tsv.
    HD_CHECK_EQ(Show(s.GetWordInfo("東京と", LookupMode::General, none)), "count 1\n[東京] 동경 N130|QCTVPl||GN r0 id2");
    HD_CHECK_EQ(Show(s.GetWordInfo("霧島さん", LookupMode::General, none)),
                "count 2\n"
                "[霧島] 키리시마 N130|QCTVPl||GN r0 id8\n"
                "[霧島] 키리시마 N130|QPN||GN r0 id9");
    // Special codes round-trip; SO senses never match.
    HD_CHECK_EQ(Show(s.GetWordInfo("大阪の", LookupMode::General, none)), "count 1\n[大阪] 오사카 N130|QCT VPl|Wb | r0 id16");
    HD_CHECK_EQ(Show(s.GetWordInfo("の", LookupMode::General, none)),
                "count 2\n[の] 한 P200|||GN r5089 id10\n[の] 의 P200|||GN r12 id11");
    HD_CHECK_EQ(Show(s.GetWordInfo("田中", LookupMode::General, none)), "count 1\n[田中] 다나카 N130|QPNXXX||GN r0 id19");
    HD_CHECK_EQ(Show(s.GetWordInfo("中村", LookupMode::General, none)), "count 1\n[中村] 나카무라 N130|XXXQPN||GN r0 id20");
    // The flag kana files the sense under its reading too; words.tsv adds a verb.
    HD_CHECK_EQ(Show(s.GetWordInfo("はしる", LookupMode::General, none)), "count 1\n[はしる] 달리다 N110|||GN r0 id21");
    HD_CHECK_EQ(Show(s.GetWordInfo("走る", LookupMode::General, none)),
                "count 3\n"
                "[走] 달리&AD@ M428|||GN r0 id13\n"
                "[走] 뛰&AH@ M428|||GN r0 id3\n"
                "[走る] 달리다 N110|||GN r0 id21");
    // Hidden and comment lines are not loaded; the key D is the sentinel of mode 1.
    HD_CHECK_EQ(Show(s.GetWordInfo("隠", LookupMode::General, none)), "count 0");
    HD_CHECK_EQ(Show(s.GetWordInfo("#注", LookupMode::General, none)), "count 0");
    HD_CHECK_EQ(Show(s.GetWordInfo("", LookupMode::AdjectivalNounEnding, none)), "count 1\n[] 1고 U5I1|||GN r0 id12");
    HD_CHECK_EQ(Show(s.GetWordInfo("Dragon", LookupMode::General, none)), "count 0");
    HD_CHECK_EQ(Show(s.GetWordInfo("Dragon", LookupMode::AdjectivalNounEnding, none)), "count 1\n[] 1고 U5I1|||GN r0 id12");

    // Exact lookups return the stored records: tags resolved by the part-of-speech table.
    HD_CHECK_EQ(Show(s.RetrieveRecords("山田", LookupMode::General)), "[山田] 야마다 N130|QPN|Wa|GN r0 id14");
    HD_CHECK_EQ(Show(s.RetrieveRecords("副", LookupMode::General)), "[副] 부 E300|||BZ r0 id25");
    HD_CHECK_EQ(Show(s.RetrieveRecords("", LookupMode::AdjectivalNounEnding)), "[D] 1고 U5I1|||GN r0 id12");
    HD_CHECK_EQ(Show(s.RetrieveRecords("霧島", LookupMode::General)),
                "[霧島] 기리시마 N130|QCTVPl||GN r0 id8\n[霧島] 키리시마 N130|QPN||GN r0 id9");

    // The name layer: a later name file shadows an earlier one; a non-person sense of a
    // respelled key is dropped.
    HD_CHECK_EQ(Targets(s.NameRecords("霧島")), "기리시마");
    HD_CHECK_EQ(Targets(s.NameRecords("京都")), "경도");
    HD_CHECK_EQ(Targets(s.NameRecords("食べるの")), "");
    HD_CHECK_EQ(Targets(s.NameRecords("猫")), "");
    // Names that are no base word take part; a base word only when cued.
    HD_CHECK_EQ(Show(s.GetWordInfo("アリスさん", LookupMode::General, none)), "count 1\n[アリス] 앨리스 N130|QPN||GN r0 id0 name");
    HD_CHECK_EQ(Show(s.GetWordInfo("アリスさん", LookupMode::VerbEnding, none)), "count 0");
    HD_CHECK_EQ(Show(s.GetWordInfo("翔太くんが", LookupMode::General, none)), "count 1\n[翔太] 쇼타 N130|QPN||GN r0 id0 name");
    HD_CHECK_EQ(s.PersonName("カイト"), "카이토");
    HD_CHECK_EQ(s.PersonName("アリス"), "");
    HD_CHECK(s.PersonCueKey("翔太"));
    HD_CHECK(!s.PersonCueKey("アリス"));
    const std::vector<std::string> keys = s.PersonPlaceKeys("霧島と霧島");
    HD_REQUIRE(keys.size() == 1);
    HD_CHECK_EQ(keys[0], "霧島");

    // A key the text confirms takes part with its person senses, dropping the base
    // and user senses of its key and class; persons read a respelled place key with the
    // person spelling.
    KeySet confirmed{"学生"}, persons{"霧島"};
    LookupContext ctx;
    ctx.confirmed = &confirmed;
    ctx.persons = &persons;
    HD_CHECK_EQ(Show(s.GetWordInfo("学生", LookupMode::General, ctx)),
                "count 5\n"
                "[学生] 가쿠세이 N130|QPN||GN r0 id0 name\n"
                "[学] 유저학 E300|||GN r0 id0\n"
                "[学] 학교 I110|||PN r0 id28\n"
                "[学] 배울&AG@ M42;|||GN r0 id27\n"
                "[学] 학 N110|||BZ r0 id26");
    HD_CHECK_EQ(Show(s.GetWordInfo("霧島", LookupMode::General, ctx)),
                "count 2\n"
                "[霧島] 기리시마 N130|QCTVPl||GN r0 id8\n"
                "[霧島] 기리시마 N130|QPN||GN r0 id9");
}

HD_TEST(SnapshotFolderWithoutLaterFolder) {
    const LoadResult r = LoadFolders({Folder()});
    HD_REQUIRE(r.snapshot != nullptr);
    // more.tsv shadows the base sense 행; words-extra.tsv adds 행차 after it.
    HD_CHECK_EQ(Show(r.snapshot->RetrieveRecords("行", LookupMode::General)),
                "[行] 가&AA@ M428|||GN r0 id3\n"
                "[行] 갈&AB@ M429|||GN r0 id5\n"
                "[行] 다닐&AC@ M427|||GN r0 id6\n"
                "[行] 줄 N110|||GN r0 id2\n"
                "[行] 행차 N110|||GN r0 id1");
}

HD_TEST(SnapshotFileErrors) {
    // A malformed line fails the load.
    std::vector<File> files;
    files.push_back(MakeFile("a/base.tsv", kHeader + "犬\t견\tN110\t\t\tGN\t1\n犬\t견\tN110\t\t\tGN\t1\textra\n犬\t견\tN110\t\t\tGN\t\n"));
    LoadResult r = Load(std::move(files));
    HD_CHECK(r.snapshot == nullptr);
    HD_REQUIRE(r.errors.size() == 2);
    HD_CHECK(r.errors[0].kind == LoadErrorKind::Tsv);
    HD_CHECK(r.errors[0].tsv == TsvErrorKind::TooManyColumns);
    HD_CHECK_EQ(r.errors[0].line, 3);
    HD_CHECK(r.errors[1].tsv == TsvErrorKind::EmptyKeyOrId);
    HD_CHECK_EQ(r.errors[1].file, "a/base.tsv");

    // No column file: no dictionary, the plain files kept.
    files.clear();
    files.push_back(MakeFile("a/plain.tsv", "犬\t개\tn\n"));
    r = Load(std::move(files));
    HD_REQUIRE(r.snapshot != nullptr);
    HD_CHECK(!r.snapshot->HasDictionary());
    HD_CHECK_EQ(r.snapshot->PlainFiles().size(), size_t{1});
}

HD_TEST(SnapshotKeysAndUserDictionary) {
    std::vector<File> files;
    files.push_back(MakeFile("base.tsv", "\xEF\xBB\xBF# comment\n" + kHeader +
                                             "犬\t견\tN110\t\t\tGN\t1\n犬\t개과\tN110\t\t\t\t2\n\xFFX\t깨짐\tN110\t\t\tGN\t3\n"
                                             "#犬\t주석\tN110\t\t\tGN\t4\n"));
    files.push_back(MakeFile("userdict.tsv", kHeader + "犬\t개\tN110\t\t\t\t1\n"));
    const LoadResult r = Load(std::move(files));
    HD_REQUIRE(r.snapshot != nullptr);
    HD_CHECK(r.errors.empty());
    const Snapshot& s = *r.snapshot;
    const LookupContext none;
    // With no prior dictionary, an empty dict code counts as the prior one when a user
    // record meets a base record of the same tag and length: the user record gets XXX.
    HD_CHECK_EQ(Show(s.GetWordInfo("犬だ", LookupMode::General, none)),
                "count 3\n"
                "[犬] 개 N110|XXX|| r0 id0\n"
                "[犬] 견 N110|||GN r0 id1\n"
                "[犬] 개과 N110||| r0 id2");
    // An invalid byte of a key is stored as U+FFFD, as lookups decode it.
    HD_CHECK_EQ(Show(s.GetWordInfo("\xFFX", LookupMode::General, none)), "count 1\n[\xFFX] 깨짐 N110|||GN r0 id3");
    HD_CHECK_EQ(Show(s.GetWordInfo("\xEF\xBF\xBDX", LookupMode::General, none)), "count 1\n[\xEF\xBF\xBDX] 깨짐 N110|||GN r0 id3");
    HD_CHECK_EQ(Show(s.GetWordInfo("#犬", LookupMode::General, none)), "count 0");

    // Plain entries: an entry drops the base and user candidates of its key and class;
    // force entries come first; hidden entries are not indexed.
    CodeTable table;
    std::vector<UserEntry> entries;
    entries.push_back(Entry(table, "犬", "멍멍이", "N110", 1));
    entries.push_back(Entry(table, "犬だ", "개다", "N130", 2));
    entries.push_back(Entry(table, "犬", "강아지", "E300", 3));
    entries.back().force = true;
    entries.push_back(Entry(table, "犬", "가려짐", "N110", 4));
    entries.back().hidden = true;
    const UserText plain(std::move(entries));
    HD_CHECK_EQ(plain.Len(), size_t{3});
    LookupContext ctx;
    ctx.plain = &plain;
    const std::string want =
        "count 3\n"
        "[犬] 강아지 E300|||GN r0 id0 plain\n"
        "[犬だ] 개다 N130|||GN r0 id0 plain\n"
        "[犬] 멍멍이 N110|||GN r0 id0 plain";
    HD_CHECK_EQ(Show(s.GetWordInfo("犬だ", LookupMode::General, ctx)), want);
    ctx.strategy = Strategy::UserFirst;
    HD_CHECK_EQ(Show(s.GetWordInfo("犬だ", LookupMode::General, ctx)), want);
    ctx.strategy = Strategy::LongestMatch;
    ctx.skip = {[](const void*, const UserEntry& e) { return e.force; }, nullptr};
    HD_CHECK_EQ(Show(s.GetWordInfo("犬だ", LookupMode::General, ctx)),
                "count 2\n"
                "[犬だ] 개다 N130|||GN r0 id0 plain\n"
                "[犬] 멍멍이 N110|||GN r0 id0 plain");
}

HD_TEST(SnapshotStrategies) {
    std::vector<File> files;
    files.push_back(MakeFile("base.tsv", kHeader + "犬\t견\tN110\t\t\tGN\t1\n犬小屋\t개집\tN110\t\t\tGN\t2\n"));
    const LoadResult r = Load(std::move(files));
    HD_REQUIRE(r.snapshot != nullptr);
    CodeTable table;
    std::vector<UserEntry> entries;
    entries.push_back(Entry(table, "犬", "강아지", "E300", 1));
    const UserText plain(std::move(entries));
    LookupContext ctx;
    ctx.plain = &plain;
    // A plain entry goes behind longer base candidates, ahead of the others.
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("犬小屋", LookupMode::General, ctx)),
                "count 3\n"
                "[犬小屋] 개집 N110|||GN r0 id2\n"
                "[犬] 강아지 E300|||GN r0 id0 plain\n"
                "[犬] 견 N110|||GN r0 id1");
    ctx.strategy = Strategy::UserFirst;
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("犬小屋", LookupMode::General, ctx)),
                "count 3\n"
                "[犬] 강아지 E300|||GN r0 id0 plain\n"
                "[犬小屋] 개집 N110|||GN r0 id2\n"
                "[犬] 견 N110|||GN r0 id1");
}

HD_TEST(SnapshotNamesAndPlainEntries) {
    const LoadResult r = LoadFolders({Folder()});
    HD_REQUIRE(r.snapshot != nullptr);
    CodeTable table;
    // A name entry goes before the plain entries with a shorter key.
    std::vector<UserEntry> shorter;
    shorter.push_back(Entry(table, "ア", "아", "E300", 1));
    const UserText a(std::move(shorter));
    LookupContext ctx;
    ctx.plain = &a;
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("アリスさん", LookupMode::General, ctx)),
                "count 2\n"
                "[アリス] 앨리스 N130|QPN||GN r0 id0 name\n"
                "[ア] 아 E300|||GN r0 id0 plain");
    // A plain entry with the key removes the name entry.
    std::vector<UserEntry> same;
    same.push_back(Entry(table, "アリス", "아리스", "N130", 1));
    const UserText b(std::move(same));
    ctx.plain = &b;
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("アリスさん", LookupMode::General, ctx)),
                "count 1\n[アリス] 아리스 N130|||GN r0 id0 plain");
}
