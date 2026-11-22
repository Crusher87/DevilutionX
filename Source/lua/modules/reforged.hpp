#pragma once

#include <sol/sol.hpp>

namespace devilution {

/** @brief The `devilutionx.reforged` Lua package: the Sanctuary Reforged mod switches itself on through it. */
sol::table LuaReforgedModule(sol::state_view &lua);

} // namespace devilution
