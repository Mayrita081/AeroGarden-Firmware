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
static_assert(scaledColor(128,128,true,1).red == 255 && scaledColor(128,128,true,1).blue == 255, "Equal mix");
static_assert(scaledColor(100,200,true,1).red == 128 && scaledColor(100,200,true,1).blue == 255, "Unequal mix");
static_assert(scaledColor(0,0,true,1).red == 0, "Zero mix cannot divide by zero");
static_assert(scaledColor(100,200,false,0).blue == 200, "Manual channels unchanged");
static_assert(scaledColor(100,200,true,0).blue == 0, "Zero intensity");

constexpr DailyLightSchedule day = {true,360,1080,-180};
constexpr DailyLightSchedule night = {true,1080,360,-180};
constexpr DailyLightSchedule disabled = {false,360,1080,-180};
constexpr DailyLightSchedule equal = {true,360,360,-180};
static_assert(!day.allows(true,359) && day.allows(true,360), "Day start inclusive");
static_assert(day.allows(true,1079) && !day.allows(true,1080), "Day end exclusive");
static_assert(!day.allows(false,720), "No clock means scheduled lights off");
static_assert(night.allows(true,1080) && night.allows(true,0) && night.allows(true,359), "Overnight includes midnight");
static_assert(!night.allows(true,360) && !night.allows(true,1079), "Overnight excludes daytime");
static_assert(disabled.allows(false,0), "Disabled schedule preserves manual lighting without clock");
static_assert(!equal.valid() && !equal.allows(true,360), "Equal times invalid");
static_assert(localMinute(0,-180)==1260, "Negative offset wraps previous day");
static_assert(localMinute(23*3600,120)==60, "Positive offset wraps next day");
static_assert(day.allows(true,localMinute(9*3600,-180)), "09:00 UTC means 06:00 Argentina");

static_assert(ambientLightScale(0,.5f)==1, "Darkness gives full output");
static_assert(ambientLightScale(.25f,.5f)==.5f, "Half target gives half output without accumulating");
static_assert(ambientLightScale(.5f,.5f)==0, "At target LEDs turn off");
static_assert(ambientLightScale(.8f,.5f)==0, "Strong sun clamps to off");
static_assert(ambientLightScale(0,0)==0, "Invalid target cannot divide by zero");
static_assert(scaledColor(100,200,true,ambientLightScale(.25f,.5f)).red==64 &&
              scaledColor(100,200,true,ambientLightScale(.25f,.5f)).blue==128, "Ambient compensation preserves color mix");

static_assert(sensorForPixel(0)==0 && sensorForPixel(31)==0 && sensorForPixel(32)==1, "First section boundary");
static_assert(sensorForPixel(63)==1 && sensorForPixel(64)==2 && sensorForPixel(95)==2 && sensorForPixel(96)==3 && sensorForPixel(127)==3, "First floor sections");
static_assert(sensorForPixel(128)==0 && sensorForPixel(159)==0 && sensorForPixel(160)==1 && sensorForPixel(255)==3, "Second floor repeats mapping");
static_assert(sensorForPixel(256)==0 && sensorForPixel(383)==3, "Third floor repeats mapping");
static_assert(sectionColor(128,128,true,0,50,true).red==255 && sectionColor(128,128,true,4095,50,true).red==0, "Shaded and sunny sections are independent");
static_assert(sectionColor(100,200,false,4095,50,true).red==100, "Manual mode ignores sunlight");
static_assert(sectionColor(100,200,true,0,50,false).blue==0, "Schedule overrides automatic mode");
static_assert(sectionColor(100,200,false,0,50,false).blue==0, "Schedule overrides manual mode");
