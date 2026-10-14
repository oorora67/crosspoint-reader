// Vertical (tategaki) layout for ParsedText. Columns run top to bottom and the
// parser places them right to left. Line-break prohibitions (kinsoku) come from
// the token boundaries addWord() already built for horizontal CJK text: a column
// only ends where TokenBoundary::allowsBreak() permits a line break.
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <VerticalText.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "ParsedText.h"
#include "TokenBoundary.h"

namespace {

bool isHalfSize(const EpdFontFamily::Style style) { return (style & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0; }

uint32_t countCodepoints(const std::string_view text) {
  uint32_t count = 0;
  for (const char c : text) {
    if ((static_cast<uint8_t>(c) & 0xC0) != 0x80) count++;
  }
  return count;
}

// Copies text without soft hyphens (U+00AD): vertical columns never hyphenate.
std::string withoutSoftHyphens(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (static_cast<uint8_t>(text[i]) == 0xC2 && i + 1 < text.size() && static_cast<uint8_t>(text[i + 1]) == 0xAD) {
      ++i;
      continue;
    }
    out.push_back(text[i]);
  }
  return out;
}

// Codepoints that never start a cell of their own: they belong to the character before.
bool joinsPrevious(const uint32_t cp) {
  return utf8IsCombiningMark(cp) || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF) ||
         cp == 0x3099 || cp == 0x309A;
}

}  // namespace

struct ParsedText::VerticalCell {
  std::string_view text;
  VerticalText::CellKind kind;
  uint16_t length;      // extent down the column, before character spacing
  uint16_t codepoints;  // source codepoints the cell covers
};

bool ParsedText::beginsWithIdeographicSpace() const {
  if (words.empty()) return false;
  const auto* p = reinterpret_cast<const unsigned char*>(wordStore.cstr(words[0]));
  return utf8NextCodepoint(&p) == 0x3000;
}

// Splits word wordIndex into cells: one per upright character, and one per run of other
// text, which is set as tate-chu-yoko when short and sideways otherwise.
template <typename Fn>
void ParsedText::forEachVerticalCell(const GfxRenderer& renderer, const int fontId, const int em,
                                     const size_t wordIndex, Fn&& fn) const {
  const std::string_view word = wordAt(wordIndex);
  const EpdFontFamily::Style style = getWordStyleAt(wordIndex);
  const uint16_t cellEm = static_cast<uint16_t>(isHalfSize(style) ? std::max(1, em / 2) : em);
  const auto* const begin = reinterpret_cast<const unsigned char*>(word.data());
  const auto* const end = begin + word.size();

  const unsigned char* cellStart = nullptr;
  uint16_t cellCodepoints = 0;
  bool cellIsRun = false;
  uint32_t cellCp = 0;

  const auto emit = [&](const unsigned char* cellEnd) {
    if (!cellStart) return;
    const std::string_view text(reinterpret_cast<const char*>(cellStart), cellEnd - cellStart);
    if (!cellIsRun) {
      fn(VerticalCell{text, VerticalText::uprightKind(cellCp), cellEm, cellCodepoints});
    } else if (VerticalText::isAutoCombined(text.data(), text.size())) {
      fn(VerticalCell{text, VerticalText::CellKind::Combined, cellEm, cellCodepoints});
    } else {
      const int advance = renderer.getTextAdvanceX(fontId, withoutSoftHyphens(text).c_str(), style);
      fn(VerticalCell{text, VerticalText::CellKind::Sideways, static_cast<uint16_t>(std::max(1, advance)),
                      cellCodepoints});
    }
    cellStart = nullptr;
    cellCodepoints = 0;
  };

  const unsigned char* p = begin;
  while (p < end) {
    const unsigned char* cpStart = p;
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (cellStart && joinsPrevious(cp)) {
      cellCodepoints++;
      continue;
    }
    const bool upright = VerticalText::isUpright(cp);
    if (cellStart && (upright || !cellIsRun)) {
      emit(cpStart);
    }
    if (!cellStart) {
      cellStart = cpStart;
      cellIsRun = !upright;
      cellCp = cp;
    }
    cellCodepoints++;
  }
  emit(p);
}

uint16_t ParsedText::verticalWordLength(const GfxRenderer& renderer, const int fontId, const int em,
                                        const size_t wordIndex) const {
  int length = 0;
  forEachVerticalCell(renderer, fontId, em, wordIndex, [&](const VerticalCell& cell) {
    length += std::max(1, cell.length + blockStyle.characterSpacing);
  });
  return static_cast<uint16_t>(std::min(length, static_cast<int>(UINT16_MAX)));
}

// Space between word wordIndex and the one before it: a word space for space-separated text
// (Latin runs), nothing for CJK and attached words.
int ParsedText::verticalGapBefore(const GfxRenderer& renderer, const int fontId, const size_t wordIndex) const {
  if (wordIndex == 0 || wordContinues[wordIndex] || wordNoSpaceBefore[wordIndex]) return 0;
  return renderer.getSpaceWidth(fontId, getWordStyleAt(wordIndex - 1));
}

// Length of the ruby a group leader carries (half-size cells), or 0.
int ParsedText::verticalRubyLength(const int em, const size_t wordIndex) const {
  if (wordIndex >= rubyTexts.size() || rubyTexts[wordIndex].empty() ||
      (wordStyles[wordIndex] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    return 0;
  }
  return static_cast<int>(countCodepoints(rubyTexts[wordIndex])) * std::max(1, em / 2);
}

void ParsedText::layoutVerticalColumns(
    const GfxRenderer& renderer, const int fontId, const uint16_t columnLength,
    const std::function<void(std::unique_ptr<TextBlock>, uint32_t, size_t)>& processColumn, const bool includeLastLine,
    const int8_t characterSpacing) {
  if (words.empty()) {
    return;
  }
  blockStyle.characterSpacing = characterSpacing;
  blockStyle.isRtl = false;
  isNaturalAlign = blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left;
  prepareSdFontMetrics(renderer, fontId);

  const int em = std::max(1, renderer.getVerticalEm(fontId));
  const size_t wordCount = words.size();

  // Word lengths down the column. A ruby group longer than its base text is widened by the
  // difference, charged to the group leader: groups never break, so only the total matters here.
  std::vector<uint16_t> wordLengths(wordCount);
  for (size_t i = 0; i < wordCount; ++i) {
    wordLengths[i] = verticalWordLength(renderer, fontId, em, i);
  }
  for (size_t i = 0; i < wordCount; ++i) {
    const int rubyLength = verticalRubyLength(em, i);
    if (rubyLength == 0) continue;
    int baseLength = wordLengths[i];
    for (size_t j = i + 1; j < wordCount && (wordStyles[j] & EpdFontFamily::RUBY_CONTINUE) != 0; ++j) {
      baseLength += wordLengths[j];
    }
    if (rubyLength > baseLength) {
      wordLengths[i] = static_cast<uint16_t>(std::min(wordLengths[i] + rubyLength - baseLength, int{UINT16_MAX}));
    }
  }

  // Greedy column breaking at the last allowed boundary that fits.
  std::vector<size_t> columnEnds;
  size_t start = 0;
  while (start < wordCount) {
    int pos = (start == 0 && !firstLineConsumed) ? verticalFirstColumnIndent(renderer, fontId, em) : 0;
    size_t lastBreak = start;
    size_t i = start;
    for (; i < wordCount; ++i) {
      const int gap = i > start ? verticalGapBefore(renderer, fontId, i) : 0;
      if (i > start && TokenBoundary::allowsBreak(wordContinues[i], wordNoSpaceBefore[i])) {
        lastBreak = i;
      }
      if (i > start && pos + gap + wordLengths[i] > columnLength) {
        break;
      }
      pos += gap + wordLengths[i];
    }
    // No allowed break fits: break where the column is full rather than overflow the page.
    const size_t end = (i == wordCount) ? wordCount : (lastBreak > start ? lastBreak : i);
    columnEnds.push_back(end);
    start = end;
  }

  const size_t columnCount = includeLastLine ? columnEnds.size() : columnEnds.size() - 1;
  size_t first = 0;
  for (size_t c = 0; c < columnCount; ++c) {
    if (!extractColumn(first, columnEnds[c], columnLength, em, columnEnds[c] == wordCount, processColumn, renderer,
                       fontId)) {
      droppedWords = true;
    }
    first = columnEnds[c];
  }
  if (columnCount > 0) {
    releaseConsumedWords(columnEnds[columnCount - 1]);
  }
}

// JLREQ indents a paragraph by one full-width character. A paragraph that already opens with
// U+3000 carries its own indent.
int ParsedText::verticalFirstColumnIndent(const GfxRenderer& renderer, const int fontId, const int em) const {
  if (resolveFirstLineIndent(true, renderer, fontId) <= 0 || beginsWithIdeographicSpace()) return 0;
  return em;
}

bool ParsedText::extractColumn(const size_t first, const size_t last, const int columnLength, const int em,
                               const bool isLastColumn,
                               const std::function<void(std::unique_ptr<TextBlock>, uint32_t, size_t)>& processColumn,
                               const GfxRenderer& renderer, const int fontId) {
  const bool firstOfParagraph = !firstLineConsumed && first == 0;
  const int tracking = blockStyle.characterSpacing;

  std::vector<std::string> cellTexts;
  std::vector<int16_t> cellOffsets;
  std::vector<EpdFontFamily::Style> cellStyles;
  std::vector<VerticalText::CellKind> cellKinds;
  std::vector<std::string> cellRuby;
  std::vector<TextBlock::SourceRange> cellRanges;
  std::vector<uint16_t> cellLengths;
  std::vector<uint8_t> cellLinkIds;
  bool anyRuby = false;
  uint16_t paragraphStartCell = UINT16_MAX;

  // Ruby group state. JLREQ 3.3.2: when the ruby is longer than its base, the base characters
  // are spaced 1:2:1 (half a unit before the first, a whole unit between, half after the last).
  size_t groupEnd = 0;  // one past the group's last word; 0 when no group is open
  int groupExtra = 0;
  int groupCells = 0;
  int groupCellIndex = 0;
  int groupPadDone = 0;

  int pos = firstOfParagraph ? verticalFirstColumnIndent(renderer, fontId, em) : 0;
  for (size_t i = first; i < last; ++i) {
    if (i > first) pos += verticalGapBefore(renderer, fontId, i);

    const int rubyLength = verticalRubyLength(em, i);
    if (rubyLength > 0) {
      groupEnd = i + 1;
      while (groupEnd < last && (wordStyles[groupEnd] & EpdFontFamily::RUBY_CONTINUE) != 0) groupEnd++;
      int baseLength = 0;
      groupCells = 0;
      for (size_t j = i; j < groupEnd; ++j) {
        baseLength += verticalWordLength(renderer, fontId, em, j);
        forEachVerticalCell(renderer, fontId, em, j, [&](const VerticalCell&) { groupCells++; });
      }
      groupExtra = std::max(0, rubyLength - baseLength);
      groupCellIndex = 0;
      groupPadDone = 0;
    }

    const EpdFontFamily::Style wordStyle =
        static_cast<EpdFontFamily::Style>(getWordStyleAt(i) & ~EpdFontFamily::RUBY_CONTINUE);
    const uint32_t wordStart = visibleOffsetAt(i);
    const uint8_t linkId = wordLinkIds[i];
    uint32_t codepointsBefore = 0;
    bool firstCellOfWord = true;
    const bool inGroup = groupEnd > i;

    forEachVerticalCell(renderer, fontId, em, i, [&](const VerticalCell& cell) {
      EpdFontFamily::Style style = wordStyle;
      std::string ruby;
      if (inGroup) {
        if (groupExtra > 0 && groupCells > 0) {
          const int padTarget = groupExtra * (2 * groupCellIndex + 1) / (2 * groupCells);
          pos += padTarget - groupPadDone;
          groupPadDone = padTarget;
        }
        if (groupCellIndex == 0) {
          ruby = rubyTexts[i];
          anyRuby = true;
        } else {
          style = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::RUBY_CONTINUE);
        }
        groupCellIndex++;
      }

      if (firstOfParagraph && i == 0 && firstCellOfWord) {
        paragraphStartCell = static_cast<uint16_t>(cellTexts.size());
      }
      const uint32_t start = sourceOffsetAfter(wordStart, codepointsBefore);
      uint32_t end = sourceOffsetAfter(wordStart, codepointsBefore + cell.codepoints);
      codepointsBefore += cell.codepoints;
      if (i + 1 < words.size() && visibleOffsetAt(i + 1) > start) {
        end = std::min(end, visibleOffsetAt(i + 1));
      }

      cellTexts.push_back(withoutSoftHyphens(cell.text));
      cellOffsets.push_back(static_cast<int16_t>(pos));
      cellStyles.push_back(style);
      cellKinds.push_back(cell.kind);
      cellRuby.push_back(std::move(ruby));
      cellRanges.push_back({start, end});
      cellLengths.push_back(cell.length);
      cellLinkIds.push_back(linkId);
      pos += std::max(1, cell.length + tracking);
      firstCellOfWord = false;
    });

    if (inGroup && i + 1 == groupEnd) {
      pos += groupExtra - groupPadDone;
      groupEnd = 0;
    }
  }

  const int cellCount = static_cast<int>(cellTexts.size());
  const int spare = columnLength - pos;
  if (spare > 0 && cellCount > 0) {
    int shift = 0;
    switch (blockStyle.alignment) {
      case CssTextAlign::Center:
        shift = spare / 2;
        break;
      case CssTextAlign::Right:
        shift = spare;
        break;
      case CssTextAlign::Justify:
        // Spread the leftover between characters, unless the column ends early so far that
        // the text would look gapped (a long unbreakable word moved to the next column).
        if (!isLastColumn && cellCount > 1 && spare / (cellCount - 1) <= em / 2) {
          for (int k = 1; k < cellCount; ++k) {
            cellOffsets[k] = static_cast<int16_t>(cellOffsets[k] + spare * k / (cellCount - 1));
          }
        }
        break;
      default:
        break;
    }
    if (shift != 0) {
      for (auto& offset : cellOffsets) offset = static_cast<int16_t>(offset + shift);
    }
  }

  // Link spans run along the column: x is the offset down it and width the length.
  std::vector<TextBlock::LinkSpan> links;
  std::vector<uint8_t> linkIdsSeen;
  for (int k = 0; k < cellCount; ++k) {
    const uint8_t linkId = cellLinkIds[k];
    if (linkId == 0 || linkId > linkTargets.size()) continue;
    const int cellEnd = cellOffsets[k] + cellLengths[k];
    size_t spanIndex = 0;
    while (spanIndex < linkIdsSeen.size() && linkIdsSeen[spanIndex] != linkId) spanIndex++;
    if (spanIndex == linkIdsSeen.size()) {
      links.emplace_back();
      auto& span = links.back();
      strncpy(span.href, linkTargets[linkId - 1].c_str(), sizeof(span.href) - 1);
      span.href[sizeof(span.href) - 1] = '\0';
      span.x = cellOffsets[k];
      span.width = static_cast<int16_t>(cellLengths[k]);
      span.topLift = 0;
      linkIdsSeen.push_back(linkId);
    } else {
      auto& span = links[spanIndex];
      span.width = static_cast<int16_t>(std::max<int>(span.width, cellEnd - span.x));
    }
  }

  if (!anyRuby) cellRuby.clear();
  auto block = makeUniqueNoThrow<TextBlock>(cellTexts, cellOffsets, cellStyles, std::vector<uint8_t>{},
                                            std::vector<uint16_t>{}, blockStyle, std::move(cellRuby), std::move(links),
                                            cellRanges, paragraphStartCell, cellKinds, static_cast<uint16_t>(em));
  if (!block || !block->valid()) {
    LOG_ERR("PTX", "Dropping column: TextBlock or arena allocation failed");
    return false;
  }
  processColumn(std::move(block), visibleOffsetAt(first), last - first);
  return true;
}
