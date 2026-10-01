#pragma once

#include <FreeInkUI.h>
#include <I18n.h>

#include <cstdint>

namespace keyboard_layouts {

// Languages are named by their _language_code rather than the Language enum,
// so a build that compiles only some UI languages keeps every layout.
struct LayoutInfo {
  freeink::ui::KeyboardLayoutId id;
  const char* languageCode;
  const char* label;  // native language name, shown in the layout list
};

// Table position is the persisted bit assignment. Keep existing rows in place
// and append new layouts so SDK enum changes cannot reinterpret saved masks.
inline constexpr LayoutInfo ALL[] = {
    {freeink::ui::KeyboardLayoutId::QwertyEn, "EN", "English"},
    {freeink::ui::KeyboardLayoutId::AzertyFr, "FR", "Français"},
    {freeink::ui::KeyboardLayoutId::QwertzDe, "DE", "Deutsch"},
    {freeink::ui::KeyboardLayoutId::SpanishEs, "ES", "Español"},
    {freeink::ui::KeyboardLayoutId::CyrillicRu, "RU", "Русский"},
    {freeink::ui::KeyboardLayoutId::CyrillicUk, "UK", "Українська"},
    {freeink::ui::KeyboardLayoutId::CyrillicBe, "BE", "Беларуская"},
    {freeink::ui::KeyboardLayoutId::CyrillicKk, "KK", "Қазақша"},
    {freeink::ui::KeyboardLayoutId::HebrewIl, "HE", "עברית"},
};
inline constexpr uint8_t COUNT = sizeof(ALL) / sizeof(ALL[0]);
static_assert(COUNT <= 16, "keyboard layout mask is uint16_t");

inline constexpr uint16_t bitAt(const uint8_t i) { return static_cast<uint16_t>(1u << i); }
// Symbol layers have no Latin letters, so credentials and URLs require at
// least one of these layouts to remain enabled.
inline constexpr uint16_t LATIN_BITS = bitAt(0) | bitAt(1) | bitAt(2) | bitAt(3);

uint16_t enabled();
freeink::ui::KeyboardLayoutId startingLayout();
freeink::ui::KeyboardLayoutId next(freeink::ui::KeyboardLayoutId current);

}  // namespace keyboard_layouts
