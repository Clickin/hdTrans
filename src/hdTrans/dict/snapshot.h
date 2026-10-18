#pragma once
// The dictionaries loaded from an ordered list of dictionary folders, and the
// lookups over them. A Snapshot is immutable once loaded and may be used from any
// number of threads; everything a lookup depends on besides the dictionaries comes
// in its LookupContext.
//
// Loading (Load) reads the top-level *.tsv files of the folders, those of each
// folder in name order:
//   - Column files (their first line that is neither blank nor a "#" comment is a
//     header starting with the key column) make the base dictionary. The senses of
//     a file shadow the senses of earlier files with the same key and class digit.
//   - Column files named words*.tsv are lexicon layers: their senses are added to
//     the senses of earlier files; only a sense with the flag replace shadows the
//     earlier senses of its key and class.
//   - The base senses of a key are ordered by prio, higher first, then by file and
//     line order.
//   - Column files named userdict.tsv make the user dictionary: its records match
//     ahead of the base records and never shadow them.
//   - Column files named names*.tsv make the name layer (see names.cpp): they
//     shadow earlier name files like base files, but never base senses. Senses with
//     a key that holds 、 or 。 or is an inflected word, or with a long Korean, are
//     dropped (Warnings counts them). A sense with the flag replace respells its key:
//     the base senses of the key that name the same place take its Korean, and so do
//     its senses in the layer; the senses of a respelled key in later name files are
//     dropped unless they respell it too or name a person.
//   - The other files (plain dictionaries, readings tables, kana spellings) are kept
//     with their text, in order, for the stages that read them.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hdTrans/dict/codes.h"
#include "hdTrans/dict/dict.h"
#include "hdTrans/dict/tsv.h"
#include "hdTrans/dict/user_text.h"

namespace hdTrans::dict {

// A *.tsv file of a dictionary folder.
struct File {
    int folder = 0;     // index of the folder in load order
    std::string name;   // the folder joined with the file name
    std::string text;
};

enum class LoadErrorKind : uint8_t {
    Folder,      // the folder cannot be listed
    Read,        // the file cannot be read
    Tsv,         // a malformed line or header of a column file (tsv says which)
    UnknownPos,  // a line skipped for a pos that is neither a part code nor a known tag
};

struct LoadError {
    std::string file;
    LoadErrorKind kind = LoadErrorKind::Read;
    int line = 0;  // 1-based; 0 for the whole file
    TsvErrorKind tsv = TsvErrorKind::Read;
};

using KeySet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

// Decides per entry whether a lookup leaves out a plain or name-layer entry.
struct SkipFunc {
    bool (*fn)(const void* context, const UserEntry& e) = nullptr;
    const void* context = nullptr;
    bool operator()(const UserEntry& e) const { return fn && fn(context, e); }
};

// What a lookup depends on besides the snapshot.
struct LookupContext {
    const UserText* plain = nullptr;  // plain-dictionary entries; null for none
    Strategy strategy = Strategy::LongestMatch;
    // Base and user records with this dict code are preferred; empty for none.
    std::string_view priorDict;
    // Name-layer keys confirmed for the text: their name senses take part.
    const KeySet* confirmed = nullptr;
    // Respelled place keys that the text shows to be persons: their senses of the
    // person spelling's sound take it.
    const KeySet* persons = nullptr;
    SkipFunc skip;
};

// One result of a lookup. The views point into the looked-up text, the snapshot or
// the plain entries.
struct Candidate {
    std::string_view source;  // the matched key; empty for the D/I/V sentinel queries
    std::string_view target;
    const Codes* codes = nullptr;
    RuleNumber rule = 0;
    int32_t id = 0;  // the record's id; 0 for user-dictionary records and entries
    // The plain or name-layer entry the candidate was built from; null for base and
    // user-dictionary records.
    const UserEntry* user = nullptr;
};

struct Candidates {
    Candidates() = default;
    Candidates(Candidates&&) = default;
    Candidates& operator=(Candidates&&) = default;
    Candidates(const Candidates&) = delete;
    Candidates& operator=(const Candidates&) = delete;

    std::vector<Candidate> list;
    // The candidate count: user-dictionary matches plus accepted records, plus the
    // plain and name entries taken, minus the candidates they dropped.
    int count = 0;
    // The name-layer entries of the lookup, which list points to.
    std::vector<UserEntry> names;
};

class Snapshot;

struct LoadResult {
    // Null when a file could not be read or holds malformed lines.
    std::shared_ptr<const Snapshot> snapshot;
    // Every error, in file order. With a snapshot, only lines skipped for an unknown
    // pos.
    std::vector<LoadError> errors;
};

// Lists the *.tsv files of the folders in order, those of each folder in name order,
// and reads them.
LoadResult LoadFolders(const std::vector<std::string>& folders);
// Loads files in load order.
LoadResult Load(std::vector<File> files);

class Snapshot {
public:
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    // Whether a column file was loaded.
    bool HasDictionary() const { return hasDictionary_; }
    const Dict& Base() const { return base_; }
    const Dict* User() const { return user_.Len() > 0 ? &user_ : nullptr; }
    const Dict* Names() const { return names_.Len() > 0 ? &names_ : nullptr; }
    // The files that are not column files, in load order.
    const std::vector<File>& PlainFiles() const { return plain_; }
    // The name keys Load dropped, per file.
    const std::vector<std::string>& Warnings() const { return warnings_; }

    // Every candidate whose key is a prefix of text and the candidate count, from the
    // base and user dictionaries and the plain and name entries of ctx.
    //
    // Mode General returns all candidates with tags other than S/T/U/V in one list:
    // user-dictionary matches, then the prior-dictionary buckets in tag order, merged
    // with the ordinary buckets by ascending tag and, within a tag, longer source
    // first. The other modes return only the U, T, S or V bucket; when it is empty,
    // the prior-dictionary bucket of that tag, else the user-dictionary matches. With
    // empty text modes 1..3 query "D", "I", "V".
    //
    // Plain entries whose key prefixes text are placed among those candidates by
    // ctx.strategy, and base candidates with the key and class digit of such an entry
    // are dropped. Name-layer senses that take part are placed like plain entries.
    // Place senses of a key the name layer respells take its Korean, or the person
    // spelling for the keys of ctx.persons.
    Candidates GetWordInfo(std::string_view text, LookupMode mode, const LookupContext& ctx) const;

    // The records of the base dictionary stored under exactly key, in order, with key
    // as their source. With an empty key, modes 1..3 query "D", "I", "V".
    std::vector<Candidate> RetrieveRecords(std::string_view key, LookupMode mode) const;

    // The name-layer records of key, whether or not the key takes part in lookups.
    std::vector<Record> NameRecords(std::string_view key) const;
    // In order of first occurrence, the respelled place keys in text that have a
    // person spelling of the same sound (霧島: 기리시마 as a place, 키리시마 as a person).
    std::vector<std::string> PersonPlaceKeys(std::string_view text) const;
    // The fan reading of the katakana key when the name layer has key as a person
    // written with that sound and the base dictionary reads key no other way; empty
    // otherwise.
    std::string PersonName(std::string_view key) const;
    // Whether a cue in the text may confirm key as a person's name and the name layer
    // has a person sense of it.
    bool PersonCueKey(std::string_view key) const;

    // Bytes held by the snapshot, the texts of the plain files included.
    std::size_t MemoryBytes() const;

private:
    friend LoadResult Load(std::vector<File> files);
    friend struct SnapshotLoader;

    // The Korean a name-layer sense with the flag replace gives its key, and which
    // base senses of the key take it: with sameSound, its place-name senses (QCT or
    // QNN) of the same sound; with allPlaces every place-name sense; with allNouns
    // every noun sense. person is the spelling of a person sense of the key that
    // differs from ko in spelling only, empty for none.
    struct Respelling {
        enum class Mode : uint8_t { SameSound, AllPlaces, AllNouns };
        std::string ko;
        std::string person;
        Mode mode = Mode::AllNouns;
    };

    Snapshot() = default;

    const Respelling* Place(std::string_view key) const;
    std::string_view Respelled(const Respelling* sp, const KeySet* persons, std::string_view key, const Record& r) const;
    void NameEntries(std::string_view text, LookupMode mode, const LookupContext& ctx,
                     const std::vector<const UserEntry*>& owned, std::vector<UserEntry>& out) const;
    bool StrongName(std::string_view s, std::size_t n) const;
    bool AddressedName(std::string_view s, std::size_t n) const;
    bool CuedKey(std::string_view key) const;
    bool PhraseKey(std::string_view key) const;
    bool BaseWord(std::string_view key) const;

    bool hasDictionary_ = false;
    CodeTable codes_;
    Dict base_;
    Dict user_;
    Dict names_;
    std::unordered_map<std::string, Respelling, StringHash, std::equal_to<>> places_;
    std::vector<std::string> warnings_;
    std::vector<File> plain_;
};

}  // namespace hdTrans::dict
