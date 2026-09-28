#include "BookFontMemory.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"

namespace {

constexpr char kPath[] = "/.crosspoint/book_fonts.json";

struct Entry {
  std::string path;
  std::string sdFamily;  // empty: a built-in family
  uint8_t fontFamily = 0;
  uint8_t pointSize = 0;
};

// Read/write go through PersistableStoreBase so the JSON parser and serializer
// stay instantiated once, in PersistableStore.cpp.
std::vector<Entry> load() {
  std::vector<Entry> entries;
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(kPath, doc)) return entries;
  const JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  entries.reserve(arr.size());
  for (const JsonObjectConst obj : arr) {
    Entry e;
    e.path = obj["p"] | "";
    e.sdFamily = obj["sd"] | "";
    e.fontFamily = obj["ff"] | static_cast<uint8_t>(0);
    e.pointSize = obj["pt"] | static_cast<uint8_t>(0);
    if (!e.path.empty() && e.pointSize != 0) entries.push_back(std::move(e));
  }
  return entries;
}

bool save(const std::vector<Entry>& entries) {
  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& e : entries) {
    JsonObject obj = arr.add<JsonObject>();
    obj["p"] = e.path;
    if (!e.sdFamily.empty()) obj["sd"] = e.sdFamily;
    obj["ff"] = e.fontFamily;
    obj["pt"] = e.pointSize;
  }
  return PersistableStoreBase::writeDocToFile(kPath, doc);
}

Entry current(const std::string& bookPath) {
  Entry e;
  e.path = bookPath;
  e.sdFamily = SETTINGS.sdFontFamilyName;
  e.fontFamily = SETTINGS.fontFamily;
  e.pointSize = SETTINGS.fontPointSize;
  return e;
}

bool sameFont(const Entry& a, const Entry& b) {
  return a.sdFamily == b.sdFamily && a.pointSize == b.pointSize &&
         (!a.sdFamily.empty() || a.fontFamily == b.fontFamily);
}

}  // namespace

bool BookFontMemory::applyFor(const std::string& bookPath) {
  const auto entries = load();
  for (const auto& e : entries) {
    if (e.path != bookPath) continue;
    if (sameFont(e, current(bookPath))) return false;
    LOG_INF("BFM", "Book font: %s %u pt", e.sdFamily.empty() ? "built-in" : e.sdFamily.c_str(),
            static_cast<unsigned>(e.pointSize));
    strncpy(SETTINGS.sdFontFamilyName, e.sdFamily.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    SETTINGS.fontFamily = e.fontFamily;
    SETTINGS.fontPointSize = e.pointSize;
    // The Flash font cache holds one family; its header check rejects a
    // different one and the loader falls back to the SD file, so the preload
    // flag can stay as it is.
    SETTINGS.saveToFile();
    return true;
  }
  return false;
}

void BookFontMemory::rememberFor(const std::string& bookPath) {
  if (bookPath.empty()) return;
  auto entries = load();
  const Entry now = current(bookPath);
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].path != bookPath) continue;
    // Unchanged and already the most recent: nothing to write.
    if (sameFont(entries[i], now) && i + 1 == entries.size()) return;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
    break;
  }
  entries.push_back(now);
  if (entries.size() > kMaxBooks) {
    entries.erase(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(entries.size() - kMaxBooks));
  }
  if (!save(entries)) LOG_ERR("BFM", "Failed to save book fonts");
}

void BookFontMemory::relocate(const std::string& oldPath, const std::string& newPath) {
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
