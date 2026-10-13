#include "PixelRoom.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "CrossPointSettings.h"
#include "PixelRoomArt.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookGenre.h"
#include "util/TimeUtils.h"

namespace PixelRoom {
namespace {

constexpr int kCell = 4;  // one art cell = 4x4 screen px
constexpr int kCatX = 104;
constexpr int kCatY = 640;
constexpr int kCatCells = 20;

constexpr std::array<Spot, 9> kSpots = {{
    {22, 64, 164, 168, 70, 236, StrId::STR_ROOM_WINDOW, StrId::STR_ROOM_SLEEP, Action::Sleep},
    {216, 68, 80, 112, 222, 182, StrId::STR_ROOM_CLOCK, StrId::STR_ROOM_STATS, Action::Stats},
    {316, 56, 144, 120, 352, 178, StrId::STR_ROOM_BOARD, StrId::STR_ROOM_RECENT, Action::Recent},
    {8, 244, 160, 296, 56, 516, StrId::STR_ROOM_SHELF, StrId::STR_ROOM_LIBRARY, Action::Library},
    {372, 290, 84, 120, 380, 268, StrId::STR_ROOM_LAMP, StrId::STR_ROOM_SETTINGS, Action::Settings},
    {252, 352, 120, 60, 262, 326, StrId::STR_ROOM_BOOK, StrId::STR_ROOM_READ, Action::Read},
    {196, 316, 60, 96, 190, 296, StrId::STR_ROOM_RADIO, StrId::STR_ROOM_TRANSFER, Action::Transfer},
    {284, 540, 186, 164, 330, 520, StrId::STR_ROOM_CABINET, StrId::STR_ROOM_APPS, Action::Apps},
    {88, 628, 120, 96, 108, 604, StrId::STR_ROOM_CAT, StrId::STR_ROOM_CHECKIN, Action::Cat},
}};

// --- study cat sprites (20x20 cells, '#' = ink, '_' = force paper) ----------
using Grid = std::array<std::array<bool, kCatCells>, kCatCells>;

constexpr const char* kSleep[kCatCells] = {
    "....................", "..............ZZZ...", "................Z...", "...............Z....",
    "..............ZZZ...", "....................", "....................", "....#......#........",
    "....##....##........", "...#..####..#.......", "...#........#.......", "...#.##..##.#.......",
    "..#..........#####..", "..#...............#.", "..#...............#.", "..#...............#.",
    "..#..............#..", "...##############...", "....................", "....................",
};
constexpr const char* kSit[kCatCells] = {
    "....................", "....................", "....................", "....#........#......",
    "....##......##......", "....#.#....#.#......", "....#..####..#......", "....#........#......",
    "....#.##..##.#......", "....#........#......", "....#...##...#......", ".....#......#.......",
    ".....########.......", "....#........#...#..", "....#........#..#...", "....#........#.#....",
    "....#........##.....", "....#.##..##.#......", "....##########......", "....................",
};

struct Layer {
  int8_t x, y;
  uint8_t rows;
  const char* const* data;
};

Grid fromRows(const char* const* rows) {
  Grid g{};
  for (int y = 0; y < kCatCells; ++y)
    for (int x = 0; x < kCatCells; ++x) g[y][x] = rows[y][x] != '.';
  return g;
}

void paint(Grid& g, const Layer& layer) {
  for (int j = 0; j < layer.rows; ++j) {
    const char* row = layer.data[j];
    for (int i = 0; row[i]; ++i) {
      const int x = layer.x + i;
      const int y = layer.y + j;
      if (row[i] == '.' || x < 0 || y < 0 || x >= kCatCells || y >= kCatCells) continue;
      g[y][x] = row[i] != '_';
    }
  }
}

#define LAYER(X, Y, ...)                                                         \
  [] {                                                                           \
    static constexpr const char* rows[] = {__VA_ARGS__};                         \
    return Layer{X, Y, static_cast<uint8_t>(std::size(rows)), rows};            \
  }()

// Classless forms (no tagged genre read lately).
void plainLayers(Grid& g, const uint8_t level) {
  if (level >= 2) {
    paint(g, LAYER(14, 9, "....#", "...#.", "...#.", "..#..", ".#..."));  // tail up
    paint(g, LAYER(15, 1, "#.#", "###", ".#."));                          // heart
  }
  if (level >= 3) {
    paint(g, LAYER(4, 8, ".##.##.###", ".##.##.##."));                    // glasses
    paint(g, LAYER(3, 15, "##########", "#...##...#", "##########"));     // book
  }
  if (level >= 4) paint(g, LAYER(4, 12, "##########", "##########", "........##", "........##"));  // scarf
  if (level >= 5) {
    paint(g, LAYER(5, 0, "#.#.#...", "######..", "######.."));            // crown
    paint(g, LAYER(2, 19, "################"));                           // book stack
  }
}

// Per-class gear: [0] from LV.1, [1] from LV.3, [2] at LV.5.
void classLayers(Grid& g, const uint8_t genre, const uint8_t level) {
  const int tiers = level >= 5 ? 3 : (level >= 3 ? 2 : 1);
  for (int t = 0; t < tiers; ++t) {
    switch (genre) {
      case BookGenre::FANTASY:  // mage: hat, staff, cape + sparkles
        if (t == 0) paint(g, LAYER(3, 0, "......##....", ".....###....", "....#####...", "############"));
        if (t == 1)
          paint(g, LAYER(15, 3, ".#.", "###", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.",
                         ".#.", ".#.", ".#.", ".#."));
        if (t == 2) {
          paint(g, LAYER(2, 12, "##", "##", "##", "##", "##", "##", "##"));
          paint(g, LAYER(0, 1, ".#.", "###", ".#."));
        }
        break;
      case BookGenre::WUXIA:  // swordsman: bamboo hat, sword, sash + qi
        if (t == 0) paint(g, LAYER(2, 1, ".....###......", "...#######....", ".###########..", "##############"));
        if (t == 1) paint(g, LAYER(14, 4, "_....#", "....#.", "...#..", ".###..", "..#...", ".#...."));
        if (t == 2) {
          paint(g, LAYER(4, 12, "##########", "##########", ".........#", "..........#"));
          paint(g, LAYER(0, 6, "#.", ".#", "..", "#.", ".#"));
        }
        break;
      case BookGenre::HISTORY:  // knight: helmet, shield, lance + crest
        if (t == 0) {
          paint(g, LAYER(4, 3, "##########", "##########", "##########", "##########", "##########",
                         "#._.__._.#"));
          paint(g, LAYER(8, 0, "..#", ".##", "###"));
        }
        if (t == 1) paint(g, LAYER(0, 11, "#####", "#.#.#", "#####", "#.#.#", ".###.", "..#.."));
        if (t == 2)
          paint(g, LAYER(15, 2, ".#", "##", ".#", ".#", ".#", ".#", ".#", ".#", ".#", ".#", ".#", ".#", ".#",
                         ".#", ".#", ".#"));
        break;
      case BookGenre::MYSTERY:  // detective: deerstalker, magnifier, bow tie + pipe
        if (t == 0) paint(g, LAYER(3, 1, "..########..", ".##########.", "############", "..........##"));
        if (t == 1) paint(g, LAYER(0, 12, ".###.", "#...#", "#...#", "#...#", ".###.", "....#", ".....#"));
        if (t == 2) {
          paint(g, LAYER(6, 12, "##..##", ".####."));
          paint(g, LAYER(12, 9, "....###", "....#.#", "####..."));
        }
        break;
      case BookGenre::ROMANCE:  // bard: feathered cap, lute, notes
        if (t == 0) paint(g, LAYER(4, 0, ".......##.", "......##..", ".#######..", "##########"));
        if (t == 1) paint(g, LAYER(13, 8, ".....#", "....#.", "...#..", ".###..", "#####.", "#####.", ".###.."));
        if (t == 2) {
          paint(g, LAYER(0, 2, ".##", ".#.", "##.", "##."));
          paint(g, LAYER(16, 0, ".##", ".#.", "##.", "##."));
        }
        break;
      case BookGenre::SCIFI:  // mechanic: goggles, wrench, antenna + arm
        if (t == 0) paint(g, LAYER(3, 5, "############", ".###....###.", ".###....###."));
        if (t == 1) paint(g, LAYER(14, 13, ".#.#", ".###", "..#.", "..#.", ".###"));
        if (t == 2) {
          paint(g, LAYER(8, 0, "..#.", ".###", "..#.", "..#."));
          paint(g, LAYER(0, 11, "###", "#.#", "###", "#.#", "###"));
        }
        break;
      default:
        break;
    }
  }
}

#undef LAYER

void drawCat(const GfxRenderer& r, const CatState& cat, const int originX = kCatX, const int originY = kCatY) {
  Grid g = cat.level == 0 ? fromRows(kSleep) : fromRows(kSit);
  if (cat.level > 0) {
    if (cat.genre > BookGenre::UNTAGGED && cat.genre < BookGenre::GENRE_COUNT)
      classLayers(g, cat.genre, cat.level);
    else
      plainLayers(g, cat.level);
  }
  r.fillRect(originX, originY, kCatCells * kCell, kCatCells * kCell, false);
  for (int y = 0; y < kCatCells; ++y)
    for (int x = 0; x < kCatCells; ++x)
      if (g[y][x]) r.fillRect(originX + x * kCell, originY + y * kCell, kCell, kCell, true);
}

void drawRoomArt(const GfxRenderer& r) {
  using namespace PixelRoomArt;
  for (int y = 0; y < kRows; ++y) {
    const uint8_t* row = kRoom + y * kRowBytes;
    int x = 0;
    while (x < kCols) {
      if (!((row[x >> 3] >> (7 - (x & 7))) & 1)) {
        ++x;
        continue;
      }
      int run = 1;
      while (x + run < kCols && ((row[(x + run) >> 3] >> (7 - ((x + run) & 7))) & 1)) ++run;
      r.fillRect(x * kCell, y * kCell, run * kCell, kCell, true);
      x += run;
    }
  }
}

void drawTag(const GfxRenderer& r, const int x, const int y, const char* text, const bool selected) {
  const int w = r.getTextWidth(SMALL_FONT_ID, text) + 12;
  const int h = r.getLineHeight(SMALL_FONT_ID) + 4;
  if (selected) {
    r.fillRoundedRect(x, y, w, h, 6, Color::Black);
  } else {
    r.fillRoundedRect(x, y, w, h, 6, Color::White);
    r.drawRoundedRect(x, y, w, h, 1, 6, true);
  }
  r.drawText(SMALL_FONT_ID, x + 6, y + 2, text, !selected);
}

void drawDashedFrame(const GfxRenderer& r, const Spot& s) {
  for (int i = 0; i < 3; ++i) {
    for (int x = s.x; x < s.x + s.w; x += 8) {
      r.fillRect(x, s.y + i, std::min(4, s.x + s.w - x), 1, true);
      r.fillRect(x, s.y + s.h - 1 - i, std::min(4, s.x + s.w - x), 1, true);
    }
    for (int y = s.y; y < s.y + s.h; y += 8) {
      r.fillRect(s.x + i, y, 1, std::min(4, s.y + s.h - y), true);
      r.fillRect(s.x + s.w - 1 - i, y, 1, std::min(4, s.y + s.h - y), true);
    }
  }
}

void drawStatusBar(const GfxRenderer& r) {
  const int w = r.getScreenWidth();
  r.fillRect(0, 0, w, 44, false);
  char clock[16];
  if (TimeUtils::formatCurrentTime(clock, sizeof(clock), SETTINGS.clockFormat == 1)) {
    r.drawText(UI_12_FONT_ID, 16, (44 - r.getLineHeight(UI_12_FONT_ID)) / 2, clock);
  }
  const uint32_t now = TimeUtils::getAuthoritativeTimestamp();
  if (TimeUtils::isClockValid(now)) {
    const std::string date = TimeUtils::formatDate(now);
    const int dw = r.getTextWidth(UI_10_FONT_ID, date.c_str());
    r.drawText(UI_10_FONT_ID, (w - dw) / 2, (44 - r.getLineHeight(UI_10_FONT_ID)) / 2, date.c_str());
  }
  GUI.drawBatteryRight(r, Rect{w - 16 - 60, 12, 60, 20},
                       SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
}

}  // namespace

int spotCount() { return static_cast<int>(kSpots.size()); }
const Spot& spot(const int index) { return kSpots[std::clamp(index, 0, spotCount() - 1)]; }

int spotAt(const int x, const int y) {
  for (int i = spotCount() - 1; i >= 0; --i) {
    const Spot& s = kSpots[i];
    if (x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h) return i;
  }
  return -1;
}

uint8_t levelForStreak(const uint32_t d) {
  return d >= 30 ? 5 : d >= 14 ? 4 : d >= 7 ? 3 : d >= 3 ? 2 : d >= 1 ? 1 : 0;
}

void draw(const GfxRenderer& r, const int selected, const CatState& cat) {
  drawRoomArt(r);
  drawStatusBar(r);
  drawCat(r, cat);
  for (int i = 0; i < spotCount(); ++i) {
    drawTag(r, kSpots[i].tagX, kSpots[i].tagY, I18N.get(kSpots[i].feature), i == selected);
  }
  if (selected < 0 || selected >= spotCount()) return;
  const Spot& s = kSpots[selected];
  drawDashedFrame(r, s);

  // Caption bar: "台灯 → 设置"
  const int bx = 16;
  const int by = r.getScreenHeight() - 58;
  const int bw = r.getScreenWidth() - 32;
  constexpr int bh = 44;
  r.fillRect(bx + 4, by + 4, bw, bh, true);
  r.fillRect(bx, by, bw, bh, false);
  r.drawRoundedRect(bx, by, bw, bh, 2, 8, true);
  const std::string caption = std::string(I18N.get(s.object)) + "  ·  " + I18N.get(s.feature);
  r.drawText(UI_12_FONT_ID, bx + 14, by + (bh - r.getLineHeight(UI_12_FONT_ID)) / 2, caption.c_str());
}

void drawSleepScene(const GfxRenderer& r, const CatState& cat) {
  drawRoomArt(r);
  CatState asleep = cat;
  asleep.level = 0;
  drawCat(r, asleep);

  // Date across the top band.
  r.fillRect(0, 0, r.getScreenWidth(), 44, false);
  const uint32_t now = TimeUtils::getAuthoritativeTimestamp();
  if (TimeUtils::isClockValid(now)) {
    const std::string date = TimeUtils::formatDate(now);
    const int dw = r.getTextWidth(UI_12_FONT_ID, date.c_str());
    r.drawText(UI_12_FONT_ID, (r.getScreenWidth() - dw) / 2, (44 - r.getLineHeight(UI_12_FONT_ID)) / 2, date.c_str(),
               true, EpdFontFamily::BOLD);
  }

  // Card over the floor: who sleeps here and what was read today.
  const int x = 220;
  const int y = 600;
  const int w = r.getScreenWidth() - x - 16;
  const int h = 150;
  r.fillRect(x + 5, y + 5, w, h, true);
  r.fillRect(x, y, w, h, false);
  r.drawRoundedRect(x, y, w, h, 2, 10, true);
  static constexpr StrId kNames[] = {StrId::STR_CAT_CLASS_NONE,      StrId::STR_CAT_CLASS_MAGE,
                                     StrId::STR_CAT_CLASS_SWORDSMAN, StrId::STR_CAT_CLASS_KNIGHT,
                                     StrId::STR_CAT_CLASS_DETECTIVE, StrId::STR_CAT_CLASS_BARD,
                                     StrId::STR_CAT_CLASS_MECHANIC};
  const uint8_t genre = cat.genre < BookGenre::GENRE_COUNT ? cat.genre : 0;
  char line[96];
  snprintf(line, sizeof(line), "%s  LV.%u", I18N.get(kNames[genre]), cat.level);
  r.drawText(UI_12_FONT_ID, x + 14, y + 12, line, true, EpdFontFamily::BOLD);
  snprintf(line, sizeof(line), tr(STR_CAT_STREAK_FMT), static_cast<unsigned>(cat.streakDays));
  r.drawText(UI_10_FONT_ID, x + 14, y + 56, line);
  snprintf(line, sizeof(line), tr(STR_CAT_TODAY_FMT), static_cast<unsigned>(cat.todayMinutes),
           static_cast<unsigned>(cat.goalMinutes));
  r.drawText(UI_10_FONT_ID, x + 14, y + 86, line);
  r.drawText(SMALL_FONT_ID, x + 14, y + h - 30, tr(STR_SLEEPING));
}

namespace {
// 5x7 block letters for the splash title.
struct Glyph {
  char ch;
  const char* rows[7];
};
constexpr Glyph kLetters[] = {
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
};

void drawBlockText(const GfxRenderer& r, const char* text, const int cx, const int y, const int scale) {
  const int n = static_cast<int>(strlen(text));
  const int advance = 6 * scale;
  int x = cx - (n * advance - scale) / 2;
  for (const char* p = text; *p; ++p, x += advance) {
    for (const auto& g : kLetters) {
      if (g.ch != *p) continue;
      for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
          if (g.rows[row][col] == '#') r.fillRect(x + col * scale, y + row * scale, scale, scale, true);
    }
  }
}
}  // namespace

void drawBootScene(const GfxRenderer& r, const char* version) {
  drawRoomArt(r);
  CatState sleepy;
  drawCat(r, sleepy);
  const int w = r.getScreenWidth();
  r.fillRect(0, 0, w, 44, false);

  const int cardX = 40;
  const int cardY = 548;
  const int cardW = w - 80;
  const int cardH = 196;
  r.fillRect(cardX + 6, cardY + 6, cardW, cardH, true);
  r.fillRect(cardX, cardY, cardW, cardH, false);
  r.drawRoundedRect(cardX, cardY, cardW, cardH, 3, 12, true);
  drawBlockText(r, "CROSSMUX", w / 2, cardY + 26, 6);
  const char* tagline = tr(STR_PIXEL_BOOT_TAGLINE);
  r.drawText(UI_10_FONT_ID, (w - r.getTextWidth(UI_10_FONT_ID, tagline)) / 2, cardY + 94, tagline);
  for (int i = 0; i < 5; ++i) {  // loading blocks
    const int bx = w / 2 - 70 + i * 28;
    if (i < 3)
      r.fillRect(bx, cardY + 136, 22, 14, true);
    else
      r.drawRect(bx, cardY + 136, 22, 14, 2, true);
  }
  if (version && *version) {
    r.drawText(SMALL_FONT_ID, (w - r.getTextWidth(SMALL_FONT_ID, version)) / 2, cardY + cardH - 26, version);
  }
}

void drawEmptyState(const GfxRenderer& r, const int centerX, const int top, const char* message) {
  CatState sleepy;
  constexpr int catSize = kCatCells * kCell;
  drawCat(r, sleepy, centerX - catSize / 2, top);
  // a little rug under the cat
  r.fillRect(centerX - 70, top + catSize + 2, 140, 3, true);
  for (int x = centerX - 60; x <= centerX + 56; x += 12) r.fillRect(x, top + catSize + 6, 4, 6, true);
  if (message && *message) {
    const int w = r.getTextWidth(UI_12_FONT_ID, message);
    r.drawText(UI_12_FONT_ID, centerX - w / 2, top + catSize + 28, message);
  }
}

void drawCatCard(const GfxRenderer& r, const CatState& cat) {
  const int w = r.getScreenWidth() - 60;
  const int h = 300;
  const int x = 30;
  const int y = (r.getScreenHeight() - h) / 2;
  r.fillRect(x + 6, y + 6, w, h, true);
  r.fillRect(x, y, w, h, false);
  r.drawRoundedRect(x, y, w, h, 3, 12, true);

  static constexpr StrId kClassNames[] = {StrId::STR_CAT_CLASS_NONE,      StrId::STR_CAT_CLASS_MAGE,
                                          StrId::STR_CAT_CLASS_SWORDSMAN, StrId::STR_CAT_CLASS_KNIGHT,
                                          StrId::STR_CAT_CLASS_DETECTIVE, StrId::STR_CAT_CLASS_BARD,
                                          StrId::STR_CAT_CLASS_MECHANIC};
  static_assert(std::size(kClassNames) == BookGenre::GENRE_COUNT);
  const uint8_t genre = cat.genre < BookGenre::GENRE_COUNT ? cat.genre : 0;
  char line[96];
  snprintf(line, sizeof(line), "%s  LV.%u", I18N.get(kClassNames[genre]), cat.level);
  r.drawText(UI_12_FONT_ID, x + 24, y + 24, line, true, EpdFontFamily::BOLD);
  snprintf(line, sizeof(line), tr(STR_CAT_STREAK_FMT), static_cast<unsigned>(cat.streakDays));
  r.drawText(UI_12_FONT_ID, x + 24, y + 80, line);
  snprintf(line, sizeof(line), tr(STR_CAT_TODAY_FMT), static_cast<unsigned>(cat.todayMinutes),
           static_cast<unsigned>(cat.goalMinutes));
  r.drawText(UI_12_FONT_ID, x + 24, y + 124, line);
  static constexpr uint32_t kNext[] = {1, 3, 7, 14, 30};
  if (cat.level < 5) {
    snprintf(line, sizeof(line), tr(STR_CAT_NEXT_FMT),
             static_cast<unsigned>(kNext[cat.level] - std::min(cat.streakDays, kNext[cat.level])));
    r.drawText(UI_12_FONT_ID, x + 24, y + 168, line);
  }
  r.drawText(SMALL_FONT_ID, x + 24, y + h - 44, tr(STR_CAT_CARD_HINT));
}

}  // namespace PixelRoom
