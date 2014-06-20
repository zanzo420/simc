// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "../simulationcraft.hpp"
#include "util/rapidjson/document.h"
#include "util/rapidjson/stringbuffer.h"
#include "util/rapidjson/prettywriter.h"

namespace {

struct wowhead_exception : public std::runtime_error
{
  explicit wowhead_exception( const std::string& what_arg ) :
      std::runtime_error( what_arg )
  {

  }
};

enum wowhead_tab_types {
  TAB_EQUIPMENT = -1,
  TAB_TALENTS = -4,
};

std::string tab_type_string( wowhead_tab_types tab )
{
  switch ( tab )
  {
  case TAB_EQUIPMENT: return "TAB_EQUIPMENT";
  case TAB_TALENTS: return "TAB_TALENTS";
  default: break;
  }
  return "UNKNOWN_TAB";
}

std::string get_main_page( unsigned list_id, cache::behavior_e caching )
{
  std::string main_page;
  std::string url = "http://www.wowhead.com/list=" + util::to_string( list_id );
  if ( ! http::get( main_page, url, caching ) )
  {
    throw wowhead_exception( "Could not down download main http page." );
  }
  return main_page;
}

std::string get_list_manager_section( std::string main_page )
{
  try
  {
    return util::get_first_substring_between( main_page, "var $WowheadListManager = new ListManager(", ");" );
  }
  catch ( const std::runtime_error& )
  {
    throw wowhead_exception( "Could not extract list manager section." );
  }
}

std::string replace_date( std::string in )
{
  std::string date_start( "new Date(" );
  std::string date_end(")");
  std::string replace_with("\"\"");
  return util::replace_all_between( in, date_start, date_end, replace_with );
}

rapidjson::Document extract_list_manager_section( std::string main_page )
{
  std::string list_manager_str = replace_date( get_list_manager_section( main_page ) );
  rapidjson::Document out;
  out.Parse< 0 >( list_manager_str.c_str() );

  if ( out.HasParseError() )
  {
    std::string error = "Could not build list manager JSON document: ";
    error += out.GetParseError();
    throw wowhead_exception( error );
  }

  return out;
}

/* Retrieves the first tab id with the given tab type in the given list_manager
 * Pre-Condition: Array Field lists is available
 */
unsigned get_tab_id( const rapidjson::Document& list_manager, wowhead_tab_types tab_type )
{
    for (rapidjson::SizeType i = 0; i < list_manager["lists"].Size(); ++i )
    {
      if ( !list_manager["lists"][i].HasMember( "type" ) )
      {
        std::string error = "List entry has no type. Tab: ";
        error += tab_type_string( tab_type );
        throw wowhead_exception( error );
      }

      int k = list_manager["lists"][i]["type"].GetInt();
      if ( k == static_cast<int>(tab_type) )
      {
        return list_manager["lists"][i]["id"].GetInt();
      }
    }

    throw wowhead_exception( "Could not extract tab page id." );
}

std::string get_tab_page( unsigned list_id, unsigned tab_id, cache::behavior_e caching )
{
  std::string tab_page;
  std::string url = "http://www.wowhead.com/list=" + util::to_string( list_id ) + "&tab=" + util::to_string( tab_id );
  if ( ! http::get( tab_page, url, caching ) )
  {
    throw wowhead_exception( "Could not down download main http page." );
  }
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
  wowhead_tab_t out;
  std::string filtered;

  try
  {
    filtered = "{" + util::get_first_substring_between( tab_page.second, "{", ");" );
  }
  catch ( const std::runtime_error& )
  {
    throw wowhead_exception( "Could not extract tab data section." );
  }

  out.tab_id = tab_page.first;
  out.content.Parse< 0 >( filtered.c_str() );
  if ( out.content.HasParseError() )
  {
    std::string error = "Could not build tab page JSON document: ";
    error += out.content.GetParseError();
    throw wowhead_exception( error );
  }
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

bool parse_equipment_data( sim_t*, player_t* p, const wowhead_tab_t& equipment_tab )
{
  // TODO: parse equip stuff into the player

  // WOWHEAD item array entries:
  // slot, itemid, subitem (no idea what that is), permanentenchant, temporaryenchant, gm1, gem2, gem3, gem4, reforge, upgrade, transmog, hidden

  std::string tab_id = util::to_string( equipment_tab.tab_id );
  if (   !equipment_tab.content.HasMember( tab_id.c_str() )
      || !equipment_tab.content[ tab_id.c_str() ].IsArray() )
  {
    throw wowhead_exception( "Equipment Tab: Field ID Inconsistency." );
  }
  const rapidjson::Value& equip_list = equipment_tab.content[ tab_id.c_str() ];
  for (rapidjson::SizeType i = 0; i < equip_list.Size(); ++i )
  {
    const rapidjson::Value& wowh_item = equip_list[ i ];
    if (   !wowh_item.IsArray()
        || wowh_item.Size() != 13 )
    {
      throw wowhead_exception( "Equipment Tab: Item Data Array format." );
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
    throw wowhead_exception( "Talent Tab: Field ID Inconstency." );
  }
  rapidjson::SizeType active_spec = 0;
  if ( ! ( talent_tab.content[ tab_id.c_str() ].Size() > active_spec ) )
  {
    throw wowhead_exception( "Talent Tab: No Data for given spec." );
  }
  const rapidjson::Value& talent_spec_list= talent_tab.content[ tab_id.c_str() ][ active_spec ];
  if (   ! talent_spec_list.IsArray() || talent_spec_list.Size() < 3 )
  {
    throw wowhead_exception( "Talent Tab: Invalid Talent Data." );
  }
  std::string talent_encoding = util::to_string( talent_spec_list[ 2 ].GetUint() );
  //std::cout << "\ntalent string: " << talent_encoding << "\n";

  // Do nothing for now, since the talent string are incomplete anyway.
  /*if ( ! p -> parse_talents_numbers( talent_encoding ) )
  {
    p -> sim -> errorf( "WOWHEAD API: Can't parse talent encoding '%s' for player %s.\n", talent_encoding.c_str(), p -> name() );
  }

  p -> create_talents_armory();
  */

  for (rapidjson::SizeType i = 3; i < talent_spec_list.Size(); ++i )
  {
    unsigned glyph_property_id = talent_spec_list[ i ].GetUint();
    if ( glyph_property_id == 0 )
      continue;

    unsigned glyph_spell_id = p -> dbc.glyph_spell_id( glyph_property_id );
    const spell_data_t* glyph = p -> dbc.spell( glyph_spell_id );
    std::string glyph_name = glyph -> name_cstr();
    if ( sim -> debug )
    {
      sim -> out_debug.raw() << "WOWHEAD API: glyph_property-id: " << glyph_property_id << " found_glyph_spell_id=" << glyph_spell_id << " found_glyph_name=" << glyph_name << "\n";
    }

    util::glyph_name( glyph_name );
    if ( ! p -> glyphs_str.empty() )
      p -> glyphs_str += '/';
    p -> glyphs_str += glyph_name;
  }
  return true;

}

} // unnamed namespace

player_t* wowhead_charplanner::download_player( sim_t* sim,
                                              unsigned list_id,
                                              cache::behavior_e caching )
{
  try
  {
    // Download main page
    std::string main_page = get_main_page( list_id, caching );

    // Get Json Document of the list manager section in the main page
    rapidjson::Document list_manager = extract_list_manager_section( main_page );

    if ( sim -> debug )
    {
      pretty_print_json( sim -> out_debug.raw(), list_manager );
    }

    // Check if all global player attributes are in the json document
    try
    {
      if ( ! list_manager.HasMember( "lists" ) || !list_manager["lists"].IsArray() ) throw( "lists" );
      if ( ! list_manager.HasMember( "name" ) ) throw( "name" );
      if ( ! list_manager.HasMember( "level" ) ) throw( "level" );
      if ( ! list_manager.HasMember( "classs" ) ) throw( "class" );
      if ( ! list_manager.HasMember( "talentspec" ) ) throw( "talentspec" );
      if ( ! list_manager.HasMember( "race" ) ) throw( "race" );
      if ( ! list_manager.HasMember( "skills" ) || ! list_manager[ "skills" ].IsArray() ) throw( "skills" );
    }
    catch ( const char* fieldname )
    {
      std::string error_str;
      if ( list_manager.HasMember( "reason" ) )
        error_str = list_manager[ "reason" ].GetString();

      sim -> errorf( "WOWHEAD API: Player '%s' Unable to extract field '%u': %s.\n", list_id,
            fieldname, error_str.c_str() );
      return nullptr;
    }

    // Load the global player fields
    std::string character_name = list_manager[ "name" ].GetString();
    unsigned character_level = list_manager[ "level" ].GetUint();
    player_e character_class = util::translate_class_id( list_manager[ "classs" ].GetInt() );
    race_e character_race = util::translate_race_id( list_manager[ "race" ].GetInt() );
    specialization_e character_spec = dbc::spec_by_idx( character_class, list_manager[ "talentspec" ].GetUint() );

    // Check if we could transform the relevant fields to enums
    if ( character_class == PLAYER_NONE ) throw wowhead_exception( "Could not parse player class.");
    if ( character_race == RACE_NONE ) throw wowhead_exception( "Could not parse player race.");
    if ( character_spec == SPEC_NONE ) throw wowhead_exception( "Could not parse player specialization.");

    // Character Professions
    std::vector<std::pair<profession_e,int> > character_professions;
    for (rapidjson::SizeType i = 0; i < list_manager[ "skills" ].Size(); ++i )
    {
      profession_e prof = util::translate_profession_id( list_manager[ "skills" ][ i ][ rapidjson::SizeType(0) ].GetInt() );
      int skill = list_manager[ "skills" ][ i ][ rapidjson::SizeType(1) ].GetInt();
      if ( prof != PROFESSION_NONE )
        character_professions.push_back( std::make_pair( prof, skill ) );
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

    // Set up Class Module
    const module_t* module = module_t::get( character_class );
    if ( ! module || ! module -> valid() )
    {
      sim -> errorf( "\nModule for class %s is currently not available.\n", util::player_type_string( character_class ) );
      return 0;
    }

    // Create Player
    player_t* p = sim -> active_player = module -> create_player( sim, character_name, character_race );
    if ( ! p )
    {
      sim -> errorf( "WOWHEAD API: Unable to build player with class '%s' and name '%s' from '%u'.\n",
          util::player_type_string( character_class ), character_name.c_str(), list_id );
      return nullptr;
    }

    // Transfer parsed Data to player
    p -> level = character_level;
    p -> _spec = character_spec;
    for( size_t i = 0; i < character_professions.size(); ++i )
    {
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

    // And we are done
    return p;
  }
  catch ( const wowhead_exception& e )
  {
    sim -> errorf( "WOWHEAD API: Unable to download player '%u': %s.\n",
        list_id, e.what() );
    return nullptr;
  }
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
