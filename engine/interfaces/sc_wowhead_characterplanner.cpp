// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "sc_wowhead_characterplanner.hpp"
#include "../simulationcraft.hpp"
#include <stdexcept>

namespace {


std::string get_main_page( unsigned list_id )
{
  std::string main_page;
  std::string url = "http://www.wowhead.com/list=" + util::to_string( list_id );
  http::get( main_page, url, cache::ANY );

  return main_page;
}

std::string get_list_manager_section( unsigned list_id )
{
  auto main_page = get_main_page( list_id );

  auto start = main_page.find( "var $WowheadListManager = new ListManager(" );
  if ( start == std::string::npos )
  {
    throw std::runtime_error("Could not find list manager section start" );
  }
  start += sizeof("var $WowheadListManager = new ListManager(") - 1;
  auto end = main_page.find( "\n", start);

  if ( end == std::string::npos )
  {
    throw std::runtime_error("Could not find list manager section end " );
  }

  std::cout << "start: " << start << "\n";
  std::cout << "end: " << end << "\n";

  return main_page.substr( start, ( end - start - sizeof(");" ) ) );

}

} // unnamed namespace

player_t* wowhead_charplanner::create_player( unsigned /* list_id */ )
{
  return nullptr;
}

#ifdef UNIT_TEST_WOWHEAD
#include <iostream>
void sim_t::errorf( const char*, ... ) { }
uint32_t dbc::get_school_mask( school_e ) { return 0; }

void print_main_page( unsigned list_id )
{
  std::cout << get_main_page( list_id );
}

void print_list_manager_section( unsigned list_id )
{

  std::cout << get_list_manager_section( list_id );

}
int main()
{
  print_list_manager_section( 1564664 );
}
#endif
