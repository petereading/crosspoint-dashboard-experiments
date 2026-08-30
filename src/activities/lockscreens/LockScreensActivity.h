#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// "Lock Screens" folder: Worker clock/weather cards and an arbitrary custom BMP.
class LockScreensActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  static constexpr int ITEM_COUNT = 3;

  void onClockOpen();
  void onWeatherOpen();
  void onCustomImageOpen();

 public:
  explicit LockScreensActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LockScreens", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
