#include "poe2/atmosphere.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "data/file.hpp"
#include "data/mode_tables.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "poe2/area_levels.hpp"
#include "quests.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"
#include "utils/string_view_hash.hpp"

namespace devilution::atmosphere {

namespace {

constexpr std::string_view TableFile = "atmosphere.tsv";

/** @brief A number (or a colour) for one area: at its first floor and at its last. */
struct Row {
	std::array<float, 3> first {};
	std::array<float, 3> last {};
	int count = 0;
};

/** @brief The table, by area (the level type) and then by the number's name. */
std::array<ankerl::unordered_dense::map<std::string, Row, StringViewHash, StringViewEquals>, 8> Areas;
bool Loaded = false;
uint32_t LoadCount = 0;

struct KnobInfo {
	std::string_view name;
	/** @brief Its setting until feel() changes it: 1 for the ideas Curtis said yes to, 0 for the maybes he wants to try. */
	float standard;
	std::string_view what;
};

constexpr std::array<KnobInfo, NumKnobs> Knobs { {
	{ "grimmer", 1.F, "each floor darker than the last (idea 4)" },
	{ "edges", 1.F, "darker screen edges (idea 6)" },
	{ "flicker", 1.F, "firelight flickers, shadows sway with it (idea 1)" },
	{ "stairs", 1.F, "stairs show where they lead (idea 48)" },
	{ "linger", 0.F, "spell light fades out slowly (idea 10, a maybe: try it)" },
	{ "dust", 1.F, "dust in the air, seen only in the light (idea 2)" },
	{ "flies", 1.F, "flies round hanging bodies and old corpses (idea 11)" },
	{ "drips", 1.F, "water drips from the ceiling (idea 11)" },
	{ "smoke", 1.F, "smoke from flames rises higher and thins (idea 11)" },
	{ "mist", 1.F, "a thin haze glowing round the flames, Cathedral only for now (idea 3)" },
	{ "rooms", 1.F, "the Butcher's room: red light, flies and dripping blood (idea 46)" },
	{ "marks", 1.F, "blood dries and stays, fire leaves soot, cold leaves frost (idea 12)" },
	{ "objects", 1.F, "chests, coffins, barrels and fountains lit by their sides, with real shadows (works with Atmosphere off)" },
	{ "shapes", 1.F, "objects that have a shape map are lit by their shape; 0 lights them by their sides like the rest" },
	{ "joined", 1.F, "shadows from one light are joined, never darker where they cross; 0 stacks them (works with Atmosphere off)" },
	{ "walls", 1.F, "shadows that reach a wall go on up it; 0 stops them at the wall's foot (works with Atmosphere off)" },
	{ "daylight", 1.F, "cold daylight down the stairs from town, Cathedral floor 1 (idea 24)" },
	{ "flutter", 1.F, "flames flutter as you or a monster hurry past (idea 25)" },
	{ "lava", 1.F, "lava's glow swells and fades, orange edge on who stands by it (idea 30)" },
	{ "haze", 1.F, "the lava wavers in its heat (idea 31)" },
	{ "farglow", 1.F, "caverns with lava are dim orange far off, not black (idea 32)" },
	{ "rainsky", 1.F, "the evening is darker while it rains, so lamps and windows stand out (idea 23)" },
	{ "lightning", 1.F, "lightning lights the town from the sky for a moment, never a white screen (idea 23)" },
	{ "wet", 1.F, "wet ground shines under the lamps, puddles ring, roofs drip after rain (ideas 23 and 14)" },
	{ "fog", 1.F, "low mist along Tristram's stream and between the graves (idea 18)" },
	{ "water", 1.F, "the stream and the fountain glint and move, the fountain splashes (idea 19)" },
	{ "wind", 1.F, "dead leaves blow over the ground with the wind, more in a storm (idea 20)" },
	{ "cathedral", 1.F, "the cathedral door breathes red and cold mist creeps out, more the deeper you've been (idea 21)" },
	{ "readahead", 1.F, "HD pictures load in the background before they come into view; 0 loads each as it first shows, the old way (works with Atmosphere off)" },
	{ "doors", 1.F, "a door opens and the room behind it comes up from the dark, with a puff of dust; 2 takes twice as long (idea 47)" },
	{ "descent", 1.F, "taking stairs down, the dark closes in on you and opens again on the new floor (idea 49)" },
	{ "quiet", 1.F, "after a fight the dust settles, the mist rolls back and the flames steady for a second (idea 50)" },
	{ "death", 1.F, "when you die your light shrinks, the flames near you die down and the colour drains to red (idea 53)" },
	{ "hidden", 1.F, "the unseen monsters show as a faint bending of the air while they move; 0 leaves them unseen as in the old game (idea 57)" },
	{ "cobwebs", 1.F, "cobwebs in the wall corners that tremble in the draft (idea 64)" },
	{ "rats", 1.F, "a rat or two runs off into the dark as your light reaches it (idea 62)" },
	{ "glow", 1.F, "a wide soft glow round flames, lamps and lit windows; 2 twice as bright (idea 66)" },
	{ "shafts", 1.F, "a shaft of lit air out of a bright doorway; 0.5 half as bright (idea 66)" },
	{ "reflect", 1.F, "you show in Tristram's stream when you stand at its edge, and the water takes a little of the sky (idea 66)" },
	{ "tileshapes", 1.F, "walls and floors that have a shape map are lit by their shape, so stone edges and pillars catch the light; 0 lights them flat (works with Atmosphere off)" },
	{ "lens", 1.F, "in a Tristram storm rain gathers on the screen as on a window: droplets, pools where it lands close together, and now and then a run down the glass; 2 twice as much" },
	{ "batch", 0.F, "an experiment for the frame rate: the HD world's tiles are drawn from a few big pictures in few draws; the picture should not change (works with Atmosphere off)" },
	{ "corpses", 1.F, "dead bodies take the floor's light smoothly across them, shadows fall over them, and those with a shape map are lit by their shape; 0 gives each one flat light as before (works with Atmosphere off)" },
	{ "carved", 1.F, "heroes' and monsters' shadows are cast from their carved bodies, each light seeing the body from its own side, where the bodies have been made; 0 casts the drawing facing you as before (works with Atmosphere off)" },
	{ "bodylight", 1.F, "heroes and monsters with a carved body are lit by its shape: the side turned to a light brighter, the far side darker; 2 twice as strong, up to 3; 0 lights each one flat as before (turning it on or off shows after the level changes; works with Atmosphere off)" },
	{ "seams", 1.F, "HD tiles are made solid wherever the game's own tile is solid, so no thin dark lines show between tiles and at the feet of buildings; 0 shows them as before, a number above 1 fills that many HD pixels deep (works with Atmosphere off)" },
	{ "seamlight", 1.F, "floors that have a shape map are lit as floors, so flat floor takes the same light from one tile to the next; 0 lights them as things standing on the floor, as before (works with Atmosphere off)" },
	{ "gaps", 0.F, "for finding gaps: bright pink is drawn under the HD world, so any hole left between tiles shows pink (works with Atmosphere off)" },
	{ "contact", 1.F, "blending in: a deeper soft dark spot right under everyone and everything that stands on the floor; 2 twice as deep, 0 as before (works with Atmosphere off)" },
	{ "shadetint", 1.F, "blending in: cast shadows take a little of the area's own colour in place of plain black; 0 plain as before (works with Atmosphere off)" },
	{ "far", 1.F, "blending in: far from a light, things lean to the area's colour and a faint veil of it lies over them before the dark; 0 as before" },
	{ "bounce", 1.F, "blending in: legs and lower bodies take a little of the light lying on the floor under them; 2 twice as much, 0 none (works with Atmosphere off)" },
	{ "mood", 1.F, "blending in: one colour mood over the whole picture, spells and all: dark parts lean cool, lit parts lean to firelight; 2 twice as strong, 0 none" },
	{ "wrap", 0.F, "blending in: a body in front of a strong light gets a thin soft edge of that light on the side turned to it; off (Curtis 11-28: looks terrible), 1 on" },
	{ "echo", 0.4F, "blending in: the game's own sounds ring a little in big halls and caverns, by the size of the space round you; 0.4 as Curtis chose (11-28), 1 stronger, 0 none (works with Atmosphere off)" },
	{ "foothaze", 0.F, "blending in, a Maybe (off; Curtis 11-28: looks terrible): a very thin haze of the room's air over feet and the foot of things, in the Catacombs and Crypt; 1 on, 2 twice as much" },
	{ "react", 0.F, "blending in, a Maybe (off; Curtis 11-28: keep off): feet stir a little dust off the floor as heroes and monsters walk (needs the dust in the air on); 1 on, 2 twice as much" },
	{ "sheen", 1.F, "blending in: worn floors catch a soft shine from the lamps and fires near them (Curtis 11-28: on, at his strength); 2 twice as bright, 0 none" },
	{ "hellcrack", 1.F, "town: the crack down to the Hive (Hellfire) glows and breathes, lights the ground and people round it red, throws up embers and smoke, steams in the rain and now and then surges; 2 twice as much, 0 as the art has it" },
} };

/** @brief feel()'s settings, kept until the game closes. */
std::array<float, NumKnobs> Settings = [] {
	std::array<float, NumKnobs> settings {};
	for (size_t i = 0; i < NumKnobs; i++)
		settings[i] = Knobs[i].standard;
	return settings;
}();
uint32_t SettingsVersion = 0;

int AreaIndex(std::string_view name)
{
	constexpr std::array<std::string_view, 8> Names { "town", "cathedral", "catacombs", "caves", "hell", "hive", "crypt", "butcher" };
	for (size_t i = 0; i < Names.size(); i++) {
		if (Names[i] == name)
			return static_cast<int>(i);
	}
	return -1;
}

int CurrentArea()
{
	switch (leveltype) {
	case DTYPE_TOWN: return 0;
	case DTYPE_CATHEDRAL: return 1;
	case DTYPE_CATACOMBS: return 2;
	case DTYPE_CAVES: return 3;
	case DTYPE_HELL: return 4;
	case DTYPE_NEST: return 5;
	case DTYPE_CRYPT: return 6;
	default: return -1;
	}
}

/** @brief One to three numbers split by spaces; how many were read (0 when the text isn't numbers). */
int ParseNumbers(const std::string &text, std::array<float, 3> &out)
{
	int count = 0;
	const char *at = text.c_str();
	while (count < 3) {
		while (*at == ' ')
			at++;
		if (*at == '\0')
			break;
		char *end = nullptr;
		const float value = std::strtof(at, &end);
		if (end == at)
			return 0;
		out[static_cast<size_t>(count++)] = value;
		at = end;
	}
	// One number stands for all three.
	for (int c = count; c > 0 && c < 3; c++)
		out[static_cast<size_t>(c)] = out[0];
	return count;
}

void Load()
{
	Loaded = true;
	LoadCount++;
	for (auto &area : Areas)
		area.clear();
	if (!HasEnhancements())
		return;
	const std::string filename = ModeTablePath(TableFile);
	const std::string_view Filename = filename;
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		// The areas keep the look they had before the table.
		LogError("Couldn't load the atmosphere table: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string area;
		std::string name;
		std::string first;
		std::string last;
		reader.readString("area", area);
		reader.readString("knob", name);
		reader.readString("first", first);
		reader.readTrailingString("last", last);
		const int index = AreaIndex(area);
		Row row;
		row.count = ParseNumbers(first, row.first);
		if (index < 0 || name.empty() || row.count == 0) {
			LogError("Bad atmosphere row in {}: {} {} {}", Filename, area, name, first);
			continue;
		}
		// No last floor's value: the same on every floor.
		row.last = row.first;
		if (!last.empty() && ParseNumbers(last, row.last) == 0)
			row.last = row.first;
		Areas[static_cast<size_t>(index)][name] = row;
	}
}

const Row *FindIn(int area, std::string_view name)
{
	if (!Loaded)
		Load();
	if (area < 0)
		return nullptr;
	const auto &rows = Areas[static_cast<size_t>(area)];
	// Looked up by the name as given: the drawing asks for dozens of numbers a frame, and a std::string made for each
	// is a trip to the heap for every name longer than fifteen letters.
	const auto it = rows.find(name);
	return it != rows.end() ? &it->second : nullptr;
}

const Row *Find(std::string_view name)
{
	return FindIn(CurrentArea(), name);
}

float OptionScale()
{
	if (!HasEnhancements())
		return 0;
	const GraphicsOptions &graphics = GetOptions().Graphics;
	// Everything here works on the HD world's own light.
	if (!*graphics.hdGraphics || !*graphics.hdLighting)
		return 0;
	switch (*graphics.atmosphere) {
	case Level::Off: return 0;
	case Level::Subtle: return 0.5F;
	case Level::Full: return 1;
	}
	return 0;
}

std::string Number(float value)
{
	std::string text = std::format("{:.2f}", value);
	while (!text.empty() && text.back() == '0')
		text.pop_back();
	if (!text.empty() && text.back() == '.')
		text.pop_back();
	return text == "-0" ? "0" : text;
}

std::string_view AreaName()
{
	switch (CurrentArea()) {
	case 0: return "Tristram";
	case 1: return "Cathedral";
	case 2: return "Catacombs";
	case 3: return "Caves";
	case 4: return "Hell";
	case 5: return "Hive";
	case 6: return "Crypt";
	default: return "nowhere";
	}
}

std::string Listing()
{
	std::string text;
	const char *level = "Off";
	switch (*GetOptions().Graphics.atmosphere) {
	case Level::Off: level = "Off"; break;
	case Level::Subtle: level = "Subtle"; break;
	case Level::Full: level = "Full"; break;
	}
	text = StrCat("Atmosphere: ", level, " (Settings > Graphics).");
	if (OptionScale() <= 0)
		text += " Nothing shows: it needs Atmosphere on, with HD Graphics and HD Lighting, zoomed in.";
	text += StrCat("\nHere: ", AreaName());
	if (leveltype != DTYPE_TOWN)
		text += StrCat(", floor ", AreaFloor(), ", depth ", Number(Depth()), " (0 first floor of the area, 1 last)");
	text += '\n';
	for (size_t i = 0; i < NumKnobs; i++)
		text += StrCat(Knobs[i].name, " ", Number(Settings[i]), (Settings[i] != Knobs[i].standard ? " (changed)" : ""), ": ", Knobs[i].what, "\n");
	text += "feel(\"flicker\", 0) turns one off, 1 is normal, 2 is double. feel(\"reset\") puts them all back, feel(\"reload\") reads the table again.";
	return text;
}

} // namespace

float Strength(Knob knob)
{
	const float scale = OptionScale();
	if (scale <= 0)
		return 0;
	return scale * Settings[static_cast<size_t>(knob)];
}

float Setting(Knob knob)
{
	return Settings[static_cast<size_t>(knob)];
}

float Depth()
{
	if (leveltype == DTYPE_TOWN)
		return 0;
	int first = 0;
	switch (leveltype) {
	case DTYPE_CATHEDRAL: first = 1; break;
	case DTYPE_CATACOMBS: first = 5; break;
	case DTYPE_CAVES: first = 9; break;
	case DTYPE_HELL: first = 13; break;
	case DTYPE_NEST: first = 17; break;
	case DTYPE_CRYPT: first = 21; break;
	default: return 0;
	}
	return std::clamp(static_cast<float>(AreaFloor() - first) / 3.F, 0.F, 1.F);
}

float Value(std::string_view name, float fallback, float depth)
{
	const Row *row = Find(name);
	if (row == nullptr)
		return fallback;
	return row->first[0] + (row->last[0] - row->first[0]) * depth;
}

float Value(std::string_view name, float fallback)
{
	return Value(name, fallback, Depth());
}

std::array<float, 3> Colour(std::string_view name, const std::array<float, 3> &fallback, float depth)
{
	const Row *row = Find(name);
	if (row == nullptr)
		return fallback;
	std::array<float, 3> colour;
	for (size_t c = 0; c < 3; c++)
		colour[c] = row->first[c] + (row->last[c] - row->first[c]) * depth;
	return colour;
}

std::array<float, 3> Colour(std::string_view name, const std::array<float, 3> &fallback)
{
	return Colour(name, fallback, Depth());
}

float RoomValue(std::string_view room, std::string_view name, float fallback)
{
	const Row *row = FindIn(AreaIndex(room), name);
	return row != nullptr ? row->first[0] : fallback;
}

std::array<float, 3> RoomColour(std::string_view room, std::string_view name, const std::array<float, 3> &fallback)
{
	const Row *row = FindIn(AreaIndex(room), name);
	return row != nullptr ? row->first : fallback;
}

bool Has(std::string_view name)
{
	return Find(name) != nullptr;
}

uint32_t Version()
{
	uint32_t hash = 2166136261U;
	const auto mix = [&hash](uint32_t part) { hash = (hash ^ part) * 16777619U; };
	mix(LoadCount);
	mix(SettingsVersion);
	mix(static_cast<uint32_t>(OptionScale() * 4));
	return hash;
}

std::string Feel(const std::optional<std::string> &name, const std::optional<double> &value)
{
	if (!name)
		return Listing();
	if (*name == "reset") {
		for (size_t i = 0; i < NumKnobs; i++)
			Settings[i] = Knobs[i].standard;
		SettingsVersion++;
		return StrCat("Every switch is back to normal.\n", Listing());
	}
	if (*name == "reload") {
		Load();
		return StrCat("Read the table again (", ModeTablePath(TableFile), ").\n", Listing());
	}
	for (size_t i = 0; i < NumKnobs; i++) {
		if (Knobs[i].name != *name)
			continue;
		if (!value)
			return StrCat(Knobs[i].name, " is ", Number(Settings[i]), ": ", Knobs[i].what, ". Say feel(\"", Knobs[i].name, "\", 0) to turn it off, 1 for normal, 2 for double.");
		Settings[i] = std::clamp(static_cast<float>(*value), 0.F, 3.F);
		SettingsVersion++;
		return StrCat(Knobs[i].name, " is now ", Number(Settings[i]), ": ", Knobs[i].what, ".");
	}
	std::string names;
	for (const KnobInfo &knob : Knobs)
		names += StrCat(names.empty() ? "" : ", ", knob.name);
	return StrCat("No switch called \"", *name, "\". The switches are ", names, ", plus \"reset\" and \"reload\".");
}

std::optional<WorldTileRectangle> ButcherRoom()
{
	if (leveltype != DTYPE_CATHEDRAL || setlevel || !Quests[Q_BUTCHER].IsAvailable() || SetPiece.size.width == 0)
		return std::nullopt;
	return WorldTileRectangle { SetPiece.position.megaToWorld(), WorldTileSize(SetPiece.size.width * 2, SetPiece.size.height * 2) };
}

} // namespace devilution::atmosphere
