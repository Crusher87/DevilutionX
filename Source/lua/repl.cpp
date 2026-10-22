#ifdef _DEBUG
#include "lua/repl.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <sol/sol.hpp>
#include <sol/utility/to_string.hpp>

#include "lua/lua_global.hpp"
#include "panels/console.hpp"
#include "player.h"
#include "levels/gendung.h"
#include "levels/setmaps.h"
#include "interfac.h"
#include "cursor.h"
#include "missiles.h"
#include "poe2/aim.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/hit_trace.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/tree_telemetry.hpp"
#include "poe2/vfx.hpp"
#include "poe2/weather.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

std::optional<sol::environment> replEnv;

void LuaConsoleWarn(void * /*userData*/, const char *message, int continued)
{
	static std::string warnBuffer;
	warnBuffer.append(message);
	if (continued != 0)
		return;
	PrintWarningToConsole(warnBuffer);
	warnBuffer.clear();
}

int LuaPrintToConsole(lua_State *state)
{
	std::string result;
	const int n = lua_gettop(state);
	for (int i = 1; i <= n; i++) {
		size_t l;
		const char *s = luaL_tolstring(state, i, &l);
		if (i > 1)
			result += '\t';
		result.append(s, l);
		lua_pop(state, 1);
	}
	PrintToConsole(result);
	return 0;
}

void CreateReplEnvironment()
{
	sol::environment env = CreateLuaSandbox();
	env["print"] = LuaPrintToConsole;
	// POE2 Mashup play log: stats() shows this game's numbers, mark("text") notes a moment in the log.
	env["stats"] = []() {
		return StrCat(TelemetryStatsText(), "\n", TelemetrySkillDamageText(), "\n", TelemetryDamageTakenText(), "\n", MyPlayer != nullptr ? TelemetryBuildText(*MyPlayer) : std::string());
	};
	// gems() shows each socketed gem's numbers now and at its next level; gemcurve("Cleave") shows levels 1 to 20.
	env["gems"] = []() {
		return MyPlayer != nullptr ? SocketedGemsText(*MyPlayer) : std::string("No hero loaded.");
	};
	env["gemcurve"] = [](std::string_view name) {
		return GemCurveTextByName(name);
	};
	// vfx() plays one of each modern effect around the hero.
	env["vfx"] = []() {
		if (MyPlayer == nullptr || !vfx::Enabled())
			return "Modern Effects are off (Settings > Graphics, needs Upscale).";
		vfx::Showcase(MyPlayer->position.tile);
		// The console covers the effects, so it gets out of the way.
		CloseConsole();
		return "Playing every effect around you.";
	};
	// weather() shows what the sky is doing; weather("rain", 0.8) or weather("snow") forces a look, weather("auto") hands it back.
	env["weather"] = [](sol::optional<std::string> what, sol::optional<double> strength) {
		// The console covers the effects while it's open, so it closes to show the change (Curtis 2026-10-21:
		// weather("rain", 0.9) seemed to do nothing).
		const auto changed = [](std::string text) {
			if (weather::Enabled() && weather::Outdoors()) {
				CloseConsole();
				text += "\nClosed the console so you can see it.";
			}
			return text;
		};
		if (what) {
			if (*what == "auto") {
				weather::Force(std::nullopt);
				return changed(std::string("Weather follows its schedule again.\n") + weather::Text());
			}
			const std::optional<weather::Kind> kind = weather::KindNamed(*what);
			if (!kind)
				return std::string("Say weather(\"rain\"), weather(\"snow\"), weather(\"clear\") or weather(\"auto\"), with a strength 0 to 1 after rain or snow.");
			weather::Force(kind, static_cast<float>(strength.value_or(0.8)));
			weather::Advance(1);
			return changed(weather::Text());
		}
		return weather::Text();
	};
	env["aim"] = []() {
		if (MyPlayer == nullptr)
			return std::string("No hero.");
		float x = 0;
		float y = 0;
		GetCursorTileExact(x, y);
		const Point target = AimAlongCursor(*MyPlayer, AimTiles);
		AddMissile(MyPlayer->position.tile, target, MyPlayer->_pdir, MissileID::Firebolt, TARGET_MONSTERS, *MyPlayer, 0, 1);
		return StrCat("hero ", MyPlayer->position.tile.x, ",", MyPlayer->position.tile.y, "  cursor tile ", cursPosition.x, ",", cursPosition.y,
		    "  exact ", static_cast<int>(x * 100), ",", static_cast<int>(y * 100), " /100  aim ", target.x, ",", target.y);
	};
	// hittrace(true) logs every tile your spells fly through, not just one line per spell; hittrace(false) goes back.
	env["hittrace"] = [](bool detail) {
		HitTraceSetDetail(detail);
		return detail ? "Hit trace logs every tile now." : "Hit trace logs one line per spell.";
	};
	// log() shows each play log channel's loudness; log("damage", "deep") breaks down every hit until the game closes.
	env["log"] = [](sol::optional<std::string> channel, sol::optional<std::string> level) {
		return TelemetryLogLevelsText(channel.value_or(""), level.value_or(""));
	};
	// tree() shows your passive points, notables by area, Profane Roads walked and what the tree adds up to.
	env["tree"] = []() {
		return MyPlayer != nullptr ? TreeTrackerText(*MyPlayer) : std::string("No hero loaded.");
	};
	// mixtest() takes you to the tile-set mixing test map: a Cathedral hall with a Tristram yard in it.
	env["mixtest"] = []() {
		if (MyPlayer == nullptr)
			return std::string("No hero loaded.");
		if (setlevel && setlvlnum == SL_MIXTEST)
			return std::string("You are already on the tile mix test map.");
		setlvltype = DTYPE_CATHEDRAL;
		StartNewLvl(*MyPlayer, WM_DIABSETLVL, SL_MIXTEST);
		return std::string("Going to the tile mix test map. Walk down into the hall; the exit is where you came in.");
	};
	env["mark"] = [](std::string_view text) {
		TelemetryMark(text);
		return TelemetryEnabled() ? "Marked in the play log." : "The play log isn't open.";
	};
	lua_setwarnf(env.lua_state(), LuaConsoleWarn, /*ud=*/nullptr);
	replEnv.emplace(env);
}

sol::protected_function_result TryRunLuaAsExpressionThenStatement(std::string_view code)
{
	// Try to compile as an expression first. This also how the `lua` repl is implemented.
	sol::state &lua = GetLuaState();
	std::string expression = StrCat("return ", code, ";");
	sol::detail::typical_chunk_name_t basechunkname = {};
	sol::load_status status = static_cast<sol::load_status>(
	    luaL_loadbufferx(lua.lua_state(), expression.data(), expression.size(),
	        sol::detail::make_chunk_name(expression, sol::detail::default_chunk_name(), basechunkname), "text"));
	if (status != sol::load_status::ok) {
		// Try as a statement:
		status = static_cast<sol::load_status>(
		    luaL_loadbufferx(lua.lua_state(), code.data(), code.size(),
		        sol::detail::make_chunk_name(code, sol::detail::default_chunk_name(), basechunkname), "text"));
		if (status != sol::load_status::ok) {
			return sol::protected_function_result(
			    lua.lua_state(), sol::absolute_index(lua.lua_state(), -1), 0, 1, static_cast<sol::call_status>(status));
		}
	}
	sol::stack_aligned_protected_function fn(lua.lua_state(), -1);
	sol::set_environment(GetLuaReplEnvironment(), fn);
	return fn();
}

} // namespace

sol::environment &GetLuaReplEnvironment()
{
	if (!replEnv.has_value())
		CreateReplEnvironment();
	return *replEnv;
}

std::expected<std::string, std::string> RunLuaReplLine(std::string_view code)
{
	const sol::protected_function_result result = TryRunLuaAsExpressionThenStatement(code);
	if (!result.valid()) {
		if (result.get_type() == sol::type::string) {
			return std::unexpected(result.get<std::string>());
		}
		return std::unexpected("Unknown Lua error");
	}
	if (result.get_type() == sol::type::none) {
		return std::string {};
	}
	return sol::utility::to_string(sol::stack_object(result));
}

void LuaReplShutdown()
{
	replEnv = std::nullopt;
}

} // namespace devilution
#endif // _DEBUG
