#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo160.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  // The logo already shows the product name.
  renderer.drawImage(Logo160, (pageWidth - 160) / 2, (pageHeight - 160) / 2, 160, 160);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 100, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
