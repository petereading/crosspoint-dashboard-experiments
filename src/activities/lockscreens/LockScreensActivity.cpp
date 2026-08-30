#include "LockScreensActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void LockScreensActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void LockScreensActivity::loop() {
  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (selectorIndex) {
      case 0:
        onClockOpen();
        break;
      case 1:
        onWeatherOpen();
        break;
      case 2:
        onCustomImageOpen();
        break;
      case 3:
        onMoonOpen();
        break;
      case 4:
        onRssOpen();
        break;
      case 5:
        onTodayOpen();
        break;
      case 6:
        onQuoteOpen();
        break;
      case 7:
        onBitcoinOpen();
        break;
      case 8:
        onSolarOpen();
        break;
      default:
        break;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void LockScreensActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LOCK_SCREENS));

  const std::vector<const char*> items = {
      tr(STR_CLOCK_DASHBOARD), tr(STR_WEATHER_DASHBOARD), tr(STR_CUSTOM_IMAGE_DASHBOARD),
      tr(STR_MOON_DASHBOARD),  tr(STR_RSS_DASHBOARD),     tr(STR_TODAY_DASHBOARD),
      tr(STR_QUOTE_DASHBOARD), tr(STR_BITCOIN_DASHBOARD), tr(STR_SOLAR_DASHBOARD)};
  const std::vector<UIIcon> icons = {UIIcon::Clock, UIIcon::Weather, UIIcon::Image,  UIIcon::Clock,  UIIcon::Github,
                                     UIIcon::Book,  UIIcon::Image,   UIIcon::Github, UIIcon::Weather};

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, pageWidth,
           pageHeight -
               (metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.buttonHintsHeight)},
      static_cast<int>(items.size()), selectorIndex, [&items](int index) { return std::string(items[index]); },
      [&icons](int index) { return icons[index]; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void LockScreensActivity::onClockOpen() { activityManager.goToClockDashboard(); }

void LockScreensActivity::onWeatherOpen() { activityManager.goToWeatherDashboard(); }

void LockScreensActivity::onCustomImageOpen() { activityManager.goToCustomImageDashboard(); }

void LockScreensActivity::onMoonOpen() { activityManager.goToMoonDashboard(); }

void LockScreensActivity::onRssOpen() { activityManager.goToRssDashboard(); }

void LockScreensActivity::onTodayOpen() { activityManager.goToTodayDashboard(); }

void LockScreensActivity::onQuoteOpen() { activityManager.goToQuoteDashboard(); }

void LockScreensActivity::onBitcoinOpen() { activityManager.goToBitcoinDashboard(); }

void LockScreensActivity::onSolarOpen() { activityManager.goToSolarDashboard(); }
