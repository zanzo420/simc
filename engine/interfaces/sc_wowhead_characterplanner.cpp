// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "../simulationcraft.hpp"
#include "util/rapidjson/document.h"
#include "util/rapidjson/stringbuffer.h"
#include "util/rapidjson/prettywriter.h"

namespace {

enum wowhead_tab_types {
  TAB_EQUIPMENT = -1,
  TAB_TALENTS = -4,
};

std::string get_main_page( unsigned list_id, cache::behavior_e caching )
{
  std::string main_page;
  std::string url = "http://www.wowhead.com/list=" + util::to_string( list_id );
  http::get( main_page, url, caching );

  return main_page;
}

std::string get_list_manager_section( unsigned list_id, cache::behavior_e caching )
{
  std::string main_page = get_main_page( list_id, caching );

  return util::get_first_substring_between( main_page, "var $WowheadListManager = new ListManager(", ");" );
}

std::string replace_date( std::string in )
{
  std::string date_start( "new Date(" );
  std::string date_end(")");
  std::string replace_with("\"\"");
  return util::replace_all_between( in, date_start, date_end, replace_with );
}

rapidjson::Document get_json_list_manager_section( unsigned list_id, cache::behavior_e caching )
{
  std::string list_manager_str = replace_date( get_list_manager_section( list_id, caching ) );

  rapidjson::Document list_manager;
  list_manager.Parse< 0 >( list_manager_str.c_str() );

  if ( list_manager.HasParseError() )
    throw wowhead_charplanner::exception(std::string("List Manager Parse error: ") + list_manager.GetParseError() + "\noffset=" + util::to_string(list_manager.GetErrorOffset()) );

  return list_manager;
}

/* Retrieves the first tab id with the given tab type in the given list_manager
 */
unsigned get_tab_id( const rapidjson::Document& list_manager, wowhead_tab_types tab_type )
{
  if ( !list_manager.HasMember( "lists" ) )
    throw wowhead_charplanner::exception("no_lists");


    int equip_id = -1;
    assert(list_manager["lists"].IsArray());
    for (rapidjson::SizeType i = 0; i < list_manager["lists"].Size(); ++i )
    {
      if ( !list_manager["lists"][i].HasMember( "type" ) )
        throw wowhead_charplanner::exception("list entry has no type." );

      int k = list_manager["lists"][i]["type"].GetInt();
      if ( k == static_cast<int>(tab_type) )
      {
        equip_id = list_manager["lists"][i]["id"].GetInt();
        break;
      }
    }
  return equip_id;
}

std::string get_tab_page( unsigned list_id, unsigned tab_id, cache::behavior_e caching )
{
  std::string tab_page;
  std::string url = "http://www.wowhead.com/list=" + util::to_string( list_id ) + "&tab=" + util::to_string( tab_id );
  http::get( tab_page, url, caching );

  return tab_page;
}

struct wowhead_tab_t {
  unsigned tab_id;
  rapidjson::Document content;
};
std::pair<unsigned,std::string> get_tab_page( unsigned list_id, const rapidjson::Document& list_manager, wowhead_tab_types tab_type, cache::behavior_e caching )
{
  unsigned tab_id = get_tab_id( list_manager, tab_type );
  return std::make_pair( tab_id, get_tab_page( list_id, tab_id , caching ) );
}


wowhead_tab_t get_tab_data( unsigned list_id, const rapidjson::Document& list_manager, wowhead_tab_types tab_type, cache::behavior_e caching )
{
  std::pair<unsigned,std::string> tab_page = get_tab_page( list_id, list_manager, tab_type, caching );
  std::string filtered = "{" + util::get_first_substring_between( tab_page.second, "{", ");" );


  wowhead_tab_t out;
  out.tab_id = tab_page.first;
  out.content.Parse< 0 >( filtered.c_str() );

  return out;
}


template <typename Out>
void pretty_print_json( Out& out, const rapidjson::Document& doc )
{
  rapidjson::StringBuffer b;
  rapidjson::PrettyWriter< rapidjson::StringBuffer > writer( b );

  doc.Accept( writer );
  out << b.GetString();
}

bool parse_equipment_data( sim_t* sim, player_t* p, const wowhead_tab_t& equipment_tab )
{
  // TODO: parse equip stuff into the player

  // WOWHEAD item array entries:
  // slot, itemid, subitem (no idea what that is), permanentenchant, temporaryenchant, gm1, gem2, gem3, gem4, reforge, upgrade, transmog, hidden

  std::string tab_id = util::to_string( equipment_tab.tab_id );
  if (   !equipment_tab.content.HasMember( tab_id.c_str() )
      || !equipment_tab.content[ tab_id.c_str() ].IsArray() )
  {
    sim -> errorf( "WOWHEAD API: Unable to extract player equip from '%s'.\n", p -> name() );
    return false;
  }
  const rapidjson::Value& equip_list = equipment_tab.content[ tab_id.c_str() ];
  for (rapidjson::SizeType i = 0; i < equip_list.Size(); ++i )
  {
    const rapidjson::Value& wowh_item = equip_list[ i ];
    if (   !wowh_item.IsArray()
        || wowh_item.Size() != 13 )
    {
      sim -> errorf( "WOWHEAD API: Unable to parse item with index '%u' of player '%s'.\n", i, p -> name() );
      continue;
    }

    int wowh_slot_id = wowh_item[ 0u ].GetInt();
    slot_e our_slot = static_cast<slot_e>( wowh_slot_id - 1 );
    if ( our_slot == SLOT_INVALID || our_slot >= SLOT_MAX )
      continue;


    item_t& item = p -> items[ our_slot ];

    item.parsed.data.id = wowh_item[ 1 ].GetUint();
    item.parsed.enchant_id = wowh_item[ 3 ].GetUint();
    // item.parsed.addon_id = wowh_item[ 4 ].GetUint();
    item.parsed.gem_id[ 0 ] = wowh_item[ 5 ].GetUint();
    item.parsed.gem_id[ 1 ] = wowh_item[ 6 ].GetUint();
    item.parsed.gem_id[ 2 ] = wowh_item[ 7 ].GetUint();
    item.parsed.gem_id[ 3 ] = wowh_item[ 8 ].GetUint();
    item.parsed.upgrade_level = wowh_item[ 10 ].GetUint();

  }

  return true;
}

bool parse_talent_and_glyph_data( sim_t* sim, player_t* p, const wowhead_tab_t& talent_tab )
{
  //pretty_print_json( std::cout, talent_tab.content );
  // TODO: parse talents and glyphs into the player
  std::string tab_id = util::to_string( talent_tab.tab_id );
  if (   !talent_tab.content.HasMember( tab_id.c_str() )
      || !talent_tab.content[ tab_id.c_str() ].IsArray() )
  {
    sim -> errorf( "WOWHEAD API: Unable to extract player talent/glyph data from '%s'.\n", p -> name() );
    return false;
  }
  rapidjson::SizeType active_spec = 0;
  if (   !talent_tab.content[ tab_id.c_str() ].Size() > active_spec )
  {
    sim -> errorf( "WOWHEAD API: Unable to extract player talent/glyph data from '%s'.\n", p -> name() );
    return false;
  }
  const rapidjson::Value& talent_spec_list= talent_tab.content[ tab_id.c_str() ][ active_spec ];

  std::string talent_encoding = util::to_string( talent_spec_list[ 2 ].GetUint() );
  //std::cout << "\ntalent string: " << talent_encoding << "\n";

  // Do nothing for now, since the talent string are incomplete anyway.
  /*if ( ! p -> parse_talents_numbers( talent_encoding ) )
  {
    p -> sim -> errorf( "WOWHEAD API: Can't parse talent encoding '%s' for player %s.\n", talent_encoding.c_str(), p -> name() );
  }

  p -> create_talents_armory();
  */

  return true;

}

} // unnamed namespace

player_t* wowhead_charplanner::download_player( sim_t* sim,
                                              unsigned list_id,
                                              cache::behavior_e caching )
{
  rapidjson::Document list_manager = get_json_list_manager_section( list_id, caching );

  if ( list_manager.HasParseError() )
  {
    sim -> errorf( "WOWHEAD API: Unable to download player '%u'\n", list_id );
    return nullptr;
  }

  if ( sim -> debug )
  {
    pretty_print_json( sim -> out_debug.raw(), list_manager );
  }

  // Character Name
  if ( ! list_manager.HasMember( "name" ) )
  {
    sim -> errorf( "WOWHEAD API: Unable to extract player name from '%u'.\n", list_id );
    return nullptr;
  }
  // Character Level
  if ( ! list_manager.HasMember( "level" ) )
  {
    sim -> errorf( "WOWHEAD API: Unable to extract player level from '%u'.\n", list_id );
    return nullptr;
  }
  // Character Class
  if ( ! list_manager.HasMember( "classs" ) )
  {
    sim -> errorf( "WOWHEAD API: Unable to extract player class from '%u'.\n", list_id );
    return nullptr;
  }
  // Character Specialization
  if ( ! list_manager.HasMember( "talentspec" ) )
  {
    sim -> errorf( "WOWHEAD API: Unable to extract player class from '%u'.\n", list_id );
    return nullptr;
  }
  // Character Race
  if ( ! list_manager.HasMember( "race" ) )
  {
    sim -> errorf( "WOWHEAD API: Unable to extract player race from '%u'.\n", list_id );
    return nullptr;
  }

  std::string character_name = list_manager[ "name" ].GetString();

  unsigned character_level = list_manager[ "level" ].GetUint();

  player_e character_class = util::translate_class_id( list_manager[ "classs" ].GetInt() );
  if ( character_class == PLAYER_NONE )
  {
    sim -> errorf( "WOWHEAD API: Unable to parse player class from '%u'.\n", list_id );
    return nullptr;
  }

  race_e character_race = util::translate_race_id( list_manager[ "race" ].GetInt() );
  if ( character_race == RACE_NONE )
  {
    sim -> errorf( "WOWHEAD API: Unable to parse player race from '%u'.\n", list_id );
    return nullptr;
  }

  specialization_e character_spec = dbc::spec_by_idx( character_class, list_manager[ "talentspec" ].GetUint() );
  if ( character_spec == SPEC_NONE )
  {
    sim -> errorf( "WOWHEAD API: Unable to parse player spec from '%u'.\n", list_id );
    return nullptr;
  }

  // Character Professions
  std::vector<std::pair<profession_e,int> > character_professions;
  if ( list_manager.HasMember( "skills" ) && list_manager[ "skills" ].IsArray() )
  {
    for (rapidjson::SizeType i = 0; i < list_manager[ "skills" ].Size(); ++i )
    {
      profession_e prof = util::translate_profession_id( list_manager[ "skills" ][ i ][ rapidjson::SizeType(0) ].GetInt() );
      int skill = list_manager[ "skills" ][ i ][ rapidjson::SizeType(1) ].GetInt();
      if ( prof != PROFESSION_NONE )
        character_professions.push_back( std::make_pair( prof, skill ) );
    }
  }
  else
  {
    sim -> errorf( "WOWHEAD API: Unable to parse player professions from '%u'.\n", list_id );
    return nullptr;
  }

  // Debug
  if ( sim -> debug )
  {
    sim -> out_debug.raw() << "name: " << character_name << "\n";
    sim -> out_debug.raw() << "level: " << character_level << "\n";
    sim -> out_debug.raw() << "class: " << util::player_type_string( character_class ) << "\n";
    sim -> out_debug.raw() << "race: " << util::race_type_string( character_race ) << "\n";
    sim -> out_debug.raw() << "spec: " << util::specialization_string( character_spec ) << "\n";
    for( size_t i = 0; i < character_professions.size(); ++i ) {
      sim -> out_debug.raw() << "profession: " << util::profession_type_string( character_professions[ i ].first ) << " skill: " << character_professions[ i ].second << "\n";
    }
  }

  const module_t* module = module_t::get( character_class );
  if ( ! module || ! module -> valid() )
  {
    sim -> errorf( "\nModule for class %s is currently not available.\n", util::player_type_string( character_class ) );
    return 0;
  }

  player_t* p = sim -> active_player = module -> create_player( sim, character_name, character_race );
  if ( ! p )
  {
    sim -> errorf( "WOWHEAD API: Unable to build player with class '%s' and name '%s' from '%u'.\n",
                   util::player_type_string( character_class ), character_name.c_str(), list_id );
    return nullptr;
  }

  p -> level = character_level;

  p -> _spec = character_spec;

  for( size_t i = 0; i < character_professions.size(); ++i ) {
    if ( i > 0 )
      p -> professions_str += "/";

    p -> professions_str += util::profession_type_string( character_professions[ i ].first );
    p -> professions_str += "=";
    p -> professions_str += util::to_string( character_professions[ i ].second );
  }

  wowhead_tab_t equipment_tab = get_tab_data( list_id, list_manager, TAB_EQUIPMENT, caching );
  parse_equipment_data( sim, p, equipment_tab );

  wowhead_tab_t talents_tab = get_tab_data( list_id, list_manager, TAB_TALENTS, caching );
  parse_talent_and_glyph_data( sim, p, talents_tab );
  return p;
}

//#define UNIT_TEST_WOWHEAD
#ifdef UNIT_TEST_WOWHEAD
#include <iostream>

int main()
{
  //std::cout << get_list_manager_section( 1564664 ) << "\n\n";
  //std::cout << get_equipment_tab_id( 1564664 ) << "\n\n";
  //std::cout <<  get_equipment_tab_page( 1564664 );

  cache::behavior_e cache = cache::ANY;
  rapidjson::Document list_manager = get_json_list_manager_section(  1564664, cache );
  pretty_print_json( std::cout, list_manager );
  std::cout << "\n\n\n";
  pretty_print_json( std::cout, get_tab_data(  1564664, list_manager, TAB_EQUIPMENT, cache ).content );
  std::cout << "\n\n\n";
  pretty_print_json( std::cout, get_tab_data(  1564664, list_manager, TAB_TALENTS, cache ).content );
  //wowhead_charplanner::create_player( 1564664 );
}
#endif
