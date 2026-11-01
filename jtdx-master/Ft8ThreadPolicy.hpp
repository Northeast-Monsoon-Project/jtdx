// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FT8_THREAD_POLICY_HPP
#define FT8_THREAD_POLICY_HPP

namespace Ft8ThreadPolicy {
constexpr int supportedMaximum=24;
// Qt returns -1 when the OS cannot report its available logical processors.
constexpr int maximum(int available) {
  return available<1?1:(available>supportedMaximum?supportedMaximum:available);
}
constexpr int normalize(int requested,int available) {
  return requested<=0?0:(requested>maximum(available)?maximum(available):requested);
}
}
#endif
