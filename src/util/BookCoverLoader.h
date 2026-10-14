#pragma once

#include <string>

namespace BookCoverLoader {

enum class ThumbnailState { Ready, Missing, None };
// Looks for a cached thumbnail without generating one: Ready (path set), Missing (ensureThumbnail
// can make it) or None (format has no cover, or a previous attempt found none).
ThumbnailState cachedThumbnail(const std::string& bookPath, int height, std::string* path);
std::string ensureThumbnail(const std::string& bookPath, int height, bool* generated = nullptr);
std::string ensureFullCover(const std::string& bookPath, std::string* title = nullptr, std::string* author = nullptr,
                            bool* generated = nullptr);

}  // namespace BookCoverLoader
