#include "HighlightListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;
}  // namespace

HighlightListActivity::HighlightListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::shared_ptr<Epub> epub, std::string bookPath)
    : UiListActivity("HighlightList", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      epub(std::move(epub)),
      bookPath(std::move(bookPath)) {}

void HighlightListActivity::onEnter() {
  UiListActivity::onEnter();
  HighlightFile::load(bookPath, highlights);
  rebuildRowItems();
}

// Rows are derived once per change of `highlights` (load, delete), not per repaint.
void HighlightListActivity::rebuildRowItems() {
  rowSubtitles.clear();
  rowItems.clear();
  rowSubtitles.reserve(highlights.size());
  rowItems.reserve(highlights.size());
  for (const auto& h : highlights) {
    std::string subtitle = std::to_string(static_cast<int>(std::clamp(h.percentage, 0.0f, 1.0f) * 100.0f + 0.5f)) + "%";
    if (epub) {
      const int tocIndex = epub->getTocIndexForSpineIndex(h.spine);
      subtitle += " - ";
      subtitle += tocIndex >= 0 ? epub->getTocItem(tocIndex).title : std::string(tr(STR_UNNAMED));
    }
    rowSubtitles.push_back(std::move(subtitle));

    fui::ListItem item;
    item.label = h.text.empty() ? tr(STR_UNNAMED) : h.text.c_str();
    item.subtitle = rowSubtitles.back().c_str();
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
}

void HighlightListActivity::openSelected() {
  if (highlights.empty()) return;
  const auto& h = highlights.at(nav.selected);
  if (epub) {
    ProgressChangeResult result{};
    result.spineIndex = h.spine;
    result.percentage = h.percentage;
    result.hasSavedProgress = true;
    result.hasVisibleTextOffset = true;
    result.visibleTextOffset = h.start;
    setResult(std::move(result));
  } else {
    TxtOffsetResult result{};
    result.sourceOffset = h.start;
    setResult(std::move(result));
  }
  finish();
}

void HighlightListActivity::activateIndex(const int index) {
  if (confirmPopup.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  openSelected();
}

void HighlightListActivity::onRowLongPress(const int index) {
  if (confirmPopup.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showDeleteConfirmation();
}

bool HighlightListActivity::handleCustomInput() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete) {
    // Popup dismissed without a choice (Back or tap outside): keep it.
    confirmingDelete = false;
    requestUpdate();
    return true;
  }
  return false;
}

bool HighlightListActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
      showDeleteConfirmation();
    } else {
      openSelected();
    }
    return true;
  }
  return false;
}

void HighlightListActivity::showDeleteConfirmation() {
  if (highlights.empty() || confirmPopup.isActive()) return;
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_HIGHLIGHT_CONFIRM_DELETE), options, 2, 0, [this](const int idx) {
    confirmingDelete = false;
    if (idx == 1) deleteSelected();
    requestUpdate();
  });
  requestUpdate();
}

void HighlightListActivity::deleteSelected() {
  highlights.erase(highlights.begin() + nav.selected);
  // Re-derive before the SD write so the render task never sees rows
  // aliasing erased storage.
  rebuildRowItems();
  if (!HighlightFile::save(bookPath, highlights)) LOG_ERR("HLT", "Failed to save highlights after delete");
  if (nav.selected >= static_cast<int>(highlights.size()) && nav.selected > 0) nav.selected--;
  if (highlights.empty()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  nav.follow(listCount());
  requestUpdate(true);
}

void HighlightListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (highlights.empty()) {
    screen.centeredText(tr(STR_HIGHLIGHT_NONE), screen.theme().bodyText);
    return;
  }

  // "Hold Open to Delete" names a physical button; touch boards long-press rows.
  if (!mappedInput.hasTouch()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_HOLD_OPEN_TO_DELETE));
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void HighlightListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int contentY = isPortraitInverted ? 50 : 0;
  const char* title = tr(STR_HIGHLIGHTS);
  const int titleX = contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, title, true, EpdFontFamily::BOLD);

  renderUi();

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  const auto confirmLabel = highlights.empty() ? "" : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
