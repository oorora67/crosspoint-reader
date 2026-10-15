#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Reader font size is stored as an actual point size (see CrossPointSettings::
// fontPointSize), not an abstract Small/Medium/Large slot. The selectable sizes
// therefore come from whichever family is active: the built-in or vector set
// below, or the .cpfont files a user installed for an SD family.

// The built-in Noto Serif / Noto Sans families are compiled in at exactly these
// point sizes (see the global font objects in main.cpp).
inline constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {12, 14, 16, 18};

// Vector (.ttf/.otf) fonts offer every whole point size from 8 through 22.
inline constexpr uint8_t VECTOR_READER_POINT_SIZES[] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22};

// Point sizes of the built-in UI fonts (SMALL, UI_10, UI_12).
inline constexpr uint8_t UI_FONT_POINT_SIZES[] = {8, 10, 12};

// True when an SD family can be the system (UI) font: a vector (TTF/OTF) family,
// which renders at any size, or a .cpfont family with at least one
// UI_FONT_POINT_SIZES file. Vector families exist only on PSRAM boards.
inline bool isUiFontCandidate(const SdCardFontFamilyInfo& family) {
  if (family.vector) return true;
  for (const uint8_t pt : UI_FONT_POINT_SIZES) {
    if (family.hasSize(pt)) return true;
  }
  return false;
}

// Point sizes selectable for the active reader font, ascending: the vector set
// or installed .cpfont sizes for a known SD family, otherwise the built-in set.
// Never returns empty.
std::vector<uint8_t> readerFontPointSizes(const SdCardFontRegistry* registry, const char* sdFamilyName);

// Closest entry in `sizes` (ascending, `count` > 0) to `pt`; ties resolve to the
// smaller size. Takes a raw range rather than a vector because getReaderFontId()
// runs inside the page render loop and must not allocate.
uint8_t snapToNearestPointSize(const uint8_t* sizes, size_t count, uint8_t pt);

inline uint8_t snapToNearestPointSize(const std::vector<uint8_t>& sizes, const uint8_t pt) {
  return sizes.empty() ? pt : snapToNearestPointSize(sizes.data(), sizes.size(), pt);
}
