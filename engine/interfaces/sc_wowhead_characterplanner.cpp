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

// WOWHEAD ID translations, starting at 1: slot, itemid, subitem (no idea what that is), permanentenchant, temporaryenchant, gm1, gem2, gem3, gem4, reforge, upgrade, transmog, hidden

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

  return util::get_first_substring_between( main_page, "var $WowheadListManager = new ListManager(", ");" );
}

std::string replace_date( std::string in )
{
  std::string date_start( "new Date(" );
  std::string date_end(")");
  std::string replace_with("\"\"");
  return util::replace_all_between( in, date_start, date_end, replace_with );
}

rapidjson::Document get_json_list_manager_section( unsigned list_id )
{
  auto list_manager_str = replace_date( get_list_manager_section( list_id ) );

  rapidjson::Document list_manager;
  list_manager.Parse< 0 >( list_manager_str.c_str() );

  if ( list_manager.HasParseError() )
    throw wowhead_charplanner::exception(std::string("List Manager Parse error: ") + list_manager.GetParseError() + "\noffset=" + util::to_string(list_manager.GetErrorOffset()) );

  return list_manager;
}

unsigned get_equipment_tab_id( unsigned list_id )
{
  auto d = get_json_list_manager_section( list_id );

  if ( !d.HasMember( "lists" ) )
    throw wowhead_charplanner::exception("no_lists");


    int equip_id = -1;
    assert(d["lists"].IsArray());
    for (rapidjson::SizeType i = 0; i < d["lists"].Size(); ++i )
    {
      if ( !d["lists"][i].HasMember( "type" ) )
        throw wowhead_charplanner::exception("list entry has no type." );

      auto k = d["lists"][i]["type"].GetInt();
      if ( k == -1 ) // Equipment Set List
      {
        equip_id = d["lists"][i]["id"].GetInt();
      }
    }
  return equip_id;
}

std::string get_tab_page( unsigned list_id, unsigned tab_id )
{
  std::string tab_page;
  std::string url = "http://www.wowhead.com/list=" + util::to_string( list_id ) + "&tab=" + util::to_string( tab_id );
  http::get( tab_page, url, cache::ANY );

  return tab_page;
}

std::string get_equipment_tab_page( unsigned list_id )
{
  auto equip_id = get_equipment_tab_id( list_id );

  return get_tab_page( list_id, equip_id );
}


rapidjson::Document get_equipment_tab_data( unsigned list_id )
{
  auto s = get_equipment_tab_page( list_id );
  auto filtered = "{" + util::get_first_substring_between( s, "{", ");" );

  rapidjson::Document out;
  out.Parse< 0 >( filtered.c_str() );

  if ( out.HasParseError() )
    throw wowhead_charplanner::exception(std::string("Equipment Data Tab Parse error: ") + out.GetParseError() );

  return out;
}
} // unnamed namespace

player_t* wowhead_charplanner::create_player( unsigned /* list_id */ )
{
  return nullptr;
}

#define UNIT_TEST_WOWHEAD
#ifdef UNIT_TEST_WOWHEAD
#include <iostream>
void sim_t::errorf( const char*, ... ) { }
uint32_t dbc::get_school_mask( school_e ) { return 0; }

void print_main_page( unsigned list_id )
{
  std::cout << get_main_page( list_id );
}

int main()
{
  std::cout << get_list_manager_section( 1564664 ) << "\n\n";
  std::cout << get_equipment_tab_id( 1564664 ) << "\n\n";
  //std::cout <<  get_equipment_tab_page( 1564664 );

  auto t = get_equipment_tab_data( 1564664 );


  rapidjson::StringBuffer b;
  rapidjson::PrettyWriter< rapidjson::StringBuffer > writer( b );

  t.Accept( writer );
  std::cout << b.GetString();
}
#endif
