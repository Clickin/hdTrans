#pragma once
// Output text as pieces: runs of text, and the texts of protected spans, which the
// cleanup leaves as they are.
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::output {

// Span kinds of a piece.
enum SpanKind : int {
    SpanText = 0,   // translated text
    SpanWord = 1,   // Hangul or foreign text, read as a noun
    SpanOpen = 2,   // tag, entity, host marker, stutter marks
    SpanClose = 3,  // closing tag
};

struct Piece {
    std::string text;
    SpanKind span = SpanText;
};

// No text piece is empty and no two text pieces follow each other; Add and Append
// keep that.
using Pieces = std::vector<Piece>;

std::string String(const Pieces& p);

// Appends text, joining it to a text piece at the end.
void Add(Pieces& p, std::string_view text);

// Appends the pieces of q.
void Append(Pieces& p, const Pieces& q);
void Append(Pieces& p, Pieces&& q);

// Splits only text pieces at newlines, preserving empty lines and protected spans.
std::vector<Pieces> Lines(const Pieces& text);
Pieces JoinLines(const std::vector<Pieces>& lines);

// The first piece when it is text, otherwise "".
std::string_view Head(const Pieces& p);

// Drops the ASCII spaces that end the last piece when it is text; the piece goes
// when nothing is left of it.
void TrimRight(Pieces& p);

// Drops the ASCII spaces that start the first piece when it is text; the piece
// goes when nothing is left of it.
void TrimLeft(Pieces& p);

}  // namespace hdTrans::output
