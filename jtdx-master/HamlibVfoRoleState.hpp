// Adapted from WSJT-X v3.3.0-beta5; see docs/RadioAudio_V3.7.0.txt.
#ifndef HAMLIB_VFO_ROLE_STATE_HPP_
#define HAMLIB_VFO_ROLE_STATE_HPP_

#include <tuple>
#include <utility>

#include <hamlib/rig.h>

class HamlibVfoRoleState final
{
public:
  void reset () noexcept
  {
    reversed_ = false;
  }

  void observe_active_vfo (vfo_t active_vfo, bool split_ptt_active) noexcept
  {
    if (!split_ptt_active)
      {
        // Unknown/CURR reports cannot establish a role. MAIN/SUB radios use
        // the same ownership rule as A/B radios.
        if (active_vfo == RIG_VFO_B || active_vfo == RIG_VFO_SUB) reversed_ = true;
        else if (active_vfo == RIG_VFO_A || active_vfo == RIG_VFO_MAIN) reversed_ = false;
      }
  }

  std::tuple<vfo_t, vfo_t> resolve (vfo_t rx_vfo, vfo_t tx_vfo) const noexcept
  {
    if (reversed_)
      {
        std::swap (rx_vfo, tx_vfo);
      }
    return std::make_tuple (rx_vfo, tx_vfo);
  }

  bool reversed () const noexcept
  {
    return reversed_;
  }

private:
  bool reversed_ {false};
};

#endif
