#pragma once

#include <cstdint>

// Spacing of adjacent fullwidth punctuation (JLREQ 3.1.4; CSS text-spacing-trim
// trim-adjacent). A fullwidth bracket or comma draws its ink in one half of an em
// and leaves the other half blank. When two of them meet, half an em of that blank
// space is removed, so 「。」」 or 「」「」 do not look spaced out.
namespace PunctuationSpacing {

// Opening brackets: ink in the right half (bottom half in vertical text).
inline bool isOpening(const uint32_t cp) {
  switch (cp) {
    case 0x3008:  // 〈
    case 0x300A:  // 《
    case 0x300C:  // 「
    case 0x300E:  // 『
    case 0x3010:  // 【
    case 0x3014:  // 〔
    case 0x3016:  // 〖
    case 0x3018:  // 〘
    case 0x301D:  // 〝
    case 0xFF08:  // （
    case 0xFF3B:  // ［
    case 0xFF5B:  // ｛
    case 0xFF5F:  // ｟
      return true;
    default:
      return false;
  }
}

// Closing brackets, commas and full stops: ink in the left half (top half in
// vertical text).
inline bool isClosing(const uint32_t cp) {
  switch (cp) {
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0x3009:  // 〉
    case 0x300B:  // 》
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0x3011:  // 】
    case 0x3015:  // 〕
    case 0x3017:  // 〗
    case 0x3019:  // 〙
    case 0x301F:  // 〟
    case 0xFF09:  // ）
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0xFF3D:  // ］
    case 0xFF5D:  // ｝
    case 0xFF60:  // ｠
      return true;
    default:
      return false;
  }
}

// True when half an em is removed between `left` and `right`. Either the
// closing mark gives up its trailing blank half or the opening mark its leading
// one; both move everything from `right` on back by half an em.
inline bool trimsHalfBetween(const uint32_t left, const uint32_t right) {
  if (isClosing(left)) return isClosing(right) || isOpening(right);
  return isOpening(left) && isOpening(right);
}

}  // namespace PunctuationSpacing
