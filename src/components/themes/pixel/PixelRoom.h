#pragma once

#include <I18n.h>

#include <cstdint>

class GfxRenderer;

// Pixel-theme home: a pixel-art study where every object opens a feature, and a
// study cat whose class follows the genre read lately (BookGenre) and whose
// level follows the reading-goal streak.
namespace PixelRoom {

enum class Action : uint8_t { Sleep, Stats, Recent, Library, Settings, Read, Transfer, Apps, Cat };

struct Spot {
  int16_t x, y, w, h;   // tap area, screen px
  int16_t tagX, tagY;   // label position
  StrId object;         // what it is ("台灯")
  StrId feature;        // what it opens ("设置")
  Action action;
};

int spotCount();
const Spot& spot(int index);
int spotAt(int x, int y);  // -1 outside every spot

// Streak days -> cat level 0..5 (0, 1, 3, 7, 14, 30 days).
uint8_t levelForStreak(uint32_t streakDays);

struct CatState {
  uint8_t genre = 0;  // BookGenre::Genre; 0 = classless cat
  uint8_t level = 0;
  uint32_t streakDays = 0;
  uint32_t todayMinutes = 0;
  uint32_t goalMinutes = 0;
  uint8_t frame = 0;  // idle animation: 1 = eyes shut (awake) / Zz drifted up (asleep)
  uint8_t action = 0;  // CatAction being played after a tap (frame alternates 0/1); wakes a sleeping cat
};

// Things the cat does when tapped, in the order taps cycle through them.
enum CatAction : uint8_t { CAT_IDLE = 0, CAT_LICK, CAT_SCRATCH, CAT_YAWN, CAT_WAG, CAT_ACTION_COUNT };

void draw(const GfxRenderer& renderer, int selected, const CatState& cat);
// Sleep screen: the room at night, the cat asleep, today's reading on a card.
void drawSleepScene(const GfxRenderer& renderer, const CatState& cat);
// Boot splash: the study with a pixel-lettered CROSSMUX card.
void drawBootScene(const GfxRenderer& renderer, const char* version);
// Empty list: the sleeping cat on a rug, with a message under it.
void drawEmptyState(const GfxRenderer& renderer, int centerX, int top, const char* message);
// Card shown after tapping the cat.
void drawCatCard(const GfxRenderer& renderer, const CatState& cat);

}  // namespace PixelRoom
