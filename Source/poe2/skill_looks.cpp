#include "poe2/skill_looks.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <optional>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "effects.h"
#include "poe2/telemetry.hpp"
#include "tables/spelldat.h"
#include "utils/file_util.h"
#include "utils/log.hpp"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

// Only this file names WeaponSkill and PersistentSkill values; both run past magic_enum's default range of 0 to 127.
template <>
struct magic_enum::customize::enum_range<devilution::WeaponSkill> {
	static constexpr int min = 0;
	static constexpr int max = 255;
};
template <>
struct magic_enum::customize::enum_range<devilution::PersistentSkill> {
	static constexpr int min = 0;
	static constexpr int max = 255;
};

namespace devilution {

namespace {

constexpr std::string_view ShippedFile = "txtdata\\poe2\\skill_looks.tsv";
constexpr std::string_view EditsFileName = "skill_looks_mine.tsv";
constexpr std::string_view Header = "key\tname\tsize\tamount\tlength\tbrightness\tspeed\tshake\ttint\tcolour\tsound\n";

const SkillLook Plain {};

const SkillLookSound Sounds[] = {
	{ "", -1 },
	{ "Swing", static_cast<int16_t>(SfxID::Swing) },
	{ "Swing2", static_cast<int16_t>(SfxID::Swing2) },
	{ "CastSkill", static_cast<int16_t>(SfxID::CastSkill) },
	{ "CastFire", static_cast<int16_t>(SfxID::CastFire) },
	{ "CastLightning", static_cast<int16_t>(SfxID::CastLightning) },
	{ "SpellFireHit", static_cast<int16_t>(SfxID::SpellFireHit) },
	{ "SpellLightningHit", static_cast<int16_t>(SfxID::SpellLightningHit) },
	{ "SpellFlameWave", static_cast<int16_t>(SfxID::SpellFlameWave) },
	{ "SpellInferno", static_cast<int16_t>(SfxID::SpellInferno) },
	{ "SpellNova", static_cast<int16_t>(SfxID::SpellNova) },
	{ "SpellHolyBolt", static_cast<int16_t>(SfxID::SpellHolyBolt) },
	{ "SpellBloodStarHit", static_cast<int16_t>(SfxID::SpellBloodStarHit) },
	{ "SpellBoneSpiritHit", static_cast<int16_t>(SfxID::SpellBoneSpiritHit) },
	{ "SpellStoneCurse", static_cast<int16_t>(SfxID::SpellStoneCurse) },
	{ "SpellElemental", static_cast<int16_t>(SfxID::SpellElemental) },
	{ "SpellDoomSerpents", static_cast<int16_t>(SfxID::SpellDoomSerpents) },
	{ "SpellApocalypse", static_cast<int16_t>(SfxID::SpellApocalypse) },
	{ "SpellGolem", static_cast<int16_t>(SfxID::SpellGolem) },
	{ "ShootBow2", static_cast<int16_t>(SfxID::ShootBow2) },
	{ "BarrelExpload", static_cast<int16_t>(SfxID::BarrelExpload) },
	{ "BigExplosion", static_cast<int16_t>(SfxID::BigExplosion) },
};

uint8_t ParseSound(std::string_view name)
{
	for (size_t i = 1; i < std::size(Sounds); i++) {
		if (Sounds[i].name == name)
			return static_cast<uint8_t>(i);
	}
	return 0;
}

std::map<std::string, SkillLook, std::less<>> Shipped;
struct Edit {
	std::string name;
	SkillLook look;
};
std::map<std::string, Edit, std::less<>> Edits;
bool EditsOn = true;
bool Loaded;

const SkillLook *Current;

float ParseFloat(std::string_view text, float fallback)
{
	if (text.empty())
		return fallback;
	const std::string copy(text);
	char *end = nullptr;
	const float value = std::strtof(copy.c_str(), &end);
	return end == copy.c_str() ? fallback : value;
}

void ParseColour(std::string_view text, SkillLook &look)
{
	if (text.size() != 6)
		return;
	const unsigned long value = std::strtoul(std::string(text).c_str(), nullptr, 16);
	look.r = static_cast<uint8_t>((value >> 16) & 0xFF);
	look.g = static_cast<uint8_t>((value >> 8) & 0xFF);
	look.b = static_cast<uint8_t>(value & 0xFF);
}

std::string ColourText(const SkillLook &look)
{
	char text[8];
	std::snprintf(text, sizeof(text), "%02x%02x%02x", look.r, look.g, look.b);
	return text;
}

std::string FloatText(float value)
{
	char text[16];
	std::snprintf(text, sizeof(text), "%.2f", value);
	return text;
}

/** @brief Reads one row: key, name and the numbers, a blank number keeping the plain look's. */
std::optional<std::pair<std::string, Edit>> ParseRow(const std::vector<std::string_view> &columns)
{
	if (columns.size() < 2 || columns[0].empty())
		return std::nullopt;
	auto column = [&columns](size_t i) { return i < columns.size() ? columns[i] : std::string_view {}; };
	Edit edit;
	edit.name = std::string(column(1));
	SkillLook &look = edit.look;
	look.size = ParseFloat(column(2), 1);
	look.amount = ParseFloat(column(3), 1);
	look.length = ParseFloat(column(4), 1);
	look.brightness = ParseFloat(column(5), 1);
	look.speed = std::max(ParseFloat(column(6), 1), 0.1F);
	look.shake = ParseFloat(column(7), 1);
	look.tint = std::clamp(ParseFloat(column(8), 0), 0.F, 1.F);
	ParseColour(column(9), look);
	look.sound = ParseSound(column(10));
	return std::pair { std::string(columns[0]), std::move(edit) };
}

std::vector<std::string_view> SplitTabs(std::string_view line)
{
	std::vector<std::string_view> columns;
	size_t start = 0;
	while (true) {
		const size_t end = line.find('\t', start);
		columns.push_back(line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}
	return columns;
}

void LoadShipped()
{
	Shipped.clear();
	auto dataFile = DataFile::load(ShippedFile);
	if (!dataFile.has_value())
		return;
	// Just the header is an empty table, not a broken file.
	if (!dataFile->skipHeader().has_value())
		return;
	for (DataFileRecord record : *dataFile) {
		std::vector<std::string_view> columns;
		for (DataFileField field : record)
			columns.push_back(field.value());
		if (auto row = ParseRow(columns))
			Shipped[row->first] = row->second.look;
	}
}

std::string EditsPath()
{
	std::string folder = TelemetryFolder();
	if (folder.empty())
		folder = StrCat(paths::PrefPath(), "poe2_logs", DIRECTORY_SEPARATOR_STR);
	return StrCat(folder, EditsFileName);
}

void LoadEdits()
{
	Edits.clear();
	FILE *file = OpenFile(EditsPath().c_str(), "rb");
	if (file == nullptr)
		return;
	std::string text;
	char buffer[4096];
	size_t read;
	while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		text.append(buffer, read);
	std::fclose(file);
	size_t start = text.find('\n'); // Skip the header.
	while (start != std::string::npos && start + 1 < text.size()) {
		const size_t end = text.find('\n', start + 1);
		std::string_view line = std::string_view(text).substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		if (auto row = ParseRow(SplitTabs(line)))
			Edits[row->first] = std::move(row->second);
		start = end;
	}
}

void EnsureLoaded()
{
	if (!Loaded)
		LoadSkillLooks();
}

void Enter(const SkillLook *&previous, SkillSlot skill)
{
	previous = Current;
	Current = nullptr;
	if (skill.isEmpty())
		return;
	const SkillLook &look = GetSkillLook(SkillLookKey(skill));
	// A plain look changes nothing, so the effects needn't look at it.
	if (!look.isPlain())
		Current = &look;
}

} // namespace

std::span<const SkillLookSound> SkillLookSounds()
{
	return Sounds;
}

void PlaySkillLookSound(SkillSlot skill, Point tile)
{
	if (skill.isEmpty())
		return;
	const SkillLook &look = GetSkillLook(SkillLookKey(skill));
	if (look.sound == 0 || look.sound >= std::size(Sounds))
		return;
	PlaySfxLoc(static_cast<SfxID>(Sounds[look.sound].sfx), tile);
}

bool SkillLook::isPlain() const
{
	return *this == SkillLook {};
}

std::string SkillLookKey(SkillSlot skill)
{
	switch (skill.kind) {
	case SkillKind::Attack:
		return std::string(magic_enum::enum_name(skill.attack()));
	case SkillKind::Spell:
		return StrCat("Spell ", magic_enum::enum_name(skill.spell()));
	case SkillKind::Persistent:
		return StrCat("Spirit ", magic_enum::enum_name(skill.persistent()));
	case SkillKind::Default:
		return "Default";
	default:
		return {};
	}
}

SkillSlot SkillSlotFromEnumName(std::string_view name)
{
	if (const std::optional<WeaponSkill> attack = magic_enum::enum_cast<WeaponSkill>(name); attack && *attack != WeaponSkill::None)
		return SkillSlot::ForAttack(*attack);
	if (const std::optional<PersistentSkill> persistent = magic_enum::enum_cast<PersistentSkill>(name); persistent && *persistent != PersistentSkill::None)
		return SkillSlot::ForPersistent(*persistent);
	return {};
}

void LoadSkillLooks()
{
	Loaded = true;
	LoadShipped();
	LoadEdits();
}

const SkillLook &GetShippedSkillLook(std::string_view key)
{
	EnsureLoaded();
	const auto it = Shipped.find(key);
	return it == Shipped.end() ? Plain : it->second;
}

const SkillLook &GetSkillLook(std::string_view key)
{
	EnsureLoaded();
	if (EditsOn) {
		const auto it = Edits.find(key);
		if (it != Edits.end())
			return it->second.look;
	}
	return GetShippedSkillLook(key);
}

bool HasSkillLookEdit(std::string_view key)
{
	EnsureLoaded();
	return Edits.find(key) != Edits.end();
}

void SetSkillLookEdit(std::string_view key, std::string_view name, const SkillLook &look)
{
	EnsureLoaded();
	if (key.empty())
		return;
	// A scope may be pointing at the old look; keep the node and change it in place.
	Edit &edit = Edits[std::string(key)];
	edit.name = std::string(name);
	edit.look = look;
}

void ClearSkillLookEdit(std::string_view key)
{
	EnsureLoaded();
	const auto it = Edits.find(key);
	if (it == Edits.end())
		return;
	if (Current == &it->second.look)
		Current = nullptr;
	Edits.erase(it);
}

bool SaveSkillLookEdits()
{
	const std::string path = EditsPath();
	FILE *file = OpenFile(path.c_str(), "wb");
	if (file == nullptr) {
		LogError("Couldn't write {}", path);
		return false;
	}
	std::fputs(std::string(Header).c_str(), file);
	for (const auto &[key, edit] : Edits) {
		const SkillLook &look = edit.look;
		const std::string line = StrCat(key, "\t", edit.name, "\t", FloatText(look.size), "\t", FloatText(look.amount), "\t", FloatText(look.length), "\t",
		    FloatText(look.brightness), "\t", FloatText(look.speed), "\t", FloatText(look.shake), "\t", FloatText(look.tint), "\t", ColourText(look), "\t",
		    Sounds[look.sound < std::size(Sounds) ? look.sound : 0].name, "\n");
		std::fputs(line.c_str(), file);
	}
	std::fclose(file);
	return true;
}

bool SkillLookEditsOn()
{
	return EditsOn;
}

void SetSkillLookEditsOn(bool on)
{
	EditsOn = on;
}

const SkillLook *CurrentSkillLook()
{
	return Current;
}

SkillLookScope::SkillLookScope(SkillSlot skill)
{
	Enter(previous_, skill);
}

SkillLookScope::SkillLookScope(WeaponSkill skill)
{
	Enter(previous_, skill == WeaponSkill::None ? SkillSlot {} : SkillSlot::ForAttack(skill));
}

SkillLookScope::SkillLookScope(uint8_t skillKind, uint8_t skillId)
{
	Enter(previous_, SkillSlot { static_cast<SkillKind>(skillKind), skillId });
}

SkillLookScope::~SkillLookScope()
{
	Current = previous_;
}

} // namespace devilution
