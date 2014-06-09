// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#pragma once

#include <string>
#include <vector>
#include <stdexcept>
struct player_t;

namespace wowhead_charplanner {

typedef std::runtime_error exception;

player_t* create_player( unsigned list_id );
}
