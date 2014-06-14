// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#pragma once

#include "../config.hpp"
#include <string>
#include <vector>
#include <stdexcept>
#include "../util/cache.hpp"
struct player_t;
struct sim_t;

namespace wowhead_charplanner {

typedef std::runtime_error exception;

player_t* download_player( sim_t* sim, unsigned list_id, cache::behavior_e caching );
}
