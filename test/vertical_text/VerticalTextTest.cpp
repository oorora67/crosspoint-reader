#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <VerticalText.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using VerticalText::CellKind;

namespace {

struct Column {
  std::vector<std::string> cells;
  std::vector<int16_t> offsets;
  std::vector<CellKind> kinds;
};

// Fixture metrics (stub renderer): the em is 16 px, every other glyph 8 px wide.
std::vector<Column> layout(ParsedText& text, const uint16_t columnLength) {
  GfxRenderer renderer;
  std::vector<Column> columns;
  text.layoutVerticalColumns(renderer, 0, columnLength, [&](std::unique_ptr<TextBlock> block, uint32_t, size_t) {
    EXPECT_TRUE(block->vertical());
    auto& column = columns.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      column.cells.emplace_back(block->wordText(i));
      column.offsets.push_back(block->wordXpos(i));
      column.kinds.push_back(block->cellKind(i));
    }
  });
  return columns;
}

BlockStyle topAligned() {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  return style;
}

}  // namespace

TEST(VerticalTextClassification, FollowsUax50) {
  EXPECT_TRUE(VerticalText::isUpright(0x6F22));                       // 漢
  EXPECT_TRUE(VerticalText::isUpright(0xFF21));                       // Ａ
  EXPECT_FALSE(VerticalText::isUpright('A'));                         // proportional Latin turns sideways
  EXPECT_EQ(VerticalText::uprightKind(0x3001), CellKind::Corner);     // 、
  EXPECT_EQ(VerticalText::uprightKind(0x3002), CellKind::Corner);     // 。
  EXPECT_EQ(VerticalText::uprightKind(0x3063), CellKind::SmallKana);  // っ
  EXPECT_EQ(VerticalText::uprightKind(0x30FC), CellKind::Rotated);    // ー
  EXPECT_EQ(VerticalText::uprightKind(0x300C), CellKind::Rotated);    // 「
  EXPECT_EQ(VerticalText::uprightKind(0x6F22), CellKind::Upright);
}

TEST(VerticalTextClassification, AutoCombinesShortRuns) {
  EXPECT_TRUE(VerticalText::isAutoCombined("12", 2));
  EXPECT_TRUE(VerticalText::isAutoCombined("7", 1));
  EXPECT_TRUE(VerticalText::isAutoCombined("!?", 2));
  EXPECT_FALSE(VerticalText::isAutoCombined("123", 3));
  EXPECT_FALSE(VerticalText::isAutoCombined("ab", 2));
}

TEST(VerticalTextClassification, AutoModeNeedsACjkLanguage) {
  EXPECT_TRUE(VerticalText::isCjkLanguageTag("ja"));
  EXPECT_TRUE(VerticalText::isCjkLanguageTag("JA-JP"));
  EXPECT_TRUE(VerticalText::isCjkLanguageTag("zh-Hant"));
  EXPECT_TRUE(VerticalText::isCjkLanguageTag("ko_KR"));
  EXPECT_FALSE(VerticalText::isCjkLanguageTag("ar"));
  EXPECT_FALSE(VerticalText::isCjkLanguageTag("he"));
  EXPECT_FALSE(VerticalText::isCjkLanguageTag("fa-IR"));
  EXPECT_FALSE(VerticalText::isCjkLanguageTag("jav"));
  EXPECT_FALSE(VerticalText::isCjkLanguageTag(""));
}

TEST(VerticalLayout, ClosingPunctuationNeverStartsAColumn) {
  ParsedText text(false, false, topAligned(), 0);
  text.addWord("あいう。えお", EpdFontFamily::REGULAR);
  // Three cells per column; 。 cannot start a column, so う moves down with it.
  const auto columns = layout(text, 48);
  ASSERT_EQ(columns.size(), 3u);
  EXPECT_EQ(columns[0].cells, (std::vector<std::string>{"あ", "い"}));
  EXPECT_EQ(columns[1].cells, (std::vector<std::string>{"う", "。", "え"}));
  EXPECT_EQ(columns[1].offsets, (std::vector<int16_t>{0, 16, 32}));
  EXPECT_EQ(columns[1].kinds[1], CellKind::Corner);
  EXPECT_EQ(columns[2].cells, (std::vector<std::string>{"お"}));
}

TEST(VerticalLayout, LatinRunsTurnSidewaysAndShortNumbersCombine) {
  ParsedText text(false, false, topAligned(), 0);
  text.addWord("第12回", EpdFontFamily::REGULAR);
  text.addWord("abc", EpdFontFamily::REGULAR);
  const auto columns = layout(text, 200);
  ASSERT_EQ(columns.size(), 1u);
  EXPECT_EQ(columns[0].cells, (std::vector<std::string>{"第", "12", "回", "abc"}));
  EXPECT_EQ(columns[0].kinds,
            (std::vector<CellKind>{CellKind::Upright, CellKind::Combined, CellKind::Upright, CellKind::Sideways}));
  // 12 fills one em cell; the word space (4 px) separates the Latin word.
  EXPECT_EQ(columns[0].offsets, (std::vector<int16_t>{0, 16, 32, 52}));
}

TEST(VerticalLayout, IndentsByOneEmUnlessTheParagraphOpensWithIdeographicSpace) {
  ParsedText indented(false, false, topAligned(), 2);
  indented.addWord("あい", EpdFontFamily::REGULAR);
  EXPECT_EQ(layout(indented, 200)[0].offsets.front(), 16);

  ParsedText ownIndent(false, false, topAligned(), 2);
  ownIndent.addWord("\xE3\x80\x80あい", EpdFontFamily::REGULAR);
  EXPECT_EQ(layout(ownIndent, 200)[0].offsets.front(), 0);
}

TEST(VerticalLayout, LongRubySpacesItsBaseOneTwoOne) {
  ParsedText text(false, false, topAligned(), 0);
  text.addWord("漢字", EpdFontFamily::REGULAR);
  // Five ruby characters at half an em (40 px) over a 32 px base: 8 px spread 1:2:1.
  text.setRubyGroupAt(0, 2, "かんじかん");
  text.addWord("あ", EpdFontFamily::REGULAR, false, /*attachToPrevious=*/true);
  const auto columns = layout(text, 200);
  ASSERT_EQ(columns.size(), 1u);
  EXPECT_EQ(columns[0].cells, (std::vector<std::string>{"漢", "字", "あ"}));
  EXPECT_EQ(columns[0].offsets, (std::vector<int16_t>{2, 22, 40}));
}

TEST(VerticalLayout, JustifiedColumnsFillTheirLength) {
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  ParsedText text(false, false, style, 0);
  text.addWord("あいう。えお", EpdFontFamily::REGULAR);
  const auto columns = layout(text, 52);
  ASSERT_EQ(columns.size(), 3u);
  // あい leaves 20 px, more than half an em per gap, so it stays top-aligned; う。え spreads 4 px.
  EXPECT_EQ(columns[0].offsets, (std::vector<int16_t>{0, 16}));
  EXPECT_EQ(columns[1].offsets, (std::vector<int16_t>{0, 18, 36}));
}
