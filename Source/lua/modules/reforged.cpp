#include "lua/modules/reforged.hpp"

#include <sol/sol.hpp>

#include "game_mode.hpp"
#include "lua/metadoc.hpp"

namespace devilution {

sol::table LuaReforgedModule(sol::state_view &lua)
{
	sol::table table = lua.create_table();
	// POE2 Mashup has the look and comfort features already: beside it this does nothing (see LuaReloadActiveMods).
	LuaSetDocFn(table, "enable", "()", "Switches the HD look and the comfort features on, on plain Diablo and Hellfire rules",
	    []() { gbIsReforged = !gbIsPoe2; });
	LuaSetDocFn(table, "is_enabled", "()", "Whether the game runs as Sanctuary Reforged", []() { return gbIsReforged; });
	LuaSetDocFn(table, "has_enhancements", "()", "Whether the HD look and the comfort features are on (Sanctuary Reforged or POE2 Mashup)",
	    []() { return HasEnhancements(); });
	return table;
}

} // namespace devilution
