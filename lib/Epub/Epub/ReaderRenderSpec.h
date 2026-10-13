#pragma once
#include <cstdint>

#include "FirstLineIndent.h"

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on every field: a section file built
// with a different spec is discarded and rebuilt.
//
// Build one via CrossPointSettings::readerRenderSpec(width, height), which
// fills settings and viewport fields. The reader supplies collectTouchLinks
// from the input device and applies any session-only CSS fallback. Taking
// the viewport as arguments is what keeps a spec from
// existing in a half-filled state — the 0 defaults below are a last-resort
// backstop (a 0x0 viewport lays out nothing), not an invitation to omit it.
struct ReaderRenderSpec {
  int fontId = 0;
  float lineCompression = 1.0f;
  uint8_t extraParagraphSpacing = 0;  // percent of the line height, 0 = off
  // Reader-level first-line indent, one of FirstLineIndent::Auto/Indent/
  // NoIndent (see ParsedText.h). Independent of extraParagraphSpacing:
  // paragraphs keep their indent even when extra paragraph spacing is
  // enabled.
  uint8_t firstLineIndent = FirstLineIndent::Auto;
  int8_t characterSpacing = 0;
  uint8_t wordSpacingPercent = 100;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  bool collectTouchLinks = false;
  // Layout-affecting renderer options (bit flags below); part of the section cache key.
  static constexpr uint8_t LAYOUT_CJK_FULL_WIDTH_QUOTES = 0x01;
  // Bits 1..3: reader-drawn chapter title style (0 = the book's own headings).
  static constexpr uint8_t LAYOUT_CHAPTER_TITLE_SHIFT = 1;
  uint8_t layoutFlags = 0;
  uint8_t chapterTitleStyle() const { return (layoutFlags >> LAYOUT_CHAPTER_TITLE_SHIFT) & 0x07; }
};
