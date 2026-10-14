#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Glyph orientation for vertical (tategaki) text. The classes follow the
// Vertical_Orientation property of Unicode UAX #50 for the characters a
// Japanese book uses; fonts are drawn from their horizontal glyphs, so the
// "vert" alternates are synthesized by moving or rotating those glyphs.
namespace VerticalText {

// How one cell of a vertical column is drawn. Stored per word in TextBlock,
// so values are part of the section cache format.
enum class CellKind : uint8_t {
  Upright = 0,    // UAX #50 U: drawn as in horizontal text, centred in the column
  Corner = 1,     // Tu: comma and full stop move to the upper-right quarter of the cell
  SmallKana = 2,  // Tu: small kana move up and right
  Rotated = 3,    // Tr/R: brackets, dashes and the long vowel mark turn 90 degrees clockwise
  Sideways = 4,   // R: a run of Latin text turned 90 degrees clockwise
  Combined = 5,   // tate-chu-yoko: a short horizontal run set upright in one cell
};

// Characters set upright in vertical text (UAX #50 U and Tu/Tr). Anything else,
// such as proportional Latin, Greek or Cyrillic, is set sideways.
inline bool isUpright(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)       // Hangul Jamo
         || (cp >= 0x2E80 && cp <= 0x2FFF)    // CJK and Kangxi radicals, ideographic description
         || (cp >= 0x3000 && cp <= 0x33FF)    // CJK punctuation, kana, Bopomofo, enclosed and squared forms
         || (cp >= 0x3400 && cp <= 0x4DBF)    // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)    // CJK Unified Ideographs
         || (cp >= 0xAC00 && cp <= 0xD7FF)    // Hangul syllables
         || (cp >= 0xE000 && cp <= 0xF8FF)    // private use (gaiji)
         || (cp >= 0xF900 && cp <= 0xFAFF)    // CJK Compatibility Ideographs
         || (cp >= 0xFE10 && cp <= 0xFE1F)    // vertical forms
         || (cp >= 0xFE30 && cp <= 0xFE4F)    // CJK compatibility forms
         || (cp >= 0xFF01 && cp <= 0xFFEF)    // fullwidth and halfwidth forms
         || (cp >= 0x20000 && cp <= 0x3FFFF)  // CJK Extensions B and later
         || (cp >= 0x2460 && cp <= 0x24FF)    // enclosed alphanumerics
         || (cp >= 0x25A0 && cp <= 0x27BF)    // geometric shapes, symbols, dingbats
         || cp == 0x00A7 || cp == 0x00B6 || cp == 0x00D7 || cp == 0x00F7  // section, pilcrow, times, divide
         || cp == 0x203B;                                                 // reference mark
}

// Comma and full stop: UAX #50 Tu, set in the upper-right quarter of the cell.
inline bool isCornerPunctuation(const uint32_t cp) {
  return cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF0E || cp == 0xFF61 || cp == 0xFF64;
}

// Small kana (JLREQ cl-11).
inline bool isSmallKana(const uint32_t cp) {
  // ぁぃぅぇぉっゃゅょゎゕゖ ァィゥェォッャュョヮヵヶ
  static constexpr uint16_t SMALL_KANA[] = {0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085,
                                            0x3087, 0x308E, 0x3095, 0x3096, 0x30A1, 0x30A3, 0x30A5, 0x30A7,
                                            0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6};
  for (const uint16_t kana : SMALL_KANA) {
    if (cp == kana) return true;
  }
  return cp >= 0x31F0 && cp <= 0x31FF;  // Katakana Phonetic Extensions
}

// Characters whose vertical form is the horizontal glyph turned 90 degrees
// clockwise (UAX #50 Tr and R within the upright blocks). Dashes and ellipses
// outside those blocks are not upright and turn with the sideways run instead.
inline bool isRotated(const uint32_t cp) {
  if (cp >= 0x3008 && cp <= 0x3011) return true;  // 〈〉《》「」『』【】
  if (cp >= 0x3014 && cp <= 0x301B) return true;  // 〔〕〖〗〘〙〚〛
  switch (cp) {
    case 0x301C:  // 〜
    case 0x3030:  // 〰
    case 0x30A0:  // ゠
    case 0x30FC:  // ー
    case 0xFF08:  // （
    case 0xFF09:  // ）
    case 0xFF0D:  // －
    case 0xFF1A:  // ：
    case 0xFF1B:  // ；
    case 0xFF1C:  // ＜
    case 0xFF1E:  // ＞
    case 0xFF3B:  // ［
    case 0xFF3D:  // ］
    case 0xFF3F:  // ＿
    case 0xFF5B:  // ｛
    case 0xFF5C:  // ｜
    case 0xFF5D:  // ｝
    case 0xFF5E:  // ～
    case 0xFF5F:  // ｟
    case 0xFF60:  // ｠
    case 0xFF62:  // ｢
    case 0xFF63:  // ｣
    case 0xFFE3:  // ￣
      return true;
    default:
      return false;
  }
}

// Cell kind of one upright character (isUpright(cp) is true).
inline CellKind uprightKind(const uint32_t cp) {
  if (isCornerPunctuation(cp)) return CellKind::Corner;
  if (isSmallKana(cp)) return CellKind::SmallKana;
  if (isRotated(cp)) return CellKind::Rotated;
  return CellKind::Upright;
}

// True when a run of non-upright text is set as tate-chu-yoko: one or two ASCII
// digits, a single ASCII letter, or a pair of "!" and "?" (JLREQ 3.2.2 and the
// usual "!!" and "!?" practice). Longer runs are set sideways.
inline bool isAutoCombined(const char* text, const size_t len) {
  if (len == 0 || len > 2) return false;
  const auto isDigit = [](const char c) { return c >= '0' && c <= '9'; };
  const auto isLetter = [](const char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
  const auto isMark = [](const char c) { return c == '!' || c == '?'; };
  if (len == 1) return isDigit(text[0]) || isLetter(text[0]) || isMark(text[0]);
  return (isDigit(text[0]) && isDigit(text[1])) || (isMark(text[0]) && isMark(text[1]));
}

// True for a Japanese, Chinese or Korean language tag ("ja", "zh-Hant", "ko_KR"). Arabic,
// Hebrew and Persian books also declare a right-to-left page progression, so Auto writing
// mode needs the language as well before it sets a book vertically.
inline bool isCjkLanguageTag(const std::string_view tag) {
  if (tag.size() < 2 || (tag.size() > 2 && tag[2] != '-' && tag[2] != '_')) return false;
  const auto lower = [](const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
  const char first = lower(tag[0]);
  const char second = lower(tag[1]);
  return (first == 'j' && second == 'a') || (first == 'z' && second == 'h') || (first == 'k' && second == 'o');
}

}  // namespace VerticalText
