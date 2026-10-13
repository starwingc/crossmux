#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <new>

namespace {

template <typename Predicate>
void renderFilteredPageElements(const std::vector<std::unique_ptr<PageElement>>& elements, GfxRenderer& renderer,
                                const int fontId, const int xOffset, const int yOffset, Predicate&& predicate) {
  for (const auto& element : elements) {
    if (predicate(*element)) {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

}  // namespace

void Page::addFootnote(const char* number, const char* href) {
  if (footnotes.size() >= MAX_FOOTNOTES_PER_PAGE || footnotes.allocationFailed()) return;

  auto* entry = footnotes.append();
  if (!entry) {
    LOG_ERR("PGE", "OOM: footnote storage (%u bytes)",
            static_cast<unsigned>(FootnoteList::MAX_SIZE * sizeof(FootnoteEntry)));
    return;
  }
  strncpy(entry->number, number, sizeof(entry->number) - 1);
  entry->number[sizeof(entry->number) - 1] = '\0';
  strncpy(entry->href, href, sizeof(entry->href) - 1);
  entry->href[sizeof(entry->href) - 1] = '\0';
}

bool Page::addLink(const char* href, const int16_t x, const int16_t y, const int16_t width, const int16_t height) {
  if (!href || width <= 0 || height <= 0 || links.size() >= MAX_LINKS_PER_PAGE) return false;
  const size_t hrefLen = strnlen(href, sizeof(PageLink::href));
  if (hrefLen == 0 || hrefLen == sizeof(PageLink::href)) return false;

  auto* link = links.append();
  if (!link) {
    if (links.allocationFailed()) {
      LOG_ERR("PGE", "OOM: page link storage (%u bytes)",
              static_cast<unsigned>(PageLinkList::MAX_SIZE * sizeof(PageLink)));
    }
    return false;
  }
  memcpy(link->href, href, hrefLen + 1);
  link->x = x;
  link->y = y;
  link->width = width;
  link->height = height;
  return true;
}

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(HalFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  if (!serialization::readPod(file, xPos) || !serialization::readPod(file, yPos)) return nullptr;

  auto tb = TextBlock::deserialize(file);
  if (!tb) {
    LOG_ERR("PGE", "Deserialization failed: null TextBlock");
    return nullptr;
  }

  auto line = makeUniqueNoThrow<PageLine>(std::move(tb), xPos, yPos);
  if (!line) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageLine");
    return nullptr;
  }
  return line;
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  // Images don't use fontId or text rendering
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

void PageImage::renderPlaceholder(GfxRenderer& renderer, const int xOffset, const int yOffset) const {
  imageBlock->renderPlaceholder(renderer, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(HalFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize ImageBlock
  return imageBlock->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  if (!serialization::readPod(file, xPos) || !serialization::readPod(file, yPos)) return nullptr;

  auto ib = ImageBlock::deserialize(file);
  if (!ib) return nullptr;
  auto image = makeUniqueNoThrow<PageImage>(std::move(ib), xPos, yPos);
  if (!image) LOG_ERR("PGE", "Deserialization failed: could not allocate PageImage");
  return image;
}

void PageHorizontalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  (void)fontId;
  if (width == 0 || thickness == 0) {
    return;
  }

  renderer.drawLine(xPos + xOffset, yPos + yOffset, xPos + xOffset + width - 1, yPos + yOffset, thickness, true);
}

int PageChapterTitle::heightInHalfLines(const uint8_t style) {
  switch (style) {
    case STYLE_BANNER:
      return 8;
    case STYLE_LEFT_RULE:
      return 10;
    case STYLE_ORNAMENT:
      return 11;
    default:
      return 4;
  }
}

namespace {
// Pixel-art scroll ribbon around a one-line title: a framed band with folded,
// notched tails on both sides.
void drawBanner(const GfxRenderer& renderer, const int cx, const int top, const int textWidth, const int bandHeight) {
  const int bandW = textWidth + 48;
  const int x0 = cx - bandW / 2;
  const int x1 = x0 + bandW - 1;
  const int y0 = top;
  const int y1 = top + bandHeight - 1;
  renderer.drawRect(x0, y0, bandW, bandHeight, 2, true);
  renderer.drawLine(x0 + 4, y0 + 4, x1 - 4, y0 + 4, true);  // inner rule
  renderer.drawLine(x0 + 4, y1 - 4, x1 - 4, y1 - 4, true);
  constexpr int tail = 22;
  const int tailTop = y0 + 8;
  const int tailBottom = y1 + 8;
  const int notchY = (tailTop + tailBottom) / 2;
  for (int side = -1; side <= 1; side += 2) {
    const int edge = side < 0 ? x0 : x1;
    const int outer = edge + side * tail;
    renderer.drawLine(edge, tailTop, outer, tailTop, 2, true);
    renderer.drawLine(edge, tailBottom, outer, tailBottom, 2, true);
    renderer.drawLine(outer, tailTop, outer - side * 8, notchY, 2, true);
    renderer.drawLine(outer - side * 8, notchY, outer, tailBottom, 2, true);
    renderer.drawLine(edge, y1, edge + side * 6, tailBottom, 2, true);  // fold
  }
}

// "— ◆ —" ornament centred on cx.
void drawOrnament(const GfxRenderer& renderer, const int cx, const int cy) {
  renderer.drawLine(cx - 30, cy, cx - 12, cy, 2, true);
  renderer.drawLine(cx + 12, cy, cx + 30, cy, 2, true);
  for (int d = 0; d <= 5; d++) renderer.drawLine(cx - (5 - d), cy - d, cx + (5 - d), cy - d, true);
  for (int d = 1; d <= 5; d++) renderer.drawLine(cx - (5 - d), cy + d, cx + (5 - d), cy + d, true);
}
}  // namespace

void PageChapterTitle::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  const int x = xPos + xOffset;
  const int y = yPos + yOffset;
  const int lh = renderer.getLineHeight(fontId);
  const int cx = x + width / 2;
  const std::string joined = second.empty() ? first : first + " " + second;
  switch (style) {
    case STYLE_BANNER: {
      const int tw = renderer.getTextWidth(fontId, joined.c_str());
      const int bandH = lh + 12;
      const int bandTop = y + lh;
      drawBanner(renderer, cx, bandTop, tw, bandH);
      renderer.drawText(fontId, cx - tw / 2, bandTop + 6, joined.c_str());
      break;
    }
    case STYLE_LEFT_RULE: {
      const int top = y + lh / 2;
      renderer.fillRect(x, top, 3, lh * 3, true);
      renderer.drawText(fontId, x + 16, top, first.c_str());
      if (!second.empty()) renderer.drawText(fontId, x + 16, top + lh * 2, second.c_str());
      break;
    }
    case STYLE_ORNAMENT: {
      const std::string& line1 = first;
      const int w1 = renderer.getTextWidth(fontId, line1.c_str());
      renderer.drawText(fontId, cx - w1 / 2, y + lh / 2, line1.c_str());
      int nextTop = y + lh / 2 + lh;
      if (!second.empty()) {
        const int w2 = renderer.getTextWidth(fontId, second.c_str());
        nextTop = y + lh / 2 + lh * 2;
        renderer.drawText(fontId, cx - w2 / 2, nextTop, second.c_str());
        nextTop += lh;
      }
      drawOrnament(renderer, cx, nextTop + 12);
      break;
    }
    default: {
      const int tw = renderer.getTextWidth(fontId, joined.c_str());
      renderer.drawText(fontId, cx - tw / 2, y, joined.c_str());
      break;
    }
  }
}

bool PageChapterTitle::serialize(HalFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, style);
  serialization::writePod(file, width);
  serialization::writePod(file, height);
  serialization::writeString(file, first);
  serialization::writeString(file, second);
  return true;
}

std::unique_ptr<PageChapterTitle> PageChapterTitle::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint8_t style = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  std::string first;
  std::string second;
  if (!serialization::readPod(file, xPos) || !serialization::readPod(file, yPos) ||
      !serialization::readPod(file, style) || !serialization::readPod(file, width) ||
      !serialization::readPod(file, height) || !serialization::readString(file, first, MAX_PART_BYTES) ||
      !serialization::readString(file, second, MAX_PART_BYTES)) {
    return nullptr;
  }
  auto title = makeUniqueNoThrow<PageChapterTitle>(style, width, height, std::move(first), std::move(second), xPos,
                                                   yPos);
  if (!title) LOG_ERR("PGE", "Deserialization failed: could not allocate PageChapterTitle");
  return title;
}

bool PageHorizontalRule::serialize(HalFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, width);
  serialization::writePod(file, thickness);
  return true;
}

std::unique_ptr<PageHorizontalRule> PageHorizontalRule::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint16_t width = 0;
  uint8_t thickness = 0;
  if (!serialization::readPod(file, xPos) || !serialization::readPod(file, yPos) ||
      !serialization::readPod(file, width) || !serialization::readPod(file, thickness)) {
    return nullptr;
  }

  if (width == 0 || thickness == 0) {
    LOG_ERR("PGE", "Deserialization failed: invalid horizontal rule metadata (width=%u thickness=%u)", width,
            thickness);
    return nullptr;
  }

  auto rule = makeUniqueNoThrow<PageHorizontalRule>(width, thickness, xPos, yPos);
  if (!rule) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageHorizontalRule");
    return nullptr;
  }
  return rule;
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset, [](const PageElement&) { return true; });
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset,
                             [](const PageElement& element) { return element.getTag() == TAG_PageImage; });
}

void Page::renderWithImagePlaceholders(GfxRenderer& renderer, const int fontId, const int xOffset,
                                       const int yOffset) const {
  for (const auto& element : elements) {
    if (element->getTag() == TAG_PageImage) {
      static_cast<const PageImage&>(*element).renderPlaceholder(renderer, xOffset, yOffset);
    } else {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

void Page::extractImagesNeedingDecode() {
  for (auto& element : elements) {
    if (element->getTag() != TAG_PageImage) continue;
    auto& image = static_cast<PageImage&>(*element).getImageBlock();
    if (image.needsDecode()) image.ensureExtracted();
  }
}

void Page::cacheImagesNeedingDecode(GfxRenderer& renderer, const int xOffset, const int yOffset) {
  for (auto& element : elements) {
    if (element->getTag() != TAG_PageImage) continue;
    auto& image = static_cast<PageImage&>(*element);
    if (image.getImageBlock().needsDecode()) {
      image.getImageBlock().cacheDecodedImage(renderer, image.xPos + xOffset, image.yPos + yOffset);
    }
  }
}

bool Page::serialize(HalFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    // Use getTag() method to determine type
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));

    if (!el->serialize(file)) {
      return false;
    }
  }

  // Serialize footnotes (clamp to MAX_FOOTNOTES_PER_PAGE to match addFootnote/deserialize limits)
  const uint16_t fnCount = std::min<uint16_t>(footnotes.size(), MAX_FOOTNOTES_PER_PAGE);
  serialization::writePod(file, fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    const auto& fn = footnotes[i];
    if (file.write(fn.number, sizeof(fn.number)) != sizeof(fn.number) ||
        file.write(fn.href, sizeof(fn.href)) != sizeof(fn.href)) {
      LOG_ERR("PGE", "Failed to write footnote");
      return false;
    }
  }

  const uint16_t linkCount = std::min<uint16_t>(links.size(), MAX_LINKS_PER_PAGE);
  serialization::writePod(file, linkCount);
  for (uint16_t i = 0; i < linkCount; i++) {
    const auto& link = links[i];
    if (file.write(link.href, sizeof(link.href)) != sizeof(link.href)) {
      LOG_ERR("PGE", "Failed to write link %u", i);
      return false;
    }
    serialization::writePod(file, link.x);
    serialization::writePod(file, link.y);
    serialization::writePod(file, link.width);
    serialization::writePod(file, link.height);
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(HalFile& file, const bool collectTouchLinks) {
  auto page = makeUniqueNoThrow<Page>();
  if (!page) {
    LOG_ERR("PGE", "OOM: Page (%u bytes)", static_cast<unsigned>(sizeof(Page)));
    return nullptr;
  }

  uint16_t count = 0;
  if (!serialization::readPod(file, count)) return nullptr;

  static constexpr uint16_t MAX_PAGE_ELEMENTS = 256;
  if (count > MAX_PAGE_ELEMENTS) {
    LOG_ERR("PGE", "Deserialization failed: page element count %u exceeds maximum", count);
    return nullptr;
  }
  page->elements.reserve(count);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag = 0;
    if (!serialization::readPod(file, tag)) return nullptr;

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      if (!pl) {
        return nullptr;
      }
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      if (!pi) {
        return nullptr;
      }
      page->elements.push_back(std::move(pi));
    } else if (tag == TAG_PageHorizontalRule) {
      auto rule = PageHorizontalRule::deserialize(file);
      if (!rule) {
        return nullptr;
      }
      page->elements.push_back(std::move(rule));
    } else if (tag == TAG_PageChapterTitle) {
      auto title = PageChapterTitle::deserialize(file);
      if (!title) {
        return nullptr;
      }
      page->elements.push_back(std::move(title));
    } else {
      LOG_ERR("PGE", "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  // Deserialize footnotes
  uint16_t fnCount = 0;
  if (!serialization::readPod(file, fnCount)) return nullptr;
  if (fnCount > MAX_FOOTNOTES_PER_PAGE) {
    LOG_ERR("PGE", "Invalid footnote count %u", fnCount);
    return nullptr;
  }
  if (!page->footnotes.resize(fnCount)) {
    const size_t bytesToSkip = static_cast<size_t>(fnCount) * sizeof(FootnoteEntry);
    const size_t position = file.position();
    const size_t fileSize = file.size();
    LOG_ERR("PGE", "OOM: dropping %u footnotes (%u bytes)", fnCount, static_cast<unsigned>(bytesToSkip));
    if (position > fileSize || bytesToSkip > fileSize - position || !file.seek(position + bytesToSkip)) {
      LOG_ERR("PGE", "Failed to skip footnotes after OOM");
      return nullptr;
    }
  }
  for (uint16_t i = 0; i < page->footnotes.size(); i++) {
    auto& entry = page->footnotes[i];
    if (file.read(entry.number, sizeof(entry.number)) != sizeof(entry.number) ||
        file.read(entry.href, sizeof(entry.href)) != sizeof(entry.href)) {
      LOG_ERR("PGE", "Failed to read footnote %u", i);
      return nullptr;
    }
    entry.number[sizeof(entry.number) - 1] = '\0';
    entry.href[sizeof(entry.href) - 1] = '\0';
  }

  uint16_t linkCount = 0;
  if (!serialization::readPod(file, linkCount)) return nullptr;
  if (linkCount > MAX_LINKS_PER_PAGE) {
    LOG_ERR("PGE", "Invalid link count %u", linkCount);
    return nullptr;
  }
  if (!collectTouchLinks || !page->links.resize(linkCount)) {
    constexpr size_t SERIALIZED_LINK_SIZE = FOOTNOTE_HREF_LEN + 4 * sizeof(int16_t);
    const size_t bytesToSkip = static_cast<size_t>(linkCount) * SERIALIZED_LINK_SIZE;
    const size_t position = file.position();
    const size_t fileSize = file.size();
    if (collectTouchLinks) {
      LOG_ERR("PGE", "OOM: dropping %u page links (%u bytes)", linkCount, static_cast<unsigned>(bytesToSkip));
    }
    if (position > fileSize || bytesToSkip > fileSize - position || !file.seek(position + bytesToSkip)) {
      LOG_ERR("PGE", "Failed to skip page links");
      return nullptr;
    }
    return page;
  }
  for (uint16_t i = 0; i < linkCount; i++) {
    auto& link = page->links[i];
    if (file.read(link.href, sizeof(link.href)) != sizeof(link.href)) {
      LOG_ERR("PGE", "Failed to read link %u", i);
      return nullptr;
    }
    link.href[sizeof(link.href) - 1] = '\0';
    if (!serialization::readPod(file, link.x) || !serialization::readPod(file, link.y) ||
        !serialization::readPod(file, link.width) || !serialization::readPod(file, link.height)) {
      return nullptr;
    }
    if (link.href[0] == '\0' || link.width <= 0 || link.height <= 0) {
      LOG_ERR("PGE", "Invalid link geometry %u", i);
      return nullptr;
    }
  }

  return page;
}
