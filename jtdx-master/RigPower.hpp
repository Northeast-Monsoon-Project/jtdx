#ifndef RIG_POWER_HPP
#define RIG_POWER_HPP
#include "RigTelemetry.hpp"

namespace RigPower {
enum class Source { Unavailable, WattsMeter, RelativeMeter, Setting };

inline Source source(bool wattsMeter, bool relativeMeter, bool setting) {
  return wattsMeter ? Source::WattsMeter
       : relativeMeter ? Source::RelativeMeter
       : setting ? Source::Setting : Source::Unavailable;
}

// A failed meter read is unavailable, not a reason to substitute the requested
// power and present it as a measurement. The setting is used only when the
// backend advertises neither kind of output meter.
template<typename GetLevel, typename Convert>
unsigned read(Source selected, GetLevel getLevel, Convert convert) {
  if (selected == Source::Unavailable) return 0;
  double value = 0;
  if (!getLevel(selected, value) || !std::isfinite(value) || value < 0) return 0;
  if (selected == Source::WattsMeter)
    return RigTelemetry::scaled(value, 1000, 0, 1000000);
  if (value > 1) return 0;
  unsigned milliwatts = 0;
  // RFPOWER_METER is a percentage of maximum output, whereas RFPOWER can
  // describe a non-linear control. Ask Hamlib for full scale for the meter.
  if (selected == Source::RelativeMeter) {
    if (!convert(1.f, milliwatts)) return 0;
    return RigTelemetry::scaled(value, milliwatts, 0, 1);
  }
  return convert(static_cast<float>(value), milliwatts) ? milliwatts : 0;
}
}
#endif
