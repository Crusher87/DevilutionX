#include "lua/modules/poe2.hpp"

#include <string>
#include <string_view>

#include <sol/sol.hpp>

#include "game_mode.hpp"
#include "lua/metadoc.hpp"

namespace devilution {

sol::table LuaPoe2Module(sol::state_view &lua)
{
	sol::table table = lua.create_table();
	LuaSetDocFn(table, "enable", "()", []() { gbIsPoe2 = true; });
	LuaSetDocFn(table, "set_version", "(version: string)", "Sets the version the game menu shows",
	    [](std::string_view version) { Poe2ModVersion = std::string(version); });
	return table;
}

} // namespace devilution
