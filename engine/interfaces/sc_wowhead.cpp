// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simulationcraft.hpp"

#include "util/rapidjson/document.h"
#include "util/rapidjson/stringbuffer.h"
#include "util/rapidjson/prettywriter.h"
#include <sstream>

namespace {
// source_str ===============================================================

std::string source_str( wowhead::wowhead_e source )
{
  switch ( source )
  {
    case wowhead::PTR:  return "ptr";
#if SC_BETA
    case wowhead::BETA: return SC_BETA_STR;
#endif
    default:   return "www";
  }
}

std::string source_desc_str( wowhead::wowhead_e source )
{
  switch ( source )
  {
    case wowhead::PTR:  return "PTR";
#if SC_BETA
    case wowhead::BETA: return "Beta";
#endif
    default:   return "Live";
  }
}

// download_id ==============================================================

std::shared_ptr<xml_node_t> download_id( sim_t*             sim,
                                unsigned           id,
                                cache::behavior_e  caching,
                                wowhead::wowhead_e source )
{
  if ( ! id )
    return std::shared_ptr<xml_node_t>();

  std::string url_www = "http://" + source_str( source ) + ".wowhead.com/item="
                        + util::to_string( id ) + "&xml";

  std::shared_ptr<xml_node_t> node = xml_node_t::get( sim, url_www, caching, "</json>" );
  if ( sim -> debug && node ) node -> print();
  return node;
}

std::string domain_str( wowhead::wowhead_e domain )
{
  switch ( domain )
  {
    case wowhead::PTR: return "ptr";
#if SC_BETA
    case wowhead::BETA: return SC_BETA_STR;
#endif
    default: return "www";
  }
}

} // unnamed namespace

// wowhead::download_glyph ==================================================

bool wowhead::download_glyph( player_t*          player,
                              std::string&       glyph_name,
                              const std::string& glyph_id,
                              wowhead_e          source,
                              cache::behavior_e  caching )
{
  unsigned glyphid = strtoul( glyph_id.c_str(), 0, 10 );
  std::shared_ptr<xml_node_t> node = download_id( player -> sim, glyphid, caching, source );
  if ( ! node || ! node -> get_value( glyph_name, "name/cdata" ) )
  {
    if ( caching != cache::ONLY )
      player -> sim -> errorf( "Unable to download glyph id %s from wowhead\n", glyph_id.c_str() );
    return false;
  }

  return true;
}

// download_item_data =======================================================

bool wowhead::download_item_data( item_t&            item,
                                  cache::behavior_e  caching,
                                  wowhead_e          source )
{
  std::shared_ptr<xml_node_t> xml = item.xml = download_id( item.sim, item.parsed.data.id, caching, source );

  if ( ! xml )
  {
    if ( caching != cache::ONLY )
      item.sim -> errorf( "Player %s unable to download item id '%u' from wowhead at slot %s.\n", item.player -> name(), item.parsed.data.id, item.slot_name() );
    return false;
  }

  try
  {
    int id;
    if ( ! xml -> get_value( id, "item/id" ) ) throw( "id" );
    item.parsed.data.id = id;

    if ( ! xml -> get_value( item.name_str, "name/cdata" ) ) throw( "name" );
    util::tokenize( item.name_str );

    xml -> get_value( item.icon_str, "icon/cdata" );

    if ( ! xml -> get_value( item.parsed.data.level, "level/." ) ) throw( "level" );

    if ( ! xml -> get_value( item.parsed.data.quality, "quality/id" ) ) throw( "quality" );

    std::string jsonequipdata, jsondata;
    xml -> get_value( jsonequipdata, "jsonEquip/cdata" );
    jsonequipdata = "{" + jsonequipdata + "}";
    xml -> get_value( jsondata, "json/cdata" );
    jsondata = "{" + jsondata + "}";

    rapidjson::Document json, jsonequip;
    json.Parse< 0 >( jsondata.c_str() );
    jsonequip.Parse< 0 >( jsonequipdata.c_str() );

    if ( json.HasParseError() )
    {
      item.sim -> errorf( "Unable to parse JSON data for item id '%u': %s", 
          id, json.GetParseError() );
      return false;
    }

    if ( jsonequip.HasParseError() )
    {
      item.sim -> errorf( "Unable to parse JSON data for item id '%u': %s", 
          id, jsonequip.GetParseError() );
      return false;
    }

    if ( item.sim -> debug )
    {
      rapidjson::StringBuffer b;
      rapidjson::PrettyWriter< rapidjson::StringBuffer > writer( b );

      json.Accept( writer );
      item.sim -> out_debug.raw() << b.GetString();

      jsonequip.Accept( writer );
      item.sim -> out_debug.raw() << b.GetString();
    }

    if ( ! json.HasMember( "slot" ) )
      throw( "inventory type" );

    if ( ! json.HasMember( "classs" ) )
      throw( "item class" );

    if ( ! json.HasMember( "subclass" ) )
      throw( "item subclass" );

    item.parsed.data.inventory_type = json[ "slot" ].GetInt();
    item.parsed.data.item_class = json[ "classs" ].GetInt();
    item.parsed.data.item_subclass = json[ "subclass" ].GetInt();
    if ( item.parsed.data.item_subclass < 0 )
      item.parsed.data.item_subclass = 0;

    if ( json.HasMember( "reqlevel" ) )
      item.parsed.data.req_level = json[ "reqlevel" ].GetInt();

    if ( json.HasMember( "heroic" ) )
      item.parsed.data.type_flags |= RAID_TYPE_HEROIC;

    if ( json.HasMember( "flexible" ) )
      item.parsed.data.type_flags |= RAID_TYPE_FLEXIBLE;

    if ( json.HasMember( "raidfinder" ) )
      item.parsed.data.type_flags |= RAID_TYPE_LFR;

    if ( json.HasMember( "warforged" ) )
      item.parsed.data.type_flags |= RAID_TYPE_ELITE;

    if ( json.HasMember( "thunderforged" ) )
      item.parsed.data.type_flags |= RAID_TYPE_ELITE;

    if ( item.parsed.data.item_class == ITEM_CLASS_WEAPON )
    {
      if ( ! jsonequip.HasMember( "dmgrange" ) )
        throw( "weapon damage range" );

      if ( ! jsonequip.HasMember( "speed" ) )
        throw( "weapon speed" );
    }

    if ( jsonequip.HasMember( "reqskill" ) )
      item.parsed.data.req_skill = jsonequip[ "reqskill" ].GetInt();

    if ( jsonequip.HasMember( "reqskillrank" ) )
      item.parsed.data.req_skill_level = jsonequip[ "reqskillrank" ].GetInt();

    // Todo binding type, needs htmlTooltip parsing
    if ( item.parsed.data.item_class == ITEM_CLASS_WEAPON )
    {
      item.parsed.data.delay = jsonequip[ "speed" ].GetDouble() * 1000.0;
      item.parsed.data.dmg_range = jsonequip[ "dmgrange" ].GetDouble();
    }

    int races = -1;
    if ( jsonequip.HasMember( "races" ) )
      races = jsonequip[ "races" ].GetInt();
    item.parsed.data.race_mask = races;

    int classes = -1;
    if ( jsonequip.HasMember( "classes" ) )
      classes = jsonequip[ "classes" ].GetInt();
    item.parsed.data.class_mask = classes;

    size_t n = 0;
    for ( rapidjson::Value::ConstMemberIterator i = jsonequip.MemberBegin(); 
          i != jsonequip.MemberEnd() && n < sizeof_array( item.parsed.data.stat_type_e ); i++ )
    {
      stat_e type = util::parse_stat_type( i -> name.GetString() );
      if ( type == STAT_NONE || type == STAT_ARMOR || util::translate_stat( type ) == ITEM_MOD_NONE ) 
        continue;

      item.parsed.data.stat_type_e[ n ] = util::translate_stat( type );
      item.parsed.data.stat_val[ n ] = i -> value.GetInt();
      n++;

      // Soo, weapons need a flag to indicate caster weapon for correct DPS calculation.
      if ( item.parsed.data.delay > 0 && ( 
           item.parsed.data.stat_type_e[ n - 1 ] == ITEM_MOD_INTELLECT || 
           item.parsed.data.stat_type_e[ n - 1 ] == ITEM_MOD_SPIRIT ||
           item.parsed.data.stat_type_e[ n - 1 ] == ITEM_MOD_SPELL_POWER ) )
        item.parsed.data.flags_2 |= ITEM_FLAG2_CASTER_WEAPON;
    }

    int n_sockets = 0;
    if ( jsonequip.HasMember( "nsockets" ) )
      n_sockets = jsonequip[ "nsockets" ].GetUint();
    
    assert( n_sockets <= static_cast< int >( sizeof_array( item.parsed.data.socket_color ) ) );
    char socket_str[32];
    for ( int i = 0; i < n_sockets; i++ )
    {
      snprintf( socket_str, sizeof( socket_str ), "socket%d", i + 1 );
      if ( jsonequip.HasMember( socket_str ) )
        item.parsed.data.socket_color[ i ] = jsonequip[ socket_str ].GetUint();
    }

    if ( jsonequip.HasMember( "socketbonus" ) )
      item.parsed.data.id_socket_bonus = jsonequip[ "socketbonus" ].GetUint();

    if ( jsonequip.HasMember( "itemset" ) )
      item.parsed.data.id_set =  std::abs( jsonequip[ "itemset" ].GetInt() );

    // Sad to the face
    std::string htmltooltip;
    xml -> get_value( htmltooltip, "htmlTooltip/cdata" );

    // Parse out Equip: and On use: strings
    int spell_idx = 0;

    std::shared_ptr<xml_node_t> htmltooltip_xml = xml_node_t::create( item.sim, htmltooltip );
    //htmltooltip_xml -> print( item.sim -> output_file, 2 );
    std::vector<xml_node_t*> spell_links = htmltooltip_xml -> get_nodes( "span" );
    for ( size_t i = 0; i < spell_links.size(); i++ )
    {
      int trigger_type = -1;
      unsigned spell_id = 0;

      std::string v;
      if ( spell_links[ i ] -> get_value( v, "." ) && v != "Equip: " && v != "Use: " )
        continue;

      if ( v == "Use: " )
        trigger_type = ITEM_SPELLTRIGGER_ON_USE;
      else if ( v == "Equip: " )
        trigger_type = ITEM_SPELLTRIGGER_ON_EQUIP;

      std::string url;
      if ( ! spell_links[ i ] -> get_value( url, "a/href" ) )
        continue;

      size_t begin = url.rfind( "=" );
      if ( begin == std::string::npos )
        continue;
      else
        begin++;

      spell_id = util::to_unsigned( url.substr( begin ) );
      if ( spell_id > 0 && trigger_type != -1 )
      {
        item.parsed.data.id_spell[ spell_idx ] = spell_id;
        item.parsed.data.trigger_spell[ spell_idx ] = trigger_type;
        spell_idx++;
      }
    }
  }
  catch ( const char* fieldname )
  {
    std::string error_str;

    xml -> get_value( error_str, "error/." );

    if ( caching != cache::ONLY )
      item.sim -> errorf( "Wowhead (%s): Player %s unable to parse item '%u' %s in slot '%s': %s\n",
                          source_desc_str( source ).c_str(), item.player -> name(), item.parsed.data.id,
                          fieldname, item.slot_name(), error_str.c_str() );
    return false;
  }

  return true;
}

// wowhead::download_item ===================================================

bool wowhead::download_item( item_t&            item,
                             wowhead_e          source,
                             cache::behavior_e  caching )
{
  bool ret = download_item_data( item, caching, source );

  if ( ret )
    item.source_str = "Wowhead";

  return ret;
}

std::string wowhead::decorated_spell_name( const std::string& name,
                                           unsigned spell_id,
                                           const std::string& spell_name,
                                           wowhead_e domain,
                                           const std::string& href_parm,
                                           bool affix )
{
  std::string decorated_name, base_href, prefix, suffix;

  if ( spell_id > 1 )
  {
    base_href = "http://" + domain_str( domain ) + ".wowhead.com/spell=" + util::to_string( spell_id );

    if ( affix )
    {
      std::string::size_type affix_offset = name.find( spell_name );

      // Add an affix to the name, if the name does not match the 
      // spell name. Affix is either the prefix- or suffix portion of the 
      // non matching parts of the stats name.
      if ( affix_offset != std::string::npos && spell_name != name )
      {
        // Suffix
        if ( affix_offset == 0 )
          suffix += " (" + name.substr( spell_name.size() ) + ")";
        // Prefix
        else if ( affix_offset > 0 )
          prefix += " (" + name.substr( 0, affix_offset ) + ")";
      }
      else if ( affix_offset == std::string::npos )
        suffix += " (" + name + ")";
    }
  }

  if ( ! base_href.empty() )
  {
    if ( ! prefix.empty() )
      decorated_name += prefix + "&nbsp;";

    decorated_name += "<a href=\"" + base_href + "\" " + href_parm.c_str() + ">" + name + "</a>";

    if ( ! suffix.empty() )
      decorated_name += suffix;
  }
  else
    decorated_name = name;

  return decorated_name;
}

std::string wowhead::decorated_action_name( const std::string& name,
                                            action_t* action,
                                            wowhead_e domain, 
                                            const std::string& href_parm,
                                            bool affix )
{
  std::string spell_name;
  unsigned spell_id = 0;
  if ( ! action )
    return name;

  // Kludge auto attack decorations
  attack_t* a = dynamic_cast< attack_t* >( action );
  if ( a && a -> auto_attack )
  {
    spell_id = 6603;
    if ( a -> weapon )
      spell_name = "Auto Attack";
  }
  else
  {
    spell_id = action -> id;
    if ( spell_id > 1 )
      spell_name = action -> s_data -> name_cstr();
  }

  util::tokenize( spell_name );

  return decorated_spell_name( name, spell_id, spell_name, domain, href_parm, affix );
}

std::string wowhead::decorated_buff_name( const std::string& name,
                                          buff_t* buff,
                                          wowhead_e domain, 
                                          const std::string& href_parm,
                                          bool affix )
{
  std::string spell_name;
  unsigned spell_id = 0;

  if ( buff -> data().id() > 0 )
  {
    spell_id = buff -> data().id();
    spell_name = buff -> data().name_cstr();
  }

  util::tokenize( spell_name );

  return decorated_spell_name( name, spell_id, spell_name, domain, href_parm, affix );
}

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

player_t* wowhead::download_player( sim_t* sim,
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
      std::stringstream error;
      error << "Unable to extract field '" << fieldname << "'";
      if ( list_manager.HasMember( "reason" ) )
        error << ": " << list_manager[ "reason" ].GetString();

      throw wowhead_exception( error.str() );
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
