#include "InxRecentActivity.h"

#include "util/BookGenre.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "components/themes/inx/InxTheme.h"
#include "components/themes/pixel/PixelAppIcons.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int kGap = 8;
constexpr int kPagePadding = 18;
constexpr int kProgressHeight = 6;
constexpr int kHomeBatteryWidth = 15;
constexpr int kHomeBatteryHeight = 12;
constexpr int kHomeBatteryRightMargin = 12;

Rect contentRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return Rect{0, top, renderer.getScreenWidth(),
              InxRecentGeometry::contentHeight(renderer.getScreenHeight(), top, metrics.buttonHintsHeight)};
}

const char* titleOf(const RecentBook& book) { return book.title.empty() ? book.path.c_str() : book.title.c_str(); }

void drawMiniProgress(const GfxRenderer& renderer, const Rect rect, const uint8_t percent) {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerWidth = std::max(0, rect.width - 2);
  const int fillWidth = innerWidth * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) renderer.fillRect(rect.x + 1, rect.y + 1, fillWidth, std::max(0, rect.height - 2));
}

uint8_t progressOf(const ReadingBookStats* stats) { return stats ? stats->lastProgressPercent : 0; }

void drawSparseInk(const GfxRenderer& renderer, const Rect rect) {
  for (int y = rect.y; y < rect.y + rect.height; y += 2) {
    for (int x = rect.x; x < rect.x + rect.width; x += 2) renderer.drawPixel(x, y, true);
  }
}

void drawDottedSeparator(const GfxRenderer& renderer, const int x, const int y, const int width) {
  for (int px = x; px < x + width; px += 3) renderer.drawPixel(px, y, true);
}

Rect fitCoverRect(const Rect bounds) {
  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  return Rect{bounds.x + (bounds.width - size.width) / 2, bounds.y + (bounds.height - size.height) / 2, size.width,
              size.height};
}

void drawThickFrame(const GfxRenderer& renderer, const Rect rect, const int thickness = 3) {
  for (int inset = 0; inset < thickness; ++inset) {
    renderer.drawRect(rect.x - inset, rect.y - inset, rect.width + inset * 2, rect.height + inset * 2, true);
  }
}

void drawProgressBadge(const GfxRenderer& renderer, const Rect cover, const uint8_t progress) {
  char label[8];
  snprintf(label, sizeof(label), "%u%%", static_cast<unsigned>(progress));
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
  const int textHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int width = std::max(24, textWidth + 8);
  const int height = textHeight + 4;
  const int x = cover.x + cover.width - width - 2;
  const int y = cover.y + 2;
  renderer.fillRect(x, y, width, height, true);
  renderer.drawText(SMALL_FONT_ID, x + (width - textWidth) / 2, y + 2, label, false, EpdFontFamily::BOLD);
}

void drawMetric(const GfxRenderer& renderer, const int x, const int y, const char* value, const char* label,
                const int width) {
  const std::string shownValue = renderer.truncatedText(UI_12_FONT_ID, value, width, EpdFontFamily::BOLD);
  const std::string shownLabel = renderer.truncatedText(SMALL_FONT_ID, label, width);
  renderer.drawText(UI_12_FONT_ID, x, y, shownValue.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, x, y + renderer.getLineHeight(UI_12_FONT_ID) + 4, shownLabel.c_str());
}

void drawBookText(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                  const bool author) {
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, titleOf(book), width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x, y, title.c_str(), true, EpdFontFamily::BOLD);
  if (author && !book.author.empty()) {
    const std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), width);
    renderer.drawText(SMALL_FONT_ID, x, y + 27, subtitle.c_str());
  }
}
}  // namespace

void InxRecentActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  selected = MainTabs::contentEdgeIndex(edge, books ? static_cast<int>(books->size()) : 0);
}

InxRecentLayout InxRecentActivity::layout() const {
  const auto value = static_cast<InxRecentLayout>(SETTINGS.inxRecentLayout);
  return value < InxRecentLayout::Count ? value : InxRecentLayout::Flow;
}

const ReadingBookStats* InxRecentActivity::statsAt(const int index) const {
  return index >= 0 && index < static_cast<int>(bookStats.size()) ? bookStats[index] : nullptr;
}

void InxRecentActivity::onEnter() {
  Activity::onEnter();
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  books = &RECENT_BOOKS.getBooks();
  bookStats.fill(nullptr);
  for (size_t i = 0; i < std::min(books->size(), bookStats.size()); ++i) {
    const RecentBook& book = (*books)[i];
    bookStats[i] = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author);
  }
  selected = 0;
  thumbnailHeight = 0;
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
  requestUpdate();
}

void InxRecentActivity::onExit() {
  books = nullptr;
  bookStats.fill(nullptr);
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
  thumbnailHeight = 0;
  Activity::onExit();
}

void InxRecentActivity::openSelected() {
  if (!books || selected < 0 || selected >= static_cast<int>(books->size())) return;
  onSelectBook((*books)[selected].path);
}

void InxRecentActivity::setThumbnailHeight(const int height) {
  if (thumbnailHeight == height) return;
  thumbnailHeight = height;
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
}

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
void InxRecentActivity::clearCoverCaches() {
  std::for_each(targetCoverCaches.begin(), targetCoverCaches.end(), [](auto& cache) { cache = {}; });
  std::for_each(fallbackCoverCaches.begin(), fallbackCoverCaches.end(), [](auto& cache) { cache = {}; });
  cachedCoverBytes = 0;
}

InxRecentActivity::CoverCacheLoadResult InxRecentActivity::tryLoadCoverCache(HalFile& file, CoverRamCache& cache) {
  if (cache.attempted) return CoverCacheLoadResult::Stream;
  cache.attempted = true;

  const size_t fileSize = file.fileSize();
  if (fileSize == 0 || fileSize > MAX_CACHED_COVER_FILE_BYTES || cachedCoverBytes > MAX_COVER_CACHE_BYTES ||
      fileSize > MAX_COVER_CACHE_BYTES - cachedCoverBytes) {
    return CoverCacheLoadResult::Stream;
  }
  if (!memory::psramHasHeadroom(fileSize, fileSize, COVER_CACHE_MAX_ALLOC_RESERVE)) {
    return CoverCacheLoadResult::Stream;
  }

  // A cover is up to 64 KB, so it cannot live on the task stack. Allocate once
  // on first use and retain it for this Activity.
  auto bytes = memory::makePsramByteBufferNoThrow(fileSize);
  if (!bytes) return CoverCacheLoadResult::Stream;
  if (!file.seek(0) || file.read(bytes.get(), fileSize) != static_cast<int>(fileSize)) {
    LOG_ERR("INX", "Short read loading cover into PSRAM (%u bytes)", static_cast<unsigned>(fileSize));
    file.seek(0);
    return CoverCacheLoadResult::Stream;
  }

  Bitmap bitmap(bytes.get(), fileSize);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    return CoverCacheLoadResult::Invalid;
  }

  cache.bytes = std::move(bytes);
  cache.size = fileSize;
  cachedCoverBytes += fileSize;
  return CoverCacheLoadResult::Loaded;
}
#endif

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
bool InxRecentActivity::tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state,
                                         CoverRamCache& cache) {
#else
bool InxRecentActivity::tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state) {
#endif
  switch (state) {
    case CoverCacheState::Unchecked:
      if (!Storage.exists(path.c_str())) {
        state = CoverCacheState::Missing;
        return false;
      }
      state = CoverCacheState::Ready;
      break;
    case CoverCacheState::Ready:
      break;
    case CoverCacheState::Missing:
    case CoverCacheState::Unavailable:
      return false;
  }

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  const auto drawCached = [this, &bounds, &state, &cache] {
    Bitmap bitmap(cache.bytes.get(), cache.size);
    if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
      cachedCoverBytes -= cache.size;
      cache = {};
      state = CoverCacheState::Missing;
      return false;
    }
    return renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
  };

  if (cache.bytes) return drawCached();
#endif

  HalFile file;
  if (!Storage.openFileForRead("INX", path, file)) {
    state = CoverCacheState::Missing;
    return false;
  }

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  switch (tryLoadCoverCache(file, cache)) {
    case CoverCacheLoadResult::Loaded:
      return drawCached();
    case CoverCacheLoadResult::Invalid:
      state = CoverCacheState::Missing;
      return false;
    case CoverCacheLoadResult::Stream:
      break;
  }
#endif

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    state = CoverCacheState::Missing;
    return false;
  }
  return renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
}

bool InxRecentActivity::drawBookCover(const int bookIndex, const Rect& bounds) {
  renderer.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, false);
  if (books && bookIndex >= 0 && bookIndex < static_cast<int>(books->size()) &&
      bookIndex < static_cast<int>(RecentBooksStore::MAX_RECENT_BOOKS) && thumbnailHeight > 0) {
    const RecentBook& book = (*books)[bookIndex];
    if (!book.coverBmpPath.empty()) {
      std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, thumbnailHeight);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
      if (tryDrawBookCover(path, bounds, targetCoverStates[bookIndex], targetCoverCaches[bookIndex])) return true;
#else
      if (tryDrawBookCover(path, bounds, targetCoverStates[bookIndex])) return true;
#endif
      if (book.coverBmpPath.find("[HEIGHT]") != std::string::npos &&
          thumbnailHeight != InxMetrics::values.homeCoverHeight) {
        path = UITheme::getCoverThumbPath(book.coverBmpPath, InxMetrics::values.homeCoverHeight);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
        if (tryDrawBookCover(path, bounds, fallbackCoverStates[bookIndex], fallbackCoverCaches[bookIndex])) return true;
#else
        if (tryDrawBookCover(path, bounds, fallbackCoverStates[bookIndex])) return true;
#endif
      }
    }
  }

  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  const int x = bounds.x + (bounds.width - size.width) / 2;
  const int y = bounds.y + (bounds.height - size.height) / 2;
  renderer.drawRect(x, y, size.width, size.height, 2, true);
  renderer.fillRect(x, y + size.height / 3, size.width, size.height * 2 / 3);
  constexpr int iconSize = 32;
  renderer.drawIcon(CoverIcon, x + (size.width - iconSize) / 2, y + size.height / 6 - iconSize / 2, iconSize);
  return false;
}

bool InxRecentActivity::prepareNextMissingCover() {
  if (!books || thumbnailHeight <= 0) return false;
  const size_t count = std::min(books->size(), RecentBooksStore::MAX_RECENT_BOOKS);
  for (size_t index = 0; index < count; ++index) {
    CoverCacheState& state = targetCoverStates[index];
    switch (state) {
      case CoverCacheState::Unchecked: {
        const std::string& templatePath = (*books)[index].coverBmpPath;
        if (templatePath.empty()) {
          state = CoverCacheState::Unavailable;
          continue;
        }
        state = Storage.exists(UITheme::getCoverThumbPath(templatePath, thumbnailHeight).c_str())
                    ? CoverCacheState::Ready
                    : CoverCacheState::Missing;
        if (state == CoverCacheState::Ready) continue;
        break;
      }
      case CoverCacheState::Ready:
      case CoverCacheState::Unavailable:
        continue;
      case CoverCacheState::Missing:
        break;
    }

    renderer.displayBuffer();
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      state = BookCoverLoader::ensureThumbnail((*books)[index].path, thumbnailHeight).empty()
                  ? CoverCacheState::Unavailable
                  : CoverCacheState::Ready;
    }
    requestUpdate();
    return true;
  }
  return false;
}

int InxRecentActivity::indexFromPoint(const int x, const int y) const {
  if (!books || books->empty()) return -1;
  const Rect content = contentRect(renderer);
  if (x < content.x || x >= content.x + content.width || y < content.y || y >= content.y + content.height) return -1;

  const InxRecentLayout currentLayout = layout();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), currentLayout);
  int columns = 1;
  int rows = 1;
  switch (currentLayout) {
    case InxRecentLayout::Grid:
      columns = 2;
      rows = 2;
      break;
    case InxRecentLayout::List:
      rows = 5;
      break;
    case InxRecentLayout::Icons:
      columns = 3;
      rows = 3;
      break;
    case InxRecentLayout::Flow:
    case InxRecentLayout::Cover:
      return selected;
    case InxRecentLayout::Count:
      return -1;
  }

  const int column = std::min(columns - 1, (x - content.x) * columns / std::max(1, content.width));
  const int row = std::min(rows - 1, (y - content.y) * rows / std::max(1, content.height));
  const int index = start + row * columns + column;
  return index < static_cast<int>(books->size()) ? index : -1;
}

void InxRecentActivity::refreshCatState() {
  catState.streakDays = READING_STATS.getCurrentStreakDays();
  catState.level = PixelRoom::levelForStreak(catState.streakDays);
  catState.genre = BookGenre::dominantRecentGenre();
  catState.todayMinutes = static_cast<uint32_t>(READING_STATS.getTodayReadingMs() / 60000);
  catState.goalMinutes = static_cast<uint32_t>(SETTINGS.getDailyGoalMs() / 60000);
}

void InxRecentActivity::activateRoomSpot(const int index) {
  using PixelRoom::Action;
  switch (PixelRoom::spot(index).action) {
    case Action::Sleep:
      activityManager.goToSleep();
      return;
    case Action::Stats:
      activityManager.goToReadingStats();
      return;
    case Action::Recent:
      activityManager.goToRecentBooks();
      return;
    case Action::Library:
      shelfOpen = true;
      shelfPage = 0;
      shelfSelected = 0;
      requestUpdate();
      return;
    case Action::Settings:
      activityManager.goToSettings();
      return;
    case Action::Read:
      if (books && !books->empty()) onSelectBook((*books)[0].path);
      return;
    case Action::Transfer:
      activityManager.goToFileTransfer();
      return;
    case Action::Apps:
      activityManager.goToApps();
      return;
    case Action::Cat:
      catCardOpen = true;
      requestUpdate();
      return;
  }
}

bool InxRecentActivity::handleRoomInput() {
  int x = 0;
  int y = 0;
  if (catCardOpen) {
    if (mappedInput.wasScreenTapped(x, y) || mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      catCardOpen = false;
      requestUpdate();
    }
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRoomSpot(roomSelected);
    return true;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    const int hit = PixelRoom::spotAt(x, y);
    if (hit >= 0 && PixelRoom::spot(hit).action == PixelRoom::Action::Cat) {
      // The cat itself plays its next trick; its "打卡" tag (above it) opens the card.
      roomSelected = hit;
      catAction = static_cast<uint8_t>(PixelRoom::CAT_LICK + catNextAction);
      catNextAction = static_cast<uint8_t>((catNextAction + 1) % (PixelRoom::CAT_ACTION_COUNT - 1));
      catActionStep = 0;
      catFrame = 0;
      catFrameAt = millis();
      requestUpdate();
      return true;
    }
    for (int i = 0; hit < 0 && i < PixelRoom::spotCount(); ++i) {
      const PixelRoom::Spot& s = PixelRoom::spot(i);
      if (s.action == PixelRoom::Action::Cat && x >= s.tagX - 6 && x < s.tagX + 84 && y >= s.tagY - 6 &&
          y < s.tagY + 40) {
        roomSelected = i;
        activateRoomSpot(i);
        return true;
      }
    }
    if (hit >= 0) {
      roomSelected = hit;
      activateRoomSpot(hit);
    }
    return true;
  }
  const int count = PixelRoom::spotCount();
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    roomSelected = (roomSelected + 1) % count;
    requestUpdate();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    roomSelected = (roomSelected + count - 1) % count;
    requestUpdate();
    return true;
  }
  return false;
}

// Idle life on the home page: the awake cat blinks every 12 s (eyes shut for 300 ms), the
// sleeping cat's Zz drift every 6 s. Each step is one fast refresh of the unchanged room, so
// only the cat's cells change on the panel; it settles 2 min after the last interaction.
void InxRecentActivity::animateCat() {
  constexpr unsigned long kSettleMs = 120000;
  const unsigned long now = millis();
  if (catAction != PixelRoom::CAT_IDLE) {  // a tapped trick: four alternating frames, 0.7 s each
    constexpr uint8_t kActionSteps = 4;    // (slower steps would merge with the panel refresh)
    if (now - catFrameAt < 700) return;
    catFrameAt = now;
    if (++catActionStep >= kActionSteps) {
      catAction = PixelRoom::CAT_IDLE;
      catFrame = 0;
    } else {
      catFrame = catActionStep % 2;
    }
    requestUpdate();
    return;
  }
  if (catCardOpen || now - roomActiveAt > kSettleMs) {
    if (catFrame != 0 && !catCardOpen) {
      catFrame = 0;
      requestUpdate();
    }
    catFrameAt = now;
    return;
  }
  const bool asleep = catState.level == 0;
  const unsigned long hold = catFrame != 0 ? (asleep ? 6000 : 300) : (asleep ? 6000 : 12000);
  if (now - catFrameAt < hold) return;
  catFrame ^= 1;
  catFrameAt = now;
  requestUpdate();
}

void InxRecentActivity::loop() {
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::PIXEL) {
    if (shelfOpen) {
      handleShelfInput();
      return;
    }
    const int before = roomSelected;
    const bool cardBefore = catCardOpen;
    handleRoomInput();
    if (roomActiveAt == 0 || roomSelected != before || catCardOpen != cardBefore) roomActiveAt = millis();
    animateCat();
    if (!catCardOpen) {
      const uint32_t minute = TimeUtils::getAuthoritativeTimestamp() / 60;
      if (roomClockMinute != 0 && minute != roomClockMinute) requestUpdate();
      roomClockMinute = minute;
    }
    return;
  }
  if (!books || books->empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchDown(x, y)) {
    const int touched = indexFromPoint(x, y);
    if (touched >= 0 && touched != selected) {
      selected = touched;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    const int touched = indexFromPoint(x, y);
    if (touched >= 0) {
      selected = touched;
      openSelected();
    }
    return;
  }

  const int count = static_cast<int>(books->size());
  const auto swipe = mappedInput.wasSwipe();
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext) || swipe == MappedInputManager::SwipeDir::Up ||
      swipe == MappedInputManager::SwipeDir::Left) {
    selected = (selected + 1) % count;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious) || swipe == MappedInputManager::SwipeDir::Down ||
      swipe == MappedInputManager::SwipeDir::Right) {
    selected = (selected + count - 1) % count;
    requestUpdate();
    return;
  }
}

void InxRecentActivity::drawFlow(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int count = static_cast<int>(books->size());
  const RecentBook& book = (*books)[selected];
  const int carouselHeight = std::max(1, content.height * 50 / 100);
  const Rect carousel{content.x, content.y + 5, content.width, carouselHeight};
  drawSparseInk(renderer, carousel);

  const auto centerSize = InxCoverGeometry::fit(carousel.width, std::max(1, carousel.height * 94 / 100));
  const Rect center{carousel.x + (carousel.width - centerSize.width) / 2,
                    carousel.y + (carousel.height - centerSize.height) / 2, centerSize.width, centerSize.height};
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(center.height));
  const auto sideSize = InxCoverGeometry::fit(carousel.width, std::max(1, center.height * 90 / 100));
  const int sideTop = center.y + (center.height - sideSize.height) / 2;
  const int sideGap = std::max(kGap, content.width * 4 / 100);
  {
    const GfxRenderer::ClipScope clip(renderer, carousel.x, carousel.y, carousel.width, carousel.height);
    if (selected > 0) {
      drawBookCover(selected - 1, Rect{center.x - sideSize.width - sideGap, sideTop, sideSize.width, sideSize.height});
    }
    if (selected + 1 < count) {
      drawBookCover(selected + 1, Rect{center.x + center.width + sideGap, sideTop, sideSize.width, sideSize.height});
    }
    drawBookCover(selected, center);
    if (showSelection) drawThickFrame(renderer, center);
  }

  const int dividerY = carousel.y + carousel.height + 10;
  renderer.drawLine(content.x, dividerY, content.x + content.width - 1, dividerY, true);
  const int textY = dividerY + 15;
  drawBookText(renderer, book, kPagePadding, textY, content.width - kPagePadding * 2, true);

  const ReadingBookStats* stats = statsAt(selected);
  const uint8_t progress = progressOf(stats);
  const int progressY = textY + 58;
  const int progressWidth = std::max(24, (content.width - kPagePadding * 2) / 2);
  drawMiniProgress(renderer, Rect{kPagePadding, progressY, progressWidth, kProgressHeight}, progress);
  char percent[8];
  snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progress));
  renderer.drawText(SMALL_FONT_ID, kPagePadding + progressWidth + 12,
                    progressY - (renderer.getLineHeight(SMALL_FONT_ID) - kProgressHeight) / 2, percent);

  const int metricsTop = progressY + 34;
  const int metricWidth = (content.width - kPagePadding * 2 - kGap) / 2;
  const int metricHeight = std::max(1, (content.y + content.height - metricsTop) / 2);
  const std::string total = stats ? ReadingStatsAnalytics::formatDurationHm(stats->totalReadingMs) : "0m";
  const std::string last = stats ? ReadingStatsAnalytics::formatDurationHm(stats->lastSessionMs) : "0m";
  char sessions[16];
  char chapter[8];
  snprintf(sessions, sizeof(sessions), "%u", stats ? static_cast<unsigned>(stats->sessions) : 0U);
  snprintf(chapter, sizeof(chapter), "%u%%", stats ? static_cast<unsigned>(stats->chapterProgressPercent) : 0U);
  drawMetric(renderer, kPagePadding, metricsTop, total.c_str(), tr(STR_TOTAL_TIME), metricWidth);
  drawMetric(renderer, kPagePadding + metricWidth + kGap, metricsTop, sessions, tr(STR_SESSIONS), metricWidth);
  drawMetric(renderer, kPagePadding, metricsTop + metricHeight, last.c_str(), tr(STR_LAST_SESSION), metricWidth);
  drawMetric(renderer, kPagePadding + metricWidth + kGap, metricsTop + metricHeight, chapter, tr(STR_CHAPTER_PROGRESS),
             metricWidth);
}

void InxRecentActivity::drawGrid(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int cellWidth = content.width / 2;
  const int cellHeight = content.height / 2;
  for (int slot = 0; slot < 4 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 2;
    const int row = slot / 2;
    const Rect cell{content.x + column * cellWidth + kGap / 2, content.y + row * cellHeight + kGap / 2,
                    cellWidth - kGap, cellHeight - kGap};
    if (showSelection && index == selected) drawSparseInk(renderer, cell);
    const Rect cover = fitCoverRect(Rect{cell.x + kGap, cell.y + kGap, cell.width - kGap * 2, cell.height - kGap * 2});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    if (showSelection && index == selected) drawThickFrame(renderer, cover);
    const int barWidth = std::max(24, cover.width - 30);
    const int barX = cover.x + (cover.width - barWidth) / 2;
    const int barY = cover.y + cover.height - 18;
    renderer.fillRect(barX - 2, barY - 2, barWidth + 4, kProgressHeight + 4, false);
    drawMiniProgress(renderer, Rect{barX, barY, barWidth, kProgressHeight}, progressOf(statsAt(index)));
  }
}

void InxRecentActivity::drawList(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int rowHeight = content.height / 5;
  for (int slot = 0; slot < 5 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const Rect row{content.x, content.y + slot * rowHeight, content.width, rowHeight};
    if (showSelection && index == selected) drawSparseInk(renderer, row);
    const Rect cover = fitCoverRect(Rect{row.x + kPagePadding, row.y + 5, 88, row.height - 10});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    const int textX = cover.x + cover.width + 14;
    const int textWidth = row.x + row.width - kPagePadding - textX;
    drawBookText(renderer, (*books)[index], textX, row.y + 14, textWidth, true);
    drawMiniProgress(renderer,
                     Rect{textX, row.y + row.height - 15, std::max(24, textWidth * 80 / 100), kProgressHeight},
                     progressOf(statsAt(index)));
    if (slot + 1 < 5 && start + slot + 1 < static_cast<int>(books->size())) {
      drawDottedSeparator(renderer, row.x + kGap, row.y + row.height - 1, row.width - kGap * 2);
    }
  }
}

void InxRecentActivity::drawIcons(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int cellWidth = content.width / 3;
  const int cellHeight = content.height / 3;
  for (int slot = 0; slot < 9 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 3;
    const int row = slot / 3;
    const Rect cell{content.x + column * cellWidth + 5, content.y + row * cellHeight + 5, cellWidth - 10,
                    cellHeight - 10};
    const Rect cover = fitCoverRect(Rect{cell.x + 4, cell.y + 4, cell.width - 8, cell.height - 8});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    drawProgressBadge(renderer, cover, progressOf(statsAt(index)));
    if (showSelection && index == selected)
      drawThickFrame(renderer, Rect{cover.x - 2, cover.y - 2, cover.width + 4, cover.height + 4});
  }
}

void InxRecentActivity::drawCover(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  constexpr int progressGap = 10;
  const int progressBlockHeight = progressGap + 8;
  const int targetWidth = std::max(1, content.width * 78 / 100);
  const Rect cover = fitCoverRect(Rect{content.x + (content.width - targetWidth) / 2, content.y + 6, targetWidth,
                                       std::max(1, content.height - progressBlockHeight - 12)});
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
  drawBookCover(selected, cover);
  if (showSelection) drawThickFrame(renderer, cover);
  const int barWidth = std::max(24, cover.width * 80 / 100);
  drawMiniProgress(renderer,
                   Rect{cover.x + (cover.width - barWidth) / 2, cover.y + cover.height + progressGap, barWidth, 8},
                   progressOf(statsAt(selected)));
}

void InxRecentActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::PIXEL) {
    if (shelfOpen) {
      drawShelf();
      if (prepareNextMissingCover()) return;
      renderer.displayBuffer();
      return;
    }
    refreshCatState();
    catState.frame = catFrame;
    catState.action = catAction;
    PixelRoom::draw(renderer, roomSelected, catState);
    if (catCardOpen) PixelRoom::drawCatCard(renderer, catState);
    renderer.displayBuffer();
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  drawPageHeader(Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));
  const Rect content = contentRect(renderer);

  if (!books || books->empty()) {
    UITheme::drawCenteredWrappedText(renderer, content, UI_12_FONT_ID, tr(STR_NO_RECENT_BOOKS), 2);
  } else {
    switch (layout()) {
      case InxRecentLayout::Flow:
        drawFlow(content);
        break;
      case InxRecentLayout::Grid:
        drawGrid(content);
        break;
      case InxRecentLayout::List:
        drawList(content);
        break;
      case InxRecentLayout::Icons:
        drawIcons(content);
        break;
      case InxRecentLayout::Cover:
        drawCover(content);
        break;
      case InxRecentLayout::Count:
        break;
    }
  }

  const auto labels = mainTabButtonLabels(SETTINGS.standbyShortcutEnabled ? tr(STR_STANDBY_TITLE) : "", tr(STR_OPEN),
                                          books && books->size() > 1, false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawBatteryRight(renderer,
                       Rect{renderer.getScreenWidth() - kHomeBatteryRightMargin - kHomeBatteryWidth,
                            renderer.getScreenHeight() - 30, kHomeBatteryWidth, kHomeBatteryHeight},
                       SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  if (prepareNextMissingCover()) return;
  renderer.displayBuffer();
}

// --- Pixel bookshelf ---------------------------------------------------------
namespace {
constexpr int kShelfHeaderH = 84;
constexpr int kShelfCoverW = 128;
constexpr int kShelfCoverH = 188;
constexpr int kTopCoverX = 28;
constexpr int kTopCoverY = 100;
constexpr int kInfoX = 178;
constexpr int kLowerCoverY = 372;
constexpr int kSlotGap = 20;
constexpr int kTileY = 632;
constexpr int kTileW = 136;
constexpr int kTileH = 120;
constexpr int kTileGap = 16;
constexpr int kSlots = 3;

// ShelfTarget: 0 = current book, 1..3 = lower-shelf slots, 4..6 = Library / Transfer / Settings.
constexpr int kTargetCurrent = 0;
constexpr int kTargetFirstSlot = 1;
constexpr int kTargetFirstTile = 4;
constexpr int kTargetCount = 7;

int slotX(const int slot) {
  const int total = kSlots * kShelfCoverW + (kSlots - 1) * kSlotGap;
  return (480 - total) / 2 + slot * (kShelfCoverW + kSlotGap);
}
int tileX(const int tile) { return 24 + tile * (kTileW + kTileGap); }
const Rect kContinueButton{kInfoX, 230, 200, 54};

std::string formatOf(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || path.size() - dot > 5) return {};
  std::string ext = path.substr(dot + 1);
  for (char& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return ext;
}

void drawPixelArt(const GfxRenderer& r, const char* const* rows, const int x, const int y, const int scale,
                  const bool ink = true) {
  for (int row = 0; row < PixelAppIcons::kSize; ++row)
    for (int col = 0; col < PixelAppIcons::kSize; ++col)
      if (rows[row][col] == '#') r.fillRect(x + col * scale, y + row * scale, scale, scale, ink);
}

// A wooden plank: dithered top face, framed front edge, hard shadow below.
void drawPlank(const GfxRenderer& r, const int y) {
  constexpr int x = 12;
  constexpr int w = 456;
  r.fillRectDither(x + 10, y, w - 20, 10, Color::DarkGray);
  r.drawLine(x + 10, y, x + w - 11, y, true);
  r.fillRect(x, y + 10, w, 20, false);
  r.drawRect(x, y + 10, w, 20, 2, true);
  r.fillRect(x + 4, y + 30, w - 4, 4, true);
}

// Our own no-cover card: double frame, title, an open book and a dotted ornament.
void drawNoCover(const GfxRenderer& r, const char* title, const int x, const int y) {
  r.fillRect(x, y, kShelfCoverW, kShelfCoverH, false);
  r.drawRect(x + 6, y + 6, kShelfCoverW - 12, kShelfCoverH - 12, 1, true);
  UITheme::drawCenteredWrappedText(r, Rect{x + 12, y + 16, kShelfCoverW - 24, 56}, UI_10_FONT_ID, title, 2);
  drawPixelArt(r, PixelAppIcons::kOpenBook, x + (kShelfCoverW - 48) / 2, y + 82, 3);
  for (int dx = 18; dx < kShelfCoverW - 18; dx += 8) r.fillRect(x + dx, y + kShelfCoverH - 30, 4, 2, true);
}
}  // namespace

int InxRecentActivity::shelfPageCount() const {
  const int rest = books ? std::max(0, static_cast<int>(books->size()) - 1) : 0;
  return std::max(1, (rest + kSlots - 1) / kSlots);
}

int InxRecentActivity::shelfBookAt(const int slot) const {
  const int index = 1 + shelfPage * kSlots + slot;
  return books && index < static_cast<int>(books->size()) ? index : -1;
}

bool InxRecentActivity::shelfTargetValid(const int target) const {
  if (target == kTargetCurrent) return books && !books->empty();
  if (target < kTargetFirstTile) return shelfBookAt(target - kTargetFirstSlot) >= 0;
  return target < kTargetCount;
}

void InxRecentActivity::activateShelfTarget(const int target) {
  if (!shelfTargetValid(target)) return;
  if (target == kTargetCurrent) {
    onSelectBook((*books)[0].path);
  } else if (target < kTargetFirstTile) {
    onSelectBook((*books)[shelfBookAt(target - kTargetFirstSlot)].path);
  } else if (target == kTargetFirstTile) {
    activityManager.goToFileBrowser();
  } else if (target == kTargetFirstTile + 1) {
    activityManager.goToFileTransfer();
  } else {
    activityManager.goToSettings();
  }
}

bool InxRecentActivity::handleShelfInput() {
  const auto close = [this] {
    shelfOpen = false;
    requestUpdate();
    return true;
  };
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) return close();
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateShelfTarget(shelfSelected);
    return true;
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    if (y < kShelfHeaderH) return x < 96 ? close() : true;
    const auto inside = [x, y](const Rect& rect) {
      return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
    };
    int target = -1;
    if (inside(Rect{kTopCoverX, kTopCoverY, kShelfCoverW, kShelfCoverH}) || inside(kContinueButton)) {
      target = kTargetCurrent;
    }
    for (int slot = 0; slot < kSlots; ++slot) {
      if (inside(Rect{slotX(slot), kLowerCoverY - 26, kShelfCoverW, kShelfCoverH + 26})) target = kTargetFirstSlot + slot;
    }
    for (int tile = 0; tile < 3; ++tile) {
      if (inside(Rect{tileX(tile), kTileY, kTileW, kTileH})) target = kTargetFirstTile + tile;
    }
    if (target >= 0 && shelfTargetValid(target)) {
      shelfSelected = target;
      activateShelfTarget(target);
    }
    return true;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right) {
    const int pages = shelfPageCount();
    shelfPage = (shelfPage + (swipe == MappedInputManager::SwipeDir::Left ? 1 : pages - 1)) % pages;
    requestUpdate();
    return true;
  }
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::NavNext);
  if (next || mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    // Walk the targets in reading order; stepping past the last slot of a page turns it.
    const int pages = shelfPageCount();
    for (int guard = 0; guard < kTargetCount * (pages + 1); ++guard) {
      int t = shelfSelected + (next ? 1 : -1);
      if (next && t == kTargetFirstTile && shelfPage + 1 < pages) {
        ++shelfPage;
        t = kTargetFirstSlot;
      } else if (!next && t == kTargetFirstSlot - 1 && shelfPage > 0) {
        --shelfPage;
        t = kTargetFirstTile - 1;
      }
      shelfSelected = (t + kTargetCount) % kTargetCount;
      if (shelfTargetValid(shelfSelected)) break;
    }
    requestUpdate();
    return true;
  }
  return false;
}

void InxRecentActivity::drawShelfCover(const int bookIndex, const int x, const int y) {
  renderer.fillRect(x + 4, y + 4, kShelfCoverW, kShelfCoverH, true);
  const Rect bounds{x, y, kShelfCoverW, kShelfCoverH};
  if (!drawBookCover(bookIndex, bounds)) drawNoCover(renderer, titleOf((*books)[bookIndex]), x, y);
  renderer.drawRect(x, y, kShelfCoverW, kShelfCoverH, 2, true);
}

void InxRecentActivity::drawShelf() {
  const int w = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, 0, w, kShelfHeaderH}, tr(STR_ROOM_SHELF));
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(kShelfCoverH));

  // Top shelf: the book in hand.
  if (!books || books->empty()) {
    PixelRoom::drawEmptyState(renderer, w / 2, kTopCoverY + 8, tr(STR_NO_RECENT_BOOKS));
  } else {
    const RecentBook& book = (*books)[0];
    const ReadingBookStats* stats = statsAt(0);
    drawShelfCover(0, kTopCoverX, kTopCoverY);
    const int infoW = w - 24 - kInfoX;
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, titleOf(book), infoW, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, kInfoX, kTopCoverY + 2, title.c_str(), true, EpdFontFamily::BOLD);
    int lineY = kTopCoverY + 50;
    const std::string format = formatOf(book.path);
    const int pillW = format.empty() ? 0 : renderer.getTextWidth(SMALL_FONT_ID, format.c_str()) + 16;
    if (!book.author.empty()) {
      const std::string author = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), infoW - pillW - 8);
      renderer.drawText(SMALL_FONT_ID, kInfoX, lineY, author.c_str());
    }
    if (pillW > 0) {
      const int pillH = renderer.getLineHeight(SMALL_FONT_ID) + 2;
      renderer.fillRoundedRect(w - 24 - pillW, lineY - 1, pillW, pillH, pillH / 2, Color::Black);
      renderer.drawText(SMALL_FONT_ID, w - 24 - pillW + 8, lineY, format.c_str(), false);
    }
    lineY += 32;
    if (stats && !stats->chapterTitle.empty()) {
      const std::string chapter = renderer.truncatedText(SMALL_FONT_ID, stats->chapterTitle.c_str(), infoW);
      renderer.drawText(SMALL_FONT_ID, kInfoX, lineY, chapter.c_str());
    }
    lineY += 34;
    const uint8_t progress = progressOf(stats);
    char percent[8];
    snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progress));
    const int percentW = renderer.getTextWidth(SMALL_FONT_ID, percent);
    renderer.drawText(SMALL_FONT_ID, w - 24 - percentW, lineY - 4, percent);
    const int barW = infoW - percentW - 12;
    renderer.drawRoundedRect(kInfoX, lineY, barW, 12, 2, 6, true);
    const int fill = (barW - 6) * std::min<int>(progress, 100) / 100;
    if (fill > 0) renderer.fillRect(kInfoX + 3, lineY + 3, fill, 6, true);
    GUI.drawActionButton(renderer, kContinueButton, tr(STR_CONTINUE_READING), shelfSelected == kTargetCurrent);
  }
  drawPlank(renderer, kTopCoverY + kShelfCoverH);

  // Lower shelf: the rest of the recent books, three per page.
  for (int slot = 0; slot < kSlots; ++slot) {
    const int index = shelfBookAt(slot);
    if (index < 0) continue;
    const int x = slotX(slot);
    char percent[8];
    snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progressOf(statsAt(index))));
    const int tabW = renderer.getTextWidth(SMALL_FONT_ID, percent) + 20;
    renderer.fillRoundedRect(x + 10, kLowerCoverY - 26, tabW, 30, 6, Color::Black);
    renderer.drawText(SMALL_FONT_ID, x + 20, kLowerCoverY - 23, percent, false);
    drawShelfCover(index, x, kLowerCoverY);
    if (shelfSelected == kTargetFirstSlot + slot) {
      renderer.drawRoundedRect(x - 6, kLowerCoverY - 6, kShelfCoverW + 16, kShelfCoverH + 16, 3, 8, true);
    }
  }
  drawPlank(renderer, kLowerCoverY + kShelfCoverH);

  const int pages = shelfPageCount();
  if (pages > 1) {
    constexpr int dot = 10;
    constexpr int gap = 12;
    const int startX = (w - (pages * dot + (pages - 1) * gap)) / 2;
    for (int i = 0; i < pages; ++i) {
      const int dx = startX + i * (dot + gap);
      if (i == shelfPage)
        renderer.fillRect(dx, 600, dot, dot, true);
      else
        renderer.drawRect(dx, 600, dot, dot, 2, true);
    }
  }

  // Library / Transfer / Settings tiles.
  static constexpr StrId kTileLabels[] = {StrId::STR_ROOM_LIBRARY, StrId::STR_ROOM_TRANSFER, StrId::STR_ROOM_SETTINGS};
  static constexpr PixelAppIcons::Art kTileArt[] = {PixelAppIcons::kOpenBook, PixelAppIcons::kTransfer,
                                                    PixelAppIcons::kGear};
  for (int tile = 0; tile < 3; ++tile) {
    const int x = tileX(tile);
    const bool selected = shelfSelected == kTargetFirstTile + tile;
    renderer.fillRect(x + 4, kTileY + 4, kTileW - 4, kTileH - 4, true);
    renderer.fillRoundedRect(x, kTileY, kTileW - 4, kTileH - 4, 8, selected ? Color::Black : Color::White);
    renderer.drawRoundedRect(x, kTileY, kTileW - 4, kTileH - 4, 2, 8, true);
    drawPixelArt(renderer, kTileArt[tile], x + (kTileW - 4 - 48) / 2, kTileY + 14, 3, !selected);
    const char* label = I18N.get(kTileLabels[tile]);
    const int lw = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, x + (kTileW - 4 - lw) / 2, kTileY + 70, label, !selected, EpdFontFamily::BOLD);
  }
}
