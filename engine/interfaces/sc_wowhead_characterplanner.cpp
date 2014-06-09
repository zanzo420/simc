// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "sc_wowhead_characterplanner.hpp"
#include "../simulationcraft.hpp"
#include "util/rapidjson/document.h"
#include "util/rapidjson/stringbuffer.h"
#include "util/rapidjson/prettywriter.h"

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
    throw wowhead_charplanner::exception("Could not find list manager section start" );
  }
  start += sizeof("var $WowheadListManager = new ListManager(") - 1;
  auto end = main_page.find( "\n", start);

  if ( end == std::string::npos )
  {
    throw wowhead_charplanner::exception("Could not find list manager section end ");
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

void json_print_list_manager_section( unsigned list_id )
{
  auto list_manager_str = get_list_manager_section( list_id );

  std::cout << "\n\n" << list_manager_str << "\n\n";

  rapidjson::Document list_manager;
  list_manager.Parse< 0 >( list_manager_str.c_str() );

  if ( list_manager.HasParseError() )
    throw wowhead_charplanner::exception(std::string("List Manager Parse error: ") + list_manager.GetParseError() + "\noffset=" + util::to_string(list_manager.GetErrorOffset()) );

  if ( !list_manager.HasMember( "lists" ) )
    throw wowhead_charplanner::exception("no_lists");

  rapidjson::StringBuffer b;
  rapidjson::PrettyWriter< rapidjson::StringBuffer > writer( b );

  list_manager.Accept( writer );
  std::cout << b.GetString();

}
int main()
{
  json_print_list_manager_section( 1564664 );
}
#endif
