#include "poe2/hit_trace.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "game_mode.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/skill_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "tables/misdat.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** Most lines one session writes, so a long fight can't fill the disk. */
constexpr int MaxTraceLines = 4000;
/** Most tile lines one projectile writes in detail mode. */
constexpr int MaxDetailLinesPerMissile = 64;

struct Refusal {
	std::string reason;
	int count;
};

struct Trace {
	MissileID type;
	Point start;
	std::string skill;
	int tiles = 0;
	int detailLines = 0;
	int hits = 0;
	int damage = 0;
	int resisted = 0;
	bool wall = false;
	Point wallTile;
	/** Monsters on a tile the projectile checked. */
	std::vector<unsigned> reached;
	/** Monsters next to the path, with where they stood: those never reached are near misses. */
	std::vector<std::pair<unsigned, Point>> passed;
	std::vector<Refusal> refusals;
};

bool Detail;
int LinesWritten;
const Missile *Current;
ankerl::unordered_dense::map<const Missile *, Trace> Traces;

bool IsTraced(const Missile &missile)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || missile._micaster == TARGET_PLAYERS || missile._misource != static_cast<int>(MyPlayerId))
		return false;
	if (Detail)
		return true;
	// Only skill projectiles that aren't arrows: the spells that have been reported passing through monsters.
	return MissileSkill(missile).kind != SkillKind::None && !GetMissileData(missile._mitype).isArrow();
}

std::string TileText(Point tile)
{
	return StrCat(tile.x, ",", tile.y);
}

std::string MonsterText(const Monster &monster)
{
	return StrCat(monster.name(), " #", monster.getId(), " at ", TileText(monster.position.tile));
}

void Write(std::string_view text)
{
	if (LinesWritten >= MaxTraceLines)
		return;
	LinesWritten++;
	TelemetryLog("hit", LinesWritten == MaxTraceLines ? StrCat(text, " (hit trace full for this session)") : std::string(text));
}

Trace *FindTrace(const Missile &missile)
{
	const auto it = Traces.find(&missile);
	return it != Traces.end() ? &it->second : nullptr;
}

/** @brief The trace for this projectile, starting one when it's new. A removed projectile's memory can be reused, so a different start means a new one. */
Trace *TraceFor(const Missile &missile)
{
	if (!IsTraced(missile))
		return nullptr;
	auto it = Traces.find(&missile);
	if (it != Traces.end() && (it->second.type != missile._mitype || it->second.start != missile.position.start)) {
		Traces.erase(it); // its summary was lost when the level changed
		it = Traces.end();
	}
	if (it == Traces.end()) {
		if (Traces.size() > 512)
			Traces.clear(); // only stale traces from a level change get this far
		Trace trace;
		trace.type = missile._mitype;
		trace.start = missile.position.start;
		const SkillSlot skill = MissileSkill(missile);
		trace.skill = skill.kind != SkillKind::None ? std::string(SkillName(skill)) : StrCat("missile ", static_cast<int>(missile._mitype));
		it = Traces.emplace(&missile, std::move(trace)).first;
	}
	return &it->second;
}

template <typename T>
bool Contains(const std::vector<T> &values, const T &value)
{
	return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

void HitTraceSetDetail(bool detail)
{
	Detail = detail;
}

bool HitTraceDetail()
{
	return Detail;
}

void HitTraceTile(const Missile &missile, Point tile)
{
	Trace *trace = TraceFor(missile);
	if (trace == nullptr || !InDungeonBounds(tile))
		return;
	trace->tiles++;
	const int mid = dMonster[tile.x][tile.y];
	if (mid != 0 && !Contains(trace->reached, static_cast<unsigned>(std::abs(mid) - 1)))
		trace->reached.push_back(static_cast<unsigned>(std::abs(mid) - 1));
	for (int dx = -1; dx <= 1; dx++) {
		for (int dy = -1; dy <= 1; dy++) {
			const Point beside = tile + Displacement { dx, dy };
			if ((dx == 0 && dy == 0) || !InDungeonBounds(beside) || dMonster[beside.x][beside.y] == 0)
				continue;
			const auto id = static_cast<unsigned>(std::abs(dMonster[beside.x][beside.y]) - 1);
			const Monster &monster = Monsters[id];
			if (monster.hasNoLife() || monster.isPlayerMinion())
				continue;
			if (std::none_of(trace->passed.begin(), trace->passed.end(), [&](const auto &entry) { return entry.first == id; }))
				trace->passed.emplace_back(id, monster.position.tile);
		}
	}
	if (Detail && trace->detailLines < MaxDetailLinesPerMissile) {
		trace->detailLines++;
		std::string text = StrCat(trace->skill, " tile ", TileText(tile));
		if (mid != 0)
			StrAppend(text, " monster ", MonsterText(Monsters[std::abs(mid) - 1]), mid < 0 ? " (moving in)" : "");
		if (dPlayer[tile.x][tile.y] != 0)
			StrAppend(text, " player");
		Write(text);
	}
}

void HitTraceWall(const Missile &missile, Point tile)
{
	Trace *trace = FindTrace(missile);
	if (trace == nullptr)
		return;
	trace->wall = true;
	trace->wallTile = tile;
}

void HitTraceEnd(const Missile &missile)
{
	const auto it = Traces.find(&missile);
	if (it == Traces.end())
		return;
	const Trace &trace = it->second;
	std::string text = StrCat(trace.skill, ": from ", TileText(trace.start), " to ", TileText(missile.position.tile), ", ", trace.tiles, " tiles");
	if (trace.hits > 0) {
		StrAppend(text, ", hit ", trace.hits, "x for ", trace.damage / 64, " damage");
		if (trace.resisted > 0)
			StrAppend(text, " (", trace.resisted, " resisted)");
	} else {
		StrAppend(text, ", no hits");
	}
	for (const Refusal &refusal : trace.refusals)
		StrAppend(text, " | refused ", refusal.count, "x: ", refusal.reason);
	int nearMisses = 0;
	for (const auto &[id, tile] : trace.passed) {
		if (Contains(trace.reached, id))
			continue;
		if (nearMisses == 3) {
			StrAppend(text, " ...");
			break;
		}
		StrAppend(text, nearMisses == 0 ? " | passed beside " : ", ", Monsters[id].name(), " #", id, " at ", TileText(tile));
		nearMisses++;
	}
	if (trace.wall)
		StrAppend(text, " | stopped by a wall at ", TileText(trace.wallTile));
	else if (missile._miHitFlag)
		StrAppend(text, " | ended on a hit");
	else
		StrAppend(text, " | ran its full range");
	Write(text);
	Traces.erase(it);
}

HitTraceScope::HitTraceScope(const Missile &missile)
    : previous_(Current)
{
	Current = &missile;
}

HitTraceScope::~HitTraceScope()
{
	Current = previous_;
}

void HitTraceRefused(const Monster &monster, std::string_view reason)
{
	if (Current == nullptr)
		return;
	Trace *trace = TraceFor(*Current);
	if (trace == nullptr)
		return;
	const std::string text = StrCat(reason, " (", monster.name(), ")");
	for (Refusal &refusal : trace->refusals) {
		if (refusal.reason == text) {
			refusal.count++;
			return;
		}
	}
	trace->refusals.push_back({ text, 1 });
	if (Detail)
		Write(StrCat(trace->skill, " refused by ", MonsterText(monster), ": ", reason));
}

void HitTraceHit(const Monster &monster, int damage, bool resisted)
{
	if (Current == nullptr)
		return;
	Trace *trace = TraceFor(*Current);
	if (trace == nullptr)
		return;
	trace->hits++;
	trace->damage += damage;
	if (resisted)
		trace->resisted++;
	if (Detail)
		Write(StrCat(trace->skill, " hit ", MonsterText(monster), " for ", damage / 64, resisted ? " (resisted)" : "", ", life left ", monster.hitPoints.whole()));
}

} // namespace devilution
