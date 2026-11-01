// Adapted from WSJT-X v3.3.0-beta5 HamlibMode.cpp (GPLv3+).
// Preserve operator DATA profiles and query real mode after band recalls.
#ifndef JTDXF_HAMLIB_MODE_POLICY_HPP
#define JTDXF_HAMLIB_MODE_POLICY_HPP

#include <hamlib/rig.h>

namespace HamlibModePolicy
{
  inline bool satisfies (rmode_t requested, rmode_t current)
  {
    if (requested == current) return true;
    if (requested == RIG_MODE_PKTUSB)
      return current == RIG_MODE_USBD1 || current == RIG_MODE_USBD2 || current == RIG_MODE_USBD3;
    if (requested == RIG_MODE_PKTLSB)
      return current == RIG_MODE_LSBD1 || current == RIG_MODE_LSBD2 || current == RIG_MODE_LSBD3;
    return false;
  }

  inline rmode_t preserveProfile (rmode_t requested, rmode_t current)
  {
    return satisfies (requested, current) ? current : requested;
  }

  inline int uncachedMode (RIG * rig, vfo_t vfo, rmode_t * mode, pbwidth_t * width)
  {
    // Both supported runtime SDKs use the Hamlib 4.7 cache API. A band recall
    // may change the radio's mode while Hamlib still caches the previous mode.
    // HAMLIB_CACHE_ALWAYS is -1, so a negative timeout is not itself an error.
    auto const timeout = rig_get_cache_timeout_ms (rig, HAMLIB_CACHE_ALL);
    auto const rc = rig_set_cache_timeout_ms (rig, HAMLIB_CACHE_ALL, 0);
    if (rc != RIG_OK) return rc;
    auto const result = rig_get_mode (rig, vfo, mode, width);
    auto const restored = rig_set_cache_timeout_ms (rig, HAMLIB_CACHE_ALL, timeout);
    return result == RIG_OK ? restored : result;
  }
}

#endif
