/**
 * @file poe2/atmosphere.hpp
 *
 * POE2 Mashup: the old Diablo feel in the HD world (the Atmosphere option). Curtis's ideas page, 2026-11-12: leave the
 * paint alone and work on the light, the air and what moves. Each area's numbers live in
 * txtdata/poe2/atmosphere.tsv, with one value for its first floor and one for its last, blended by how deep the floor
 * is. Each effect has a live switch in the console, feel(), so it can be judged standing in the game.
 *
 * Looks only: nothing here touches the game state, saves or the game's seeded random numbers.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "engine/world_tile.hpp"

namespace devilution::atmosphere {

/** @brief The Atmosphere option. */
enum class Level : uint8_t {
	Off,
	Subtle,
	Full,
};

/** @brief The effects, each with its own feel() switch. */
enum class Knob : uint8_t {
	/** Idea 4: each floor of an area grimmer than the one above. */
	Grimmer,
	/** Idea 6: the screen's edges and corners a touch darker. */
	Edges,
	/** Idea 1: firelight that flickers, with the shadows it casts. */
	Flicker,
	/** Idea 48: stairs down glow dully from below, stairs up take a faint cold light. */
	Stairs,
	/** Idea 10 (a maybe, off until tried): a spell's light fades out after it ends instead of going at once. */
	Linger,
	/** Idea 2: specks of dust in the air, seen only where light falls on them. */
	Dust,
	/** Idea 11: flies round hanging bodies and old corpses. */
	Flies,
	/** Idea 11: water dripping from the ceiling, a small ring where it lands. */
	Drips,
	/** Idea 11: smoke from torches and braziers rising higher and thinning out. */
	Smoke,
	/** Idea 3: a thin haze that glows round the flames and is gone in the dark (the Cathedral only, for now). */
	Mist,
	/** Idea 46: the famous rooms' own light and air (the Butcher's, for now). */
	Rooms,
	/** Idea 12: blood that dries darker and stays, soot from fire and frost from cold that slowly goes. */
	Marks,
	/**
	 * Solid things (sarcophagi, chests, barrels, fountains) take the light by their sides and cast real shadows. Part of
	 * HD Lighting itself, so it shows whatever the Atmosphere option says (see Setting).
	 */
	Objects,
	/** Objects whose HD art has a shape map beside it are lit by their shape; at 0, by their sides as the rest are. */
	Shapes,
	/**
	 * The shadows one light casts are joined: where two of them cross, the floor is no darker than under one, and a body
	 * standing in another's shadow adds none of its own from that light. At 0 each shadow darkens the floor by itself.
	 */
	Joined,
	/** A shadow that reaches a wall turned to the view goes on up the wall's face; at 0 it stops at the wall's foot. */
	Walls,
	/** Idea 24: cold daylight falls down the stairs from town on the Cathedral's first floor, with dust drifting in it. */
	Daylight,
	/** Idea 25: flames flutter and their light shivers as a hero or monster hurries past. */
	Flutter,
	/** Idea 30: the lava's glow swells and fades slowly, and heroes and monsters take an orange edge on the side turned to it. */
	Lava,
	/** Idea 31: the lava's surface wavers in its heat. */
	Haze,
	/** Idea 32: a cavern with lava in it is dim orange in the distance, not black. */
	FarGlow,
	/** Idea 23: while it rains the evening sky over Tristram is darker, so the lamps and windows stand out. */
	RainSky,
	/** Idea 23: lightning lights the town from the sky for a moment, with the shadows it casts; never a white screen. */
	Lightning,
	/** Ideas 23 and 14: wet ground shines under the lamps, puddles ring where drops land, roofs drip after the rain. */
	Wet,
	/** Idea 18: low mist lies along Tristram's stream and between the graves. */
	Fog,
	/** Idea 19: the stream and the fountain glint and move, and the fountain splashes a little. */
	Water,
	/** Idea 20: dead leaves blow across the ground with the wind, more in a storm. */
	Wind,
	/** Idea 21: the cathedral's doorway breathes a red glow and cold mist creeps out of it, stronger the deeper the hero has been. */
	Cathedral,
	/**
	 * Not a look: HD pictures are read from disk off the frame, before they come into view (see hd.cpp's reader). At 0
	 * each is read in the frame that first shows it, as before.
	 */
	ReadAhead,
	/** Idea 47: when a door opens, the room beyond comes up from the dark over a third of a second, and dust swirls in the doorway. */
	Doors,
	/** Idea 49: taking stairs down, the dark closes in on the hero as the level fades, and opens out again on the new floor. */
	Descent,
	/** Idea 50: when the last monster of a fight dies the dust settles, the mist rolls back in and the flames steady for a second. */
	Quiet,
	/** Idea 53: when the hero dies their light shrinks, the flames near them die down and the colour drains to red. */
	Death,
};
inline constexpr size_t NumKnobs = 33;

/** @brief How strong an effect is now: the option's level times its feel() setting; 0 when off or without HD Lighting. */
float Strength(Knob knob);

/** @brief An effect's feel() setting alone, for the looks that belong to HD Lighting and show with the option off. */
float Setting(Knob knob);

/** @brief How far down its area this floor is: 0 on the first floor, 1 on the last (0 in town). */
float Depth();

/**
 * @brief This area's number from the table at a depth (0 its first floor, 1 its last, past 1 further along the same
 * line), or the fallback when the table has none for it.
 */
float Value(std::string_view name, float fallback, float depth);
/** @brief The same at this floor's depth. */
float Value(std::string_view name, float fallback);
/** @brief A special room's number from the table (rows whose area is the room's name, such as "butcher"), or the fallback. */
float RoomValue(std::string_view room, std::string_view name, float fallback);
std::array<float, 3> RoomColour(std::string_view room, std::string_view name, const std::array<float, 3> &fallback);

/** @brief The Butcher's room on this floor, in tiles, while his quest is here (idea 46); empty otherwise. */
std::optional<WorldTileRectangle> ButcherRoom();
/** @brief A colour (three numbers) from the table, as Value. */
std::array<float, 3> Colour(std::string_view name, const std::array<float, 3> &fallback, float depth);
std::array<float, 3> Colour(std::string_view name, const std::array<float, 3> &fallback);
/** @brief Whether the table has this number for this area. */
bool Has(std::string_view name);

/** @brief Changes whenever anything the level's baked light depends on changes: the option, a switch, the table. */
uint32_t Version();

/** @brief The console's feel(): lists the switches, sets one (feel("flicker", 0)), or "reset" / "reload". */
std::string Feel(const std::optional<std::string> &name, const std::optional<double> &value);

} // namespace devilution::atmosphere
