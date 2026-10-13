#pragma once
#include <cstdint>
#include <string>

// Each book's broad genre. Drives the Pixel-theme study cat's class (the genre
// read most lately). Tagged when a book is first opened or when it is uploaded.
//
// Stored in /.crosspoint/book_genres.json, most recent last, capped at
// kMaxBooks entries (the oldest are dropped).
namespace BookGenre {

enum Genre : uint8_t {
  UNTAGGED = 0,
  FANTASY = 1,  // 奇幻 · 玄幻 -> mage
  WUXIA = 2,    // 武侠 · 仙侠 -> swordsman
  HISTORY = 3,  // 西幻 · 历史 -> knight
  MYSTERY = 4,  // 推理 · 悬疑 -> detective
  ROMANCE = 5,  // 言情 · 诗文 -> bard
  SCIFI = 6,    // 科幻 · 赛博 -> mechanic
  GENRE_COUNT,
  DECLINED = 0xFF  // asked once, user skipped: don't ask again
};

constexpr size_t kMaxBooks = 400;

uint8_t get(const std::string& bookPath);
void set(const std::string& bookPath, uint8_t genre);
// Keep the entry when a book file is renamed or moved.
void relocate(const std::string& oldPath, const std::string& newPath);

// The genre with the most reading time over the last `days` days (ReadingStatsStore),
// UNTAGGED when nothing tagged was read.
uint8_t dominantRecentGenre(uint32_t days = 14);

}  // namespace BookGenre
