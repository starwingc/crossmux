#pragma once
#include <string>

// Per-book reader font: each book remembers the font family and point size it
// was last read with, and reopens with them. A book without an entry keeps the
// current (last used) font, so new books start from whatever was read last.
//
// Stored in /.crosspoint/book_fonts.json, most recent last, capped at
// kMaxBooks entries (the oldest are dropped).
namespace BookFontMemory {

constexpr size_t kMaxBooks = 200;

// Before the reader loads its font: switch SETTINGS to the book's remembered
// font. Returns true when a setting changed (and was saved).
bool applyFor(const std::string& bookPath);

// When the reader closes: record the current font for this book.
void rememberFor(const std::string& bookPath);

// Keep the entry when a book file is renamed or moved.
void relocate(const std::string& oldPath, const std::string& newPath);

}  // namespace BookFontMemory
