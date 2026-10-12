#include "lua/modules/poe2.hpp"

#include <sol/sol.hpp>

#include "game_mode.hpp"
#include "lua/metadoc.hpp"

namespace devilution {

sol::table LuaPoe2Module(sol::state_view &lua)
{
	sol::table table = lua.create_table();
	LuaSetDocFn(table, "enable", "()", []() { gbIsPoe2 = true; });
	return table;
}

} // namespace devilution
