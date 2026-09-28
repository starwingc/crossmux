#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A highlighted (underlined) passage in a book.
//
// Anchors are content positions, not pages, so highlights survive changes of
// font, margins and orientation:
//   EPUB: `spine` is the spine index; start/end are visible-text codepoint
//         offsets within it (TextBlock::wordVisibleOffset, the same
//         coordinate bookmarks use).
//   TXT:  `spine` is 0; start/end are byte offsets in the source file (before
//         any GBK->UTF-8 transcoding).
// The range is half-open: [start, end).
struct HighlightEntry {
  uint16_t spine = 0;
  uint32_t start = 0;
  uint32_t end = 0;
  float percentage = 0.0f;  // book progress at the highlight, for the list
  std::string text;         // excerpt shown in the list (bounded)
};

namespace HighlightFile {

// Excerpts are cut to this many bytes (at a UTF-8 boundary).
constexpr size_t kMaxExcerptBytes = 120;
// A book keeps at most this many highlights; the JSON file stays small enough
// for the shared ArduinoJson path on the C3.
constexpr size_t kMaxHighlights = 200;

std::string getHighlightsDir();
std::string getHighlightPath(const std::string& bookPath);

// Loads the highlights for bookPath, sorted by (spine, start). A missing file
// yields an empty list and returns false.
bool load(const std::string& bookPath, std::vector<HighlightEntry>& highlights);
// Saves; an empty list removes the file.
bool save(const std::string& bookPath, const std::vector<HighlightEntry>& highlights);

// Pure helpers (no I/O).

// Inserts [start, end) in `spine`, merging every highlight it overlaps or
// touches. Returns false when the list is full or the range is empty.
bool add(std::vector<HighlightEntry>& highlights, HighlightEntry entry);
// Index of the highlight covering `offset` in `spine`, or -1.
int find(const std::vector<HighlightEntry>& highlights, uint16_t spine, uint32_t offset);
// Cuts `text` to at most maxBytes without splitting a UTF-8 sequence.
std::string excerpt(const std::string& text, size_t maxBytes = kMaxExcerptBytes);

}  // namespace HighlightFile
