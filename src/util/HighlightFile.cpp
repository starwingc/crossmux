#include "HighlightFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>

std::string HighlightFile::getHighlightsDir() { return "/.crosspoint/highlights/"; }

std::string HighlightFile::getHighlightPath(const std::string& bookPath) {
  // Same flat naming as bookmarks: "/Books/a.epub" -> "Books_a.json".
  std::string name = bookPath.empty() || bookPath[0] != '/' ? bookPath : bookPath.substr(1);
  std::replace(name.begin(), name.end(), '/', '_');
  std::replace(name.begin(), name.end(), '\\', '_');
  const size_t lastDot = name.find_last_of('.');
  if (lastDot != std::string::npos) name.erase(lastDot);
  return getHighlightsDir() + name + ".json";
}

std::string HighlightFile::excerpt(const std::string& text, const size_t maxBytes) {
  if (text.size() <= maxBytes) return text;
  size_t cut = maxBytes;
  // Step back over continuation bytes to the start of the cut character.
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
  return text.substr(0, cut);
}

bool HighlightFile::add(std::vector<HighlightEntry>& highlights, HighlightEntry entry) {
  if (entry.end <= entry.start) return false;
  // Absorb every existing highlight that overlaps or touches the new one.
  for (size_t i = 0; i < highlights.size();) {
    const auto& h = highlights[i];
    if (h.spine == entry.spine && h.start <= entry.end && entry.start <= h.end) {
      if (h.start < entry.start) {
        // The earlier highlight's excerpt starts the merged passage.
        entry.text = h.text;
        entry.percentage = h.percentage;
      }
      entry.start = std::min(entry.start, h.start);
      entry.end = std::max(entry.end, h.end);
      highlights.erase(highlights.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
  if (highlights.size() >= kMaxHighlights) return false;
  entry.text = excerpt(entry.text);
  const auto pos = std::lower_bound(highlights.begin(), highlights.end(), entry,
                                    [](const HighlightEntry& a, const HighlightEntry& b) {
                                      return a.spine != b.spine ? a.spine < b.spine : a.start < b.start;
                                    });
  highlights.insert(pos, std::move(entry));
  return true;
}

int HighlightFile::find(const std::vector<HighlightEntry>& highlights, const uint16_t spine, const uint32_t offset) {
  for (size_t i = 0; i < highlights.size(); ++i) {
    const auto& h = highlights[i];
    if (h.spine == spine && offset >= h.start && offset < h.end) return static_cast<int>(i);
  }
  return -1;
}

bool HighlightFile::load(const std::string& bookPath, std::vector<HighlightEntry>& highlights) {
  highlights.clear();
  // Read/write go through PersistableStoreBase so the JSON parser and
  // serializer stay instantiated once, in PersistableStore.cpp.
  const std::string path = getHighlightPath(bookPath);
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), doc)) return false;

  const JsonArrayConst arr = doc["highlights"].as<JsonArrayConst>();
  highlights.reserve(std::min<size_t>(arr.size(), kMaxHighlights));
  for (const JsonObjectConst obj : arr) {
    if (highlights.size() >= kMaxHighlights) break;
    HighlightEntry entry;
    entry.spine = obj["si"] | static_cast<uint16_t>(0);
    entry.start = obj["s"] | static_cast<uint32_t>(0);
    entry.end = obj["e"] | static_cast<uint32_t>(0);
    entry.percentage = obj["p"] | 0.0f;
    entry.text = excerpt(obj["t"] | "");
    if (entry.end > entry.start) highlights.push_back(std::move(entry));
  }
  std::sort(highlights.begin(), highlights.end(), [](const HighlightEntry& a, const HighlightEntry& b) {
    return a.spine != b.spine ? a.spine < b.spine : a.start < b.start;
  });
  LOG_DBG("HLT", "Loaded %u highlights", static_cast<unsigned>(highlights.size()));
  return true;
}

bool HighlightFile::save(const std::string& bookPath, const std::vector<HighlightEntry>& highlights) {
  const std::string path = getHighlightPath(bookPath);
  if (highlights.empty()) {
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
    return true;
  }
  JsonDocument doc;
  JsonArray arr = doc["highlights"].to<JsonArray>();
  for (const auto& h : highlights) {
    JsonObject obj = arr.add<JsonObject>();
    obj["si"] = h.spine;
    obj["s"] = h.start;
    obj["e"] = h.end;
    obj["p"] = h.percentage;
    obj["t"] = h.text;
  }
  // writeDocToFile ensures /.crosspoint; the highlights subdirectory is ours.
  Storage.mkdir(getHighlightsDir().c_str());
  return PersistableStoreBase::writeDocToFile(path.c_str(), doc);
}
