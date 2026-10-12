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
// Feedback with a 3% deadband accommodates sensors that also see grow lights.
// These are relative ADC levels, not calibrated lux measurements.
inline CONTROL_CONSTEXPR float nextLightScale(float scale, float measured, float target) {
  float error = target - measured;
  if ((error > 0.03f || error < -0.03f)) scale += error > 0 ? 0.05f : -0.05f;
  return scale < 0 ? 0 : scale > 1 ? 1 : scale;
}
struct Color { int red, blue; };
inline CONTROL_CONSTEXPR Color scaledColor(int red, int blue, bool automatic, float scale) {
  if (!automatic) return {red, blue};
  int largest = red > blue ? red : blue;
  if (!largest) return {0, 0};
  float factor = 255.0f * scale / largest;
  return {int(red * factor + 0.5f), int(blue * factor + 0.5f)};
}

#undef CONTROL_CONSTEXPR
