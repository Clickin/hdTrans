#pragma once
// The sound stage of the text path: sounds, elongation and stutter written before the
// line is translated, and the spoken spellings inside words that need the dictionary
// to be recognised.
//
// A sound of the builtin sound lexicon (sound.tsv, and the interjections in kana of
// phrases.tsv) is written in Korean, so the protect stage makes it a Hangul span that
// is read as a noun. Before する an ono row written KR/lemma is written as the lemma
// without 하다 (ぼーっとした 멍했다). Elongation after the sound (はぁぁぁ, うわぁー,
// ああああん, きゃああっ) lengthens the Korean (하아아아, 우와아아, 아아아앙, 꺄아악).
//
// Where a sound may stand:
//   - a katakana run is taken only when sounds cover it completely, and when they are
//     different, with keys of four or more characters (ドキドキワクワク, but not the
//     name シャーカーン); after a kanji the first key needs three or more characters;
//   - a hiragana sound starts its hiragana run (after a kanji, katakana, letter or
//     digit only when it is an ono row with a key of four or more characters that does
//     not start with a particle), follows another sound, or, for ono rows, follows a
//     lone particle; the text between sounds is read as the longest dictionary words;
//   - sounds in a row are written with a space between those of two or more syllables
//     (あっあっ 앗앗, うふふふっ 우후후훗, もうやだ 정말 싫어);
//   - an intj row must not be followed by kana, except a hiragana one by katakana
//     (はわわユウキ); an ono row may be followed by anything, and と or the て of って
//     after it is dropped (ぎゅっと 꽉). A space separates a sound from a following
//     kanji or verb (ぽりぽりかく), and after a dropped particle from kana, but not from
//     a form of する;
//   - a lead row (おい、 ほら！ ちょっと…) is a sound only at the start of an utterance
//     and before a pause, and then wins over the dictionary words;
//   - a dictionary word longer than the sound with its elongation, or as long when
//     there is elongation (パターン is not ぱたん lengthened), and a plain-dictionary
//     entry at least as long as the sound's key (without a final と), win.
//
// A stutter (な、なにを, だっ、だめ, べ…別に) becomes a stutter mark U+E000 before each
// separator; the restore stage writes it as the first syllable of the next Korean word
// (WriteStutters).
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "core/dict/snapshot.h"
#include "core/pipeline/line_context.h"

namespace hdTrans::pipeline {

// The normalized line after the sound stage: CompleteCutOffs, WriteSounds, then RewriteCasualAdjectives;
// when RewriteEmphasis changes that text, WriteSounds of the rewritten text instead.
// Markup tags, BBCode tags, entities and ctx.markers are left as they are. The lookups
// use ctx.lookup.
std::string Sound(std::string_view normalized, const LineContext& ctx, const dict::Snapshot& snap);

namespace internal {

// The sounds of text in Korean and each stutter as stutter marks before its separators
// (see the comment at the top). Invalid UTF-8 bytes are kept and read as neither kana
// nor letters.
std::string WriteSounds(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// A verb cut off before a dash or an ellipsis (― — … ‥, ・・ or ーー) completed to the form
// it starts, when the dictionary has the verb with its kanji: a part of the te form to the
// te form (待っ―― 待って――, 書い…… 書いて……), the negative stem with な to ない
// (行かな…… 行かない……). A stem or a fragment without kanji stays.
std::string CompleteCutOffs(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The casual adjective endings -ee and -ii of -ai, -oi, -ui (うるせぇ, すげー, わりぃ,
// 冷て～) written in full (うるさい, すごい, わるい, 冷たい) before the end of a clause or a
// sentence particle, when the dictionary has the stem, starting a word, as an
// adjective. Not after っ or ん, not the particle ぜ (歌おうぜぇ), not a て form
// (届いてぇ; 冷てぇ and いてぇ are adjectives).
std::string RewriteCasualAdjectives(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// The emphasis that spoken text writes inside a word dropped when the dictionary then
// reads the word: a っ before a kana that does not start a て or た form (まっかせて,
// いっくぞ, 負っける), and a run of ー, ～ or small vowels that lengthens the kana before
// it (やーめーて, うるさ～い, 見ぃつけた, 勝ぁああつ), dropped or written as the long vowel
// (どーだ どうだ, おにーちゃん); a run of two or more with a small vowel after a
// dictionary word before a shout's ッ, ！ or ？ is dropped (なんだそりゃああぁあッ). A
// lengthened vowel kana (あぁ) or particle (か～, なぁー), a mark that a word or a sound
// holds (もっと, せーの, おーい) and a lengthened nickname (うーちゃん) are left alone. The
// rewrite is kept when a dictionary key then reaches further past the mark than before
// or, when no key reached past it, after a kanji or, for a lengthening mark, a verb or
// adjective stem. A ー or ～ that ends a volitional form after a verb stem is written う
// (いこー いこう). Returns text itself when nothing changes.
std::string RewriteEmphasis(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap);

// korean with the elongation units of a sound match: 'v' for each lengthening
// character, '~' for each run of ～, '!' for a closing ッ. When the last syllable
// carries the sound (the only syllable, or one without an initial consonant: 앙, 하아,
// 우왓), each 'v' adds a syllable of its vowel, at most four, before the final
// consonant if it has one (앙 아앙, 꺅 꺄악), and '!' gives it the final ㅅ when it has none
// (하아 하앗); other words take one ~ instead (정말 정말~). '~' adds ~.
std::string Lengthen(std::string_view korean, std::string_view units);

// A row of the sound lexicon; key in katakana.
struct SoundRow {
    std::u32string key;
    std::string korean;
    // The lemma of a KR/lemma row without a final 하다, written before する; empty for
    // rows without a lemma.
    std::string root;
    bool intj = false;    // an intj or lead row; the other rows are ono rows
    bool phrase = false;  // a row of phrases.tsv
    bool lead = false;    // an interjection only at the start of an utterance, before a pause
};

// The rows of the sound lexicon whose key starts with the katakana c, longer keys
// first, rows of equal length in file order (sound.tsv, then phrases.tsv).
std::span<const SoundRow> SoundRows(char32_t c, bool extra = false);

// Whether key (katakana) is the key of a row of sound.tsv.
bool IsSoundKey(std::u32string_view key, bool extra = false);

// The characters of s; an invalid UTF-8 byte b is kInvalidByte + b.
constexpr char32_t kInvalidByte = 0x110000;
std::u32string DecodeText(std::string_view s);
// The inverse of DecodeText.
std::string EncodeText(std::u32string_view rs);
// rs with the hiragana ぁ..ゖ in katakana.
std::u32string ToKatakana(std::u32string_view rs);

// The end of a match of row at f[p:] (f in katakana) and its elongation units (see
// Lengthen), or false. In a key of three or more characters a small vowel also matches
// the plain one (ふわぁ, ふわあ). Elongation may follow the key, or come before its last
// character when that is ン or ッ; ッ may close it when no kana but ト or テ follows.
bool MatchSound(const SoundRow& row, std::u32string_view f, size_t p, size_t& end, std::string& units);

}  // namespace internal

}  // namespace hdTrans::pipeline
