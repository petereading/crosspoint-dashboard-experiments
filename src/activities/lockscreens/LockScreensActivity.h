#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// "Lock Screens" folder: Worker dashboard cards and an arbitrary custom BMP.
class LockScreensActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  static constexpr int ITEM_COUNT = 9;

  void onClockOpen();
  void onWeatherOpen();
  void onCustomImageOpen();
  void onMoonOpen();
  void onRssOpen();
  void onTodayOpen();
  void onQuoteOpen();
  void onBitcoinOpen();
  void onSolarOpen();

 public:
  explicit LockScreensActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LockScreens", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
