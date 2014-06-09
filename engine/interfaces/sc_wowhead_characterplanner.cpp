// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "sc_wowhead_characterplanner.hpp"
#include "../simulationcraft.hpp"

namespace {

unsigned get_tab_id( unsigned list_id )
{
  std::string main_page;
  std::string url = "http://www.wowhead.com/list=" + util::to_string( list_id );
  http::get( main_page, url, cache::CURRENT );

  std::cout << main_page;
}
}

player_t* wowhead_charplanner::create_player( unsigned list_id )
{
  return nullptr;
}

#ifdef UNIT_TEST

void print_main_page( unsigned list_id )
{
  std::string main_page;
  std::string url = "http://www.wowhead.com/list=" + util::to_string( list_id );
  http::get( main_page, url, cache::CURRENT );
}

int main()
{
  print_main_page( 1564664 );
}
#endif
