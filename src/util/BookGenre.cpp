#include "BookGenre.h"

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <algorithm>
#include <array>
#include <vector>

#include "ReadingStatsStore.h"

namespace {

constexpr char kPath[] = "/.crosspoint/book_genres.json";

struct Entry {
  std::string path;
  uint8_t genre = BookGenre::UNTAGGED;
};

std::vector<Entry> load() {
  std::vector<Entry> entries;
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(kPath, doc)) return entries;
  const JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  entries.reserve(arr.size());
  for (const JsonObjectConst obj : arr) {
    Entry e;
    e.path = obj["p"] | "";
    e.genre = obj["g"] | static_cast<uint8_t>(BookGenre::UNTAGGED);
    if (!e.path.empty() && e.genre != BookGenre::UNTAGGED) entries.push_back(std::move(e));
  }
  return entries;
}

bool save(const std::vector<Entry>& entries) {
  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& e : entries) {
    JsonObject obj = arr.add<JsonObject>();
    obj["p"] = e.path;
    obj["g"] = e.genre;
  }
  return PersistableStoreBase::writeDocToFile(kPath, doc);
}

}  // namespace

namespace BookGenre {

uint8_t get(const std::string& bookPath) {
  for (const auto& e : load()) {
    if (e.path == bookPath) return e.genre;
  }
  return UNTAGGED;
}

void set(const std::string& bookPath, const uint8_t genre) {
  auto entries = load();
  entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.path == bookPath; }),
                entries.end());
  if (genre != UNTAGGED) entries.push_back(Entry{bookPath, genre});
  if (entries.size() > kMaxBooks) entries.erase(entries.begin(), entries.begin() + (entries.size() - kMaxBooks));
  save(entries);
}

void relocate(const std::string& oldPath, const std::string& newPath) {
  auto entries = load();
  bool changed = false;
  for (auto& e : entries) {
    if (e.path == oldPath) {
      e.path = newPath;
      changed = true;
    }
  }
  if (changed) save(entries);
}

uint8_t dominantRecentGenre(const uint32_t days) {
  const auto entries = load();
  if (entries.empty()) return UNTAGGED;
  const auto& stats = ReadingStatsStore::getInstance();
  const auto& readingDays = stats.getReadingDays();
  if (readingDays.empty()) return UNTAGGED;
  const uint32_t today = readingDays.back().dayOrdinal;
  const uint32_t firstDay = today >= days ? today - days + 1 : 0;

  std::array<uint64_t, GENRE_COUNT> totals{};
  for (const auto& book : stats.getBooks()) {
    uint8_t genre = UNTAGGED;
    for (const auto& e : entries) {
      if (e.path == book.path) {
        genre = e.genre;
        break;
      }
    }
    if (genre == UNTAGGED || genre >= GENRE_COUNT) continue;
    for (const auto& day : book.readingDays) {
      if (day.dayOrdinal >= firstDay) totals[genre] += day.readingMs;
    }
  }
  const auto best = std::max_element(totals.begin() + 1, totals.end());
  return *best > 0 ? static_cast<uint8_t>(best - totals.begin()) : UNTAGGED;
}

}  // namespace BookGenre
