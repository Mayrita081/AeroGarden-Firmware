#pragma once
#include <stdint.h>
#if __cplusplus >= 201402L
#define CONTROL_CONSTEXPR constexpr
#else
#define CONTROL_CONSTEXPR
#endif
enum class PumpMode { Manual, Timer, Cycle };
struct PumpControl {
  bool on = false;
  PumpMode mode = PumpMode::Manual;
  uint32_t started = 0, duration = 0, onMinutes = 5, offMinutes = 5;
  CONTROL_CONSTEXPR void manual(bool state) { on = state; mode = PumpMode::Manual; }
  CONTROL_CONSTEXPR void timer(uint32_t minutes, uint32_t now) { mode = PumpMode::Timer; duration = minutes * 60000U; started = now; }
  CONTROL_CONSTEXPR void cycle(uint32_t onMins, uint32_t offMins, uint32_t now) {
    onMinutes = onMins; offMinutes = offMins; mode = PumpMode::Cycle; started = now;
  }
  CONTROL_CONSTEXPR uint32_t interval() const { return mode == PumpMode::Cycle ? (on ? onMinutes : offMinutes) * 60000U : duration; }
  CONTROL_CONSTEXPR uint32_t remaining(uint32_t now) const {
    uint32_t elapsed = now - started;
    return mode == PumpMode::Manual || elapsed >= interval() ? 0 : interval() - elapsed;
  }
  CONTROL_CONSTEXPR bool tick(uint32_t now) {
    if (mode == PumpMode::Manual || uint32_t(now - started) < interval()) return false;
    on = !on; started = now;
    if (mode == PumpMode::Timer) mode = PumpMode::Manual;
    return true;
  }
};
// Feed-forward compensation for ambient-only sensors (normalized 0..1).
// Each row uses only its own measurement; output cannot affect this input.
inline CONTROL_CONSTEXPR float ambientLightScale(float measured, float target) {
  if (target <= 0) return 0;
  if (measured <= 0) return 1;
  if (measured >= target) return 0;
  return 1.0f - measured / target;
}
struct Color { int red, blue; };
inline CONTROL_CONSTEXPR Color scaledColor(int red, int blue, bool automatic, float scale) {
  if (!automatic) return {red, blue};
  int largest = red > blue ? red : blue;
  if (!largest) return {0, 0};
  float factor = 255.0f * scale / largest;
  return {int(red * factor + 0.5f), int(blue * factor + 0.5f)};
}

constexpr int LIGHT_SENSOR_COUNT = 4;
constexpr int LEDS_PER_SECTION = 32;
constexpr int LEDS_PER_FLOOR = LIGHT_SENSOR_COUNT * LEDS_PER_SECTION;
inline CONTROL_CONSTEXPR int sensorForPixel(int pixel) {
  return (pixel % LEDS_PER_FLOOR) / LEDS_PER_SECTION;
}
inline CONTROL_CONSTEXPR Color sectionColor(int red, int blue, bool automatic,
                                          int ambientADC, int targetPercent, bool allowed) {
  return allowed ? scaledColor(red, blue, automatic,
    ambientLightScale(ambientADC / 4095.0f, targetPercent / 100.0f)) : Color{0, 0};
}
// Daily local-time window: inclusive start, exclusive end; supports midnight crossing.
struct DailyLightSchedule {
  bool enabled = false;
  int startMinute = 360, endMinute = 1080, utcOffsetMinutes = -180;
  CONTROL_CONSTEXPR bool valid() const {
    return startMinute >= 0 && startMinute < 1440 && endMinute >= 0 && endMinute < 1440 &&
      startMinute != endMinute && utcOffsetMinutes >= -720 && utcOffsetMinutes <= 840;
  }
  CONTROL_CONSTEXPR bool allows(bool clockValid, int minute) const {
    if (!enabled) return true;
    if (!clockValid || !valid() || minute < 0 || minute >= 1440) return false;
    return startMinute < endMinute ? minute >= startMinute && minute < endMinute
      : minute >= startMinute || minute < endMinute;
  }
};
inline CONTROL_CONSTEXPR int localMinute(int64_t epoch, int offsetMinutes) {
  int64_t minute = (epoch / 60 + offsetMinutes) % 1440;
  return int(minute < 0 ? minute + 1440 : minute);
}
#undef CONTROL_CONSTEXPR
