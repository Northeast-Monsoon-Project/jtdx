#ifndef RIG_TELEMETRY_HPP
#define RIG_TELEMETRY_HPP
#include <QString>
#include <cmath>
#include <limits>
// Zero means unavailable, never an invented measurement.
namespace RigTelemetry {
inline unsigned scaled(double value, unsigned scale, double minimum, double maximum) {
  if (!std::isfinite(value) || value < minimum || value > maximum ||
      value * scale > std::numeric_limits<unsigned>::max()) return 0;
  return static_cast<unsigned>(std::llround(value * scale));
}
inline unsigned scaled(QString const& text, unsigned scale, double minimum, double maximum) {
  bool ok = false;
  double value = text.toDouble(&ok);
  return ok ? scaled(value, scale, minimum, maximum) : 0;
}
}
#endif
