#include "Control.h"
constexpr bool timerBothDirections() {
  PumpControl p;
  p.timer(1, 100);
  if (p.tick(60099) || p.on || p.remaining(100) != 60000) return false;
  if (!p.tick(60100) || !p.on || p.mode != PumpMode::Manual) return false;
  if (p.tick(120100)) return false;
  p.timer(1, 120100);
  return p.tick(180100) && !p.on;
}
constexpr bool cycleAndOverride() {
  PumpControl p;
  p.cycle(2, 1, 0);
  if (p.tick(59999) || !p.tick(60000) || !p.on) return false;
  if (p.tick(179999) || !p.tick(180000) || p.on) return false;
  if (!p.tick(240000) || !p.on) return false;
  p.manual(false);
  return !p.tick(500000) && !p.on && p.remaining(500000) == 0;
}
constexpr bool rollover() {
  PumpControl p;
  p.timer(1, UINT32_MAX - 29999);
  return !p.tick(29999) && p.remaining(29999) == 1 && p.tick(30000) && p.on;
}
constexpr bool replaceTimer() {
  PumpControl p;
  p.timer(1, 0); p.timer(2, 1000);
  return !p.tick(60000) && p.tick(121000);
}
static_assert(timerBothDirections(), "Timer must switch once in either direction");
static_assert(cycleAndOverride(), "Cycle alternates and manual control cancels it");
static_assert(rollover(), "Timer must survive millis rollover");
static_assert(replaceTimer(), "Replacing a timer restarts its deadline");
static_assert(nextLightScale(0, 0, .5f) > 0, "Low light increases intensity");
static_assert(nextLightScale(.5f, 1, .5f) < .5f, "High light decreases intensity");
static_assert(nextLightScale(.5f, .51f, .5f) == .5f, "Deadband suppresses hunting");
static_assert(nextLightScale(1, 0, 1) == 1, "Upper bound");
static_assert(nextLightScale(0, 1, .5f) == 0, "Lower bound");
static_assert(scaledColor(128,128,true,1).red == 255 && scaledColor(128,128,true,1).blue == 255, "Equal mix");
static_assert(scaledColor(100,200,true,1).red == 128 && scaledColor(100,200,true,1).blue == 255, "Unequal mix");
static_assert(scaledColor(0,0,true,1).red == 0, "Zero mix cannot divide by zero");
static_assert(scaledColor(100,200,false,0).blue == 200, "Manual channels unchanged");
static_assert(scaledColor(100,200,true,0).blue == 0, "Zero intensity");
