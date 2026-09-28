#include "TouchZoneEditorActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kLineWidth = 2;
}  // namespace

void TouchZoneEditorActivity::onEnter() {
  Activity::onEnter();
  memcpy(actions, SETTINGS.touchZoneActions, sizeof(actions));
  requestUpdate();
}

void TouchZoneEditorActivity::openPicker() {
  std::vector<std::string> options;
  options.reserve(static_cast<size_t>(touchZones::Action::Count));
  for (uint8_t i = 0; i < static_cast<uint8_t>(touchZones::Action::Count); ++i) {
    options.emplace_back(I18N.get(touchZones::label(static_cast<touchZones::Action>(i))));
  }
  picker.show(StrId::STR_EDIT_TOUCH_ZONES, options, actions[selected], [this](const int index) {
    if (index < 0 || index >= static_cast<int>(touchZones::Action::Count)) return;
    if (actions[selected] != static_cast<uint8_t>(index)) {
      actions[selected] = static_cast<uint8_t>(index);
      changed = true;
    }
  });
  requestUpdate();
}

void TouchZoneEditorActivity::saveAndFinish() {
  if (changed) {
    memcpy(SETTINGS.touchZoneActions, actions, sizeof(actions));
    SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_CUSTOM;
    SETTINGS.saveToFile();
  }
  finish();
}

void TouchZoneEditorActivity::loop() {
  if (picker.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveAndFinish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openPicker();
    return;
  }

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int zone = touchZones::zoneAt(tx, ty, renderer.getScreenWidth(), renderer.getScreenHeight());
    if (zone >= 0) {
      selected = zone;
      openPicker();
    }
    return;
  }

  // Buttons move the highlighted cell across the grid.
  int row = selected / touchZones::kColumns;
  int col = selected % touchZones::kColumns;
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    col = (col + touchZones::kColumns - 1) % touchZones::kColumns;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    col = (col + 1) % touchZones::kColumns;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    row = (row + touchZones::kRows - 1) % touchZones::kRows;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    row = (row + 1) % touchZones::kRows;
  } else {
    return;
  }
  selected = row * touchZones::kColumns + col;
  requestUpdate();
}

void TouchZoneEditorActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  // Grid: the same cell edges touchZones::zoneAt() uses.
  for (int c = 1; c < touchZones::kColumns; ++c) {
    const int x = c * width / touchZones::kColumns;
    renderer.fillRect(x - kLineWidth / 2, 0, kLineWidth, height, true);
  }
  for (int r = 1; r < touchZones::kRows; ++r) {
    const int y = r * height / touchZones::kRows;
    renderer.fillRect(0, y - kLineWidth / 2, width, kLineWidth, true);
  }

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  for (int zone = 0; zone < touchZones::kZoneCount; ++zone) {
    const int col = zone % touchZones::kColumns;
    const int row = zone / touchZones::kColumns;
    const int left = col * width / touchZones::kColumns;
    const int right = (col + 1) * width / touchZones::kColumns;
    const int top = row * height / touchZones::kRows;
    const int bottom = (row + 1) * height / touchZones::kRows;
    const auto action = static_cast<touchZones::Action>(actions[zone]);
    const char* text = I18N.get(touchZones::label(action));
    const bool isSelected = zone == selected && !mappedInput.hasTouch();
    if (isSelected) renderer.fillRect(left + 6, top + 6, right - left - 12, bottom - top - 12, true);
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, text, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, left + (right - left - textWidth) / 2, top + (bottom - top - lineHeight) / 2, text,
                      !isSelected, EpdFontFamily::BOLD);
  }

  // Instruction band over the top of the grid.
  const int bandHeight = renderer.getLineHeight(UI_10_FONT_ID) + 8;
  renderer.fillRect(0, 0, width, bandHeight, false);
  renderer.drawLine(0, bandHeight, width - 1, bandHeight, true);
  renderer.drawCenteredText(UI_10_FONT_ID, 4, tr(STR_TOUCH_ZONES_HINT));

  if (picker.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}
