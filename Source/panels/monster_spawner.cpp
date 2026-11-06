#include "panels/monster_spawner.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_timer.h>
#else
#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#include "DiabloUI/text_input.hpp"
#include "control/control.hpp"
#include "crawl.hpp"
#include "dead.h"
#include "engine/assets.hpp"
#include "engine/load_cl2.hpp"
#include "engine/load_file.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "lighting.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/hd.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** Maps from monster animation to its file's letter (monster.cpp's Animletter). */
constexpr char AnimLetters[] = "nwahds";
constexpr uint32_t MsPerGameTick = 50;

constexpr int RowHeight = 18;
constexpr int ChipHeight = 20;
constexpr int Gap = 8;
constexpr int CompassButton = 32;
constexpr int SideColumnWidth = 3 * CompassButton + 8;
constexpr int AnimRowHeight = 22;

constexpr uint8_t BorderColor = PAL16_YELLOW + 8;
constexpr uint8_t FieldColor = PAL16_GRAY + 14;
constexpr uint8_t ChipColor = PAL16_GRAY + 12;
constexpr uint8_t ChipActiveColor = PAL16_YELLOW + 11;
constexpr uint8_t RowHoverColor = PAL16_GRAY + 13;
constexpr uint8_t RowSelectedColor = PAL16_YELLOW + 13;
/** @brief The preview's background: near black, as the dungeon is around a monster. */
constexpr uint8_t PreviewColor = PAL16_GRAY + 14;
constexpr uint8_t ArrowColor = PAL16_YELLOW + 2;
constexpr uint8_t ArrowActiveColor = PAL16_GRAY;

constexpr std::array<MonsterGraphic, 6> Anims = { MonsterGraphic::Stand, MonsterGraphic::Walk, MonsterGraphic::Attack, MonsterGraphic::GotHit, MonsterGraphic::Death, MonsterGraphic::Special };

/** @brief The compass buttons, row by row: north-west at the top left (the screen's up is the dungeon's north). */
constexpr std::array<Direction, 8> CompassDirections = {
	Direction::NorthWest, Direction::North, Direction::NorthEast,
	Direction::West, Direction::East,
	Direction::SouthWest, Direction::South, Direction::SouthEast
};
constexpr std::array<Point, 8> CompassCells = {
	Point { 0, 0 }, Point { 1, 0 }, Point { 2, 0 },
	Point { 0, 1 }, Point { 2, 1 },
	Point { 0, 2 }, Point { 1, 2 }, Point { 2, 2 }
};

std::vector<MonsterSpawnerEntry> List;
std::vector<size_t> Results;
std::string LastQuery;
MonsterSpawnerFilter Filter = MonsterSpawnerFilter::All;
MonsterSpawnerFilter LastFilter = MonsterSpawnerFilter::Count;
size_t Selected = 0;
size_t ScrollTop = 0;
MonsterGraphic Anim = MonsterGraphic::Stand;
Direction Facing = Direction::South;
uint32_t AnimStart = 0;
/** @brief The list row last clicked and when, so a second click on it spawns (double-click). */
uint32_t LastRowClickTicks = 0;
constexpr uint32_t DoubleClickTicks = 450;

char QueryBuffer[64];
TextInputCursorState QueryCursor;
TextInputState QueryInput {
	TextInputState::Options {
	    .value = QueryBuffer,
	    .cursor = &QueryCursor,
	    .maxLength = sizeof(QueryBuffer) - 1,
	}
};

/** @brief The previewed monster's pictures, loaded apart from the level's own. */
CMonster PreviewType;
_monster_id PreviewMonster = MT_INVALID;
bool PreviewLoaded = false;
/** @brief A unique monster's colours. */
std::array<uint8_t, 256> PreviewTrn;
int PreviewUnique = -1;
bool HavePreviewTrn = false;

struct Layout {
	Rectangle search;
	std::array<Rectangle, static_cast<size_t>(MonsterSpawnerFilter::Count)> filters;
	Rectangle list;
	Rectangle preview;
	std::array<Rectangle, Anims.size()> anims;
	std::array<Rectangle, CompassDirections.size()> compass;
	Rectangle spawn;

	[[nodiscard]] size_t visibleRows() const
	{
		return static_cast<size_t>(std::max(list.size.height / RowHeight, 1));
	}
};

std::string_view AnimName(MonsterGraphic graphic)
{
	switch (graphic) {
	case MonsterGraphic::Stand:
		return _("Idle");
	case MonsterGraphic::Walk:
		return _("Walk");
	case MonsterGraphic::Attack:
		return _("Attack");
	case MonsterGraphic::GotHit:
		return _("Hit");
	case MonsterGraphic::Death:
		return _("Death");
	default:
		return _("Special");
	}
}

/** @param withSpecial The monster has a special animation, so it gets a button. */
Layout ComputeLayout(const Rectangle &area, bool withSpecial)
{
	Layout layout;
	const int x = area.position.x;
	int y = area.position.y;
	layout.search = { { x, y }, { area.size.width, 24 } };
	y += 24 + Gap;
	int chipX = x;
	for (size_t i = 0; i < layout.filters.size(); i++) {
		const int width = GetLineWidth(MonsterSpawnerFilterName(static_cast<MonsterSpawnerFilter>(i))) + 16;
		layout.filters[i] = { { chipX, y }, { width, ChipHeight } };
		chipX += width + 4;
	}
	y += ChipHeight + Gap;

	const int bottom = area.position.y + area.size.height;
	const int height = std::max(bottom - y, RowHeight);
	const int listWidth = (area.size.width * 2) / 5;
	layout.list = { { x, y }, { listWidth, height } };

	const int right = area.position.x + area.size.width;
	const int sideX = right - SideColumnWidth;
	const int previewX = x + listWidth + Gap + 4;
	const int previewWidth = std::max(sideX - Gap - previewX, 40);
	layout.preview = { { previewX, y }, { previewWidth, std::max(height - AnimRowHeight - 6, 40) } };

	// The animation buttons run under the preview and the compass.
	const int animY = layout.preview.position.y + layout.preview.size.height + 6;
	const int buttons = withSpecial ? static_cast<int>(Anims.size()) : static_cast<int>(Anims.size()) - 1;
	const int animWidth = (right - previewX - 4 * (buttons - 1)) / buttons;
	for (int i = 0; i < buttons; i++)
		layout.anims[i] = { { previewX + i * (animWidth + 4), animY }, { animWidth, AnimRowHeight } };
	if (!withSpecial)
		layout.anims.back() = {};

	const int compassX = sideX + (SideColumnWidth - 3 * CompassButton) / 2;
	for (size_t i = 0; i < CompassCells.size(); i++) {
		layout.compass[i] = { { compassX + CompassCells[i].x * CompassButton, y + CompassCells[i].y * CompassButton }, { CompassButton - 2, CompassButton - 2 } };
	}
	layout.spawn = { { sideX, y + 3 * CompassButton + Gap }, { SideColumnWidth, 40 } };
	return layout;
}

void RefreshResults()
{
	const std::string_view query = QueryInput.value();
	if (query == LastQuery && Filter == LastFilter)
		return;
	LastQuery = std::string(query);
	LastFilter = Filter;
	Results = FilterMonsterSpawnerList(List, query, Filter);
	Selected = 0;
	ScrollTop = 0;
	AnimStart = SDL_GetTicks();
}

const MonsterSpawnerEntry *SelectedEntry()
{
	return Selected < Results.size() ? &List[Results[Selected]] : nullptr;
}

std::string UniqueColoursPath(const UniqueMonsterData &unique)
{
	return StrCat(R"(monsters\monsters\)", unique.mTrnName, ".trn");
}

/** @brief Whether a unique monster's colours are in the game's data (the shareware data has few). */
bool UniqueColoursExist(const UniqueMonsterData &unique)
{
	return !unique.mTrnName.empty() && FindAsset(UniqueColoursPath(unique)).ok();
}

void FreePreview()
{
	for (AnimStruct &anim : PreviewType.anims) {
		if (anim.sprites)
			hd::ForgetSprites(*anim.sprites);
		anim.sprites = std::nullopt;
	}
	PreviewType.animData = nullptr;
	PreviewMonster = MT_INVALID;
	PreviewLoaded = false;
	PreviewUnique = -1;
	HavePreviewTrn = false;
}

/** @brief Loads the pictures of the selected monster for the preview, when it changed. */
void LoadPreview(const MonsterSpawnerEntry &entry)
{
	if (entry.type != PreviewMonster) {
		FreePreview();
		PreviewMonster = entry.type;
		PreviewType.type = entry.type;
		const MonsterData &data = MonstersData[entry.type];
		// As AddMonsterType sets up a type.
		for (size_t i = 0; i < Anims.size(); i++) {
			AnimStruct &anim = PreviewType.anims[i];
			anim.frames = data.frames[i];
			anim.rate = data.hasAnim(i) ? data.rate[i] : 0;
			anim.width = data.hasAnim(i) ? data.width : 0;
		}
		if (!HeadlessMode && MonsterSpritesExist(data))
			PreviewLoaded = InitMonsterGFX(PreviewType).has_value();
	}
	if (entry.unique != PreviewUnique) {
		PreviewUnique = entry.unique;
		HavePreviewTrn = false;
		if (entry.unique >= 0) {
			const std::string path = UniqueColoursPath(UniqueMonstersData[entry.unique]);
			HavePreviewTrn = LoadFileInMemWithStatus(path.c_str(), PreviewTrn).has_value();
		}
	}
}

bool HasAnim(MonsterGraphic graphic)
{
	if (!PreviewLoaded || SelectedEntry() == nullptr)
		return false;
	const AnimStruct &anim = PreviewType.getAnimData(graphic);
	return anim.sprites && anim.frames > 0;
}

/** @brief The animation the preview plays: the one picked, or standing (walking for a monster that never stands) when it has none. */
MonsterGraphic ShownAnim()
{
	if (HasAnim(Anim))
		return Anim;
	return HasAnim(MonsterGraphic::Stand) ? MonsterGraphic::Stand : MonsterGraphic::Walk;
}

void SetAnim(MonsterGraphic graphic)
{
	Anim = graphic;
	AnimStart = SDL_GetTicks();
}

void Turn(int steps)
{
	Facing = TurnDirection(Facing, steps);
}

void KeepSelectionVisible(const Layout &layout)
{
	const size_t rows = layout.visibleRows();
	if (Selected < ScrollTop)
		ScrollTop = Selected;
	else if (Selected >= ScrollTop + rows)
		ScrollTop = Selected - rows + 1;
}

void Select(size_t index, const Layout &layout)
{
	if (Results.empty())
		return;
	Selected = std::min(index, Results.size() - 1);
	AnimStart = SDL_GetTicks();
	KeepSelectionVisible(layout);
}

void Scroll(int rows, const Layout &layout)
{
	const int maxTop = std::max(static_cast<int>(Results.size()) - static_cast<int>(layout.visibleRows()), 0);
	ScrollTop = static_cast<size_t>(std::clamp(static_cast<int>(ScrollTop) + rows, 0, maxTop));
}

/** @return An error to show, or nothing when the monster was spawned. */
std::optional<std::string> SpawnSelected()
{
	const MonsterSpawnerEntry *entry = SelectedEntry();
	if (entry == nullptr || MyPlayer == nullptr)
		return std::string(_("Pick a monster."));
	if (leveltype == DTYPE_TOWN)
		return std::string(_("Monsters can't be spawned in town."));
	if (ActiveMonsterCount >= MaxEnemyMonsters())
		return std::string(_("Too many monsters here."));
	if (!MonsterSpritesExist(MonstersData[entry->type]))
		return std::string(_("This monster has no pictures."));
	if (entry->unique >= 0 && !UniqueColoursExist(UniqueMonstersData[entry->unique]))
		return std::string(_("This monster has no pictures."));
	// A golem is a hero's minion, and Diablo's death ends the game: they're for the preview only.
	if (IsAnyOf(entry->type, MT_GOLEM, MT_DIABLO))
		return std::string(_("Preview only."));

	size_t typeIndex = LevelMonsterTypeCount;
	for (size_t i = 0; i < LevelMonsterTypeCount; i++) {
		if (LevelMonsterTypes[i].type == entry->type) {
			typeIndex = i;
			break;
		}
	}
	if (typeIndex == LevelMonsterTypeCount) {
		if (LevelMonsterTypeCount >= MaxLvlMTypes)
			return std::string(_("Too many kinds of monster here."));
		std::expected<size_t, std::string> added = AddMonsterType(entry->type, PLACE_SCATTER);
		if (!added.has_value())
			return std::move(added).error();
		typeIndex = *added;
	}
	CMonster &monsterType = LevelMonsterTypes[typeIndex];
	// A type new to the level needs its pictures before it's drawn, and a corpse picture for when it dies.
	if (!HeadlessMode && monsterType.animData == nullptr) {
		std::expected<void, std::string> loaded = InitMonsterGFX(monsterType);
		if (!loaded.has_value())
			return std::move(loaded).error();
	}
	if (monsterType.corpseId == 0) {
		monsterType.corpseId = AddCorpseKind(monsterType, 0);
		if (monsterType.corpseId == 0)
			monsterType.corpseId = stonendx;
	}

	const Player &player = *MyPlayer;
	const std::optional<Point> spot = Crawl(1, MaxCrawlRadius, [&player](Displacement displacement) -> std::optional<Point> {
		const Point position = player.position.tile + displacement;
		if (!InDungeonBounds(position) || dPlayer[position.x][position.y] != 0 || dMonster[position.x][position.y] != 0)
			return std::nullopt;
		if (!IsTileWalkable(position))
			return std::nullopt;
		return position;
	});
	if (!spot)
		return std::string(_("No room next to you."));

	if (entry->unique < 0) {
		const size_t before = ActiveMonsterCount;
		SpawnMonster(*spot, Facing, typeIndex);
		if (ActiveMonsterCount == before)
			return std::string(_("Couldn't spawn it here."));
		return std::nullopt;
	}

	Monster *monster = AddMonster(*spot, Facing, typeIndex, true);
	if (monster == nullptr)
		return std::string(_("Too many monsters here."));
	const auto uniqueType = static_cast<UniqueMonsterType>(entry->unique);
	std::expected<void, std::string> prepared = PrepareUniqueMonst(*monster, uniqueType, 0, 0, UniqueMonstersData[entry->unique]);
	if (!prepared.has_value())
		return std::move(prepared).error();
	const auto monsterIndex = static_cast<uint8_t>(monster - Monsters);
	monster->corpseId = AddCorpseKind(monsterType, static_cast<uint8_t>(monsterIndex + 1));
	if (monster->corpseId == 0)
		monster->corpseId = stonendx;
	return std::nullopt;
}

bool TrySpawn()
{
	const MonsterSpawnerEntry *entry = SelectedEntry();
	if (std::optional<std::string> error = SpawnSelected()) {
		EventPlrMsg(*error, UiFlags::ColorRed);
		return false;
	}
	EventPlrMsg(FormatRuntime(_("Spawned {:s}"), entry->name));
	return true;
}

void FillRectangle(const Surface &out, const Rectangle &rect, uint8_t color)
{
	FillRect(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height, color);
}

void DrawFrame(const Surface &out, const Rectangle &rect, uint8_t color)
{
	DrawHorizontalLine(out, rect.position, rect.size.width, color);
	DrawHorizontalLine(out, rect.position + Displacement { 0, rect.size.height - 1 }, rect.size.width, color);
	DrawVerticalLine(out, rect.position, rect.size.height, color);
	DrawVerticalLine(out, rect.position + Displacement { rect.size.width - 1, 0 }, rect.size.height, color);
}

void DrawChip(const Surface &out, const Rectangle &rect, std::string_view text, bool active, bool hovered, bool enabled = true)
{
	FillRectangle(out, rect, active ? ChipActiveColor : (hovered && enabled ? RowHoverColor : ChipColor));
	const UiFlags color = !enabled ? UiFlags::ColorButtonpushed : (active ? UiFlags::ColorWhitegold : UiFlags::ColorWhite);
	DrawString(out, text, rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | color });
}

/** @brief A line two pixels thick. */
void DrawThickLine(const Surface &out, Point from, Point to, uint8_t color)
{
	const int dx = std::abs(to.x - from.x);
	const int dy = -std::abs(to.y - from.y);
	const int sx = from.x < to.x ? 1 : -1;
	const int sy = from.y < to.y ? 1 : -1;
	int error = dx + dy;
	Point p = from;
	while (true) {
		out.SetPixel(p, color);
		out.SetPixel(p + Displacement { 1, 0 }, color);
		out.SetPixel(p + Displacement { 0, 1 }, color);
		out.SetPixel(p + Displacement { 1, 1 }, color);
		if (p == to)
			break;
		const int twice = 2 * error;
		if (twice >= dy) {
			error += dy;
			p.x += sx;
		}
		if (twice <= dx) {
			error += dx;
			p.y += sy;
		}
	}
}

/** @brief The way a facing points on screen, in eighths of a turn: x right, y down. */
Displacement ScreenStep(Direction direction)
{
	switch (direction) {
	case Direction::South: return { 0, 1 };
	case Direction::SouthWest: return { -1, 1 };
	case Direction::West: return { -1, 0 };
	case Direction::NorthWest: return { -1, -1 };
	case Direction::North: return { 0, -1 };
	case Direction::NorthEast: return { 1, -1 };
	case Direction::East: return { 1, 0 };
	default: return { 1, 1 };
	}
}

void DrawArrow(const Surface &out, const Rectangle &rect, Direction direction, uint8_t color)
{
	const Point centre = rect.position + Displacement { rect.size.width / 2 - 1, rect.size.height / 2 - 1 };
	const Displacement step = ScreenStep(direction);
	const bool diagonal = step.deltaX != 0 && step.deltaY != 0;
	const int reach = diagonal ? 7 : 9;
	const Point tip = centre + Displacement { step.deltaX * reach, step.deltaY * reach };
	const Point tail = centre - Displacement { step.deltaX * reach, step.deltaY * reach };
	DrawThickLine(out, tail, tip, color);
	// The head: two strokes back from the tip, each an eighth of a turn either side of the shaft.
	constexpr int head = 6;
	const Displacement left = ScreenStep(TurnDirection(direction, 3));
	const Displacement right = ScreenStep(TurnDirection(direction, -3));
	const auto headLength = [&](Displacement d) { return (d.deltaX != 0 && d.deltaY != 0) ? head * 3 / 4 : head; };
	DrawThickLine(out, tip, tip + Displacement { left.deltaX * headLength(left), left.deltaY * headLength(left) }, color);
	DrawThickLine(out, tip, tip + Displacement { right.deltaX * headLength(right), right.deltaY * headLength(right) }, color);
}

void DrawPreview(const Surface &out, const Layout &layout)
{
	const Rectangle &box = layout.preview;
	FillRectangle(out, box, PreviewColor);
	DrawFrame(out, box, BorderColor);
	const MonsterSpawnerEntry *entry = SelectedEntry();
	if (entry == nullptr) {
		DrawString(out, _("No monsters match."), box, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });
		return;
	}
	LoadPreview(*entry);
	const Rectangle title { box.position + Displacement { 6, 4 }, { box.size.width - 12, 18 } };
	DrawString(out, entry->name, title, { .flags = UiFlags::AlignCenter | (entry->unique >= 0 ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });

	if (!PreviewLoaded) {
		DrawString(out, _("No pictures."), box, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorRed });
		return;
	}
	const MonsterGraphic graphic = ShownAnim();
	const AnimStruct &anim = PreviewType.getAnimData(graphic);
	const OptionalClxSpriteList sprites = anim.spritesForDirection(Facing);
	if (!sprites || sprites->numSprites() == 0)
		return;
	const int frames = std::min<int>(anim.frames, static_cast<int>(sprites->numSprites()));
	const MonsterPreviewFrame shown = MonsterPreviewFrameAt(frames, anim.rate, SDL_GetTicks() - AnimStart, graphic == MonsterGraphic::Death);
	const ClxSprite sprite = (*sprites)[shown.frame];

	// Twice the size when it fits; it stands on the same spot whatever it does.
	const Size spriteSize { sprite.width(), sprite.height() };
	const Size room { box.size.width - 8, box.size.height - 28 };
	const int zoom = (spriteSize.width * 2 <= room.width && spriteSize.height * 2 <= room.height) ? 2 : 1;
	const Size drawn { spriteSize.width * zoom, spriteSize.height * zoom };
	const Point topLeft {
		box.position.x + (box.size.width - drawn.width) / 2,
		box.position.y + box.size.height - 6 - drawn.height,
	};

	// The HD art is twice the game's pixels and stands in for them exactly (unique monsters keep their own colours, as in the game).
	if (!HavePreviewTrn) {
		const Rectangle dest { topLeft + Displacement { out.region.x, out.region.y }, drawn };
		const bool fits = drawn.width <= box.size.width && drawn.height <= box.size.height;
		if (fits && hd::DrawUiSprite(sprite, dest, shown.fraction)) {
			DrawString(out, "HD", { box.position + Displacement { 6, box.size.height - 22 }, { 40, 18 } }, { .flags = UiFlags::ColorGold });
			return;
		}
	}

	const Surface clip = out.subregion(box.position.x + 1, box.position.y + 1, box.size.width - 2, box.size.height - 2);
	const Point inClip = topLeft - Displacement { box.position.x + 1, box.position.y + 1 };
	if (zoom == 1) {
		const Point bottomLeft = inClip + Displacement { 0, spriteSize.height - 1 };
		if (HavePreviewTrn)
			ClxDrawTRN(clip, bottomLeft, sprite, PreviewTrn.data());
		else
			ClxDraw(clip, bottomLeft, sprite);
		return;
	}
	// Drawn twice, on two backgrounds: the pixels that come out the same are the sprite's.
	const Point bottomLeft { 0, spriteSize.height - 1 };
	const auto drawOn = [&](uint8_t background) {
		OwnedSurface frame(spriteSize);
		FillRect(frame, 0, 0, spriteSize.width, spriteSize.height, background);
		if (HavePreviewTrn)
			ClxDrawTRN(frame, bottomLeft, sprite, PreviewTrn.data());
		else
			ClxDraw(frame, bottomLeft, sprite);
		return frame;
	};
	const OwnedSurface first = drawOn(1);
	const OwnedSurface second = drawOn(2);
	for (int y = 0; y < spriteSize.height; y++) {
		for (int x = 0; x < spriteSize.width; x++) {
			const uint8_t color = *first.at(x, y);
			if (color != *second.at(x, y))
				continue;
			const Point at = inClip + Displacement { x * 2, y * 2 };
			clip.SetPixel(at, color);
			clip.SetPixel(at + Displacement { 1, 0 }, color);
			clip.SetPixel(at + Displacement { 0, 1 }, color);
			clip.SetPixel(at + Displacement { 1, 1 }, color);
		}
	}
}

bool HandleClick(Point position, const Layout &layout, bool &requestClose)
{
	if (layout.search.contains(position))
		return true;
	for (size_t i = 0; i < layout.filters.size(); i++) {
		if (layout.filters[i].contains(position)) {
			Filter = static_cast<MonsterSpawnerFilter>(i);
			RefreshResults();
			return true;
		}
	}
	if (layout.list.contains(position)) {
		const size_t row = ScrollTop + static_cast<size_t>((position.y - layout.list.position.y) / RowHeight);
		if (row < Results.size()) {
			const uint32_t now = SDL_GetTicks();
			const bool again = row == Selected && now - LastRowClickTicks <= DoubleClickTicks;
			if (row != Selected)
				Select(row, layout);
			LastRowClickTicks = again ? 0 : now;
			if (again && TrySpawn())
				requestClose = true;
		}
		return true;
	}
	for (size_t i = 0; i < layout.anims.size(); i++) {
		if (layout.anims[i].contains(position)) {
			if (HasAnim(Anims[i]))
				SetAnim(Anims[i]);
			return true;
		}
	}
	for (size_t i = 0; i < layout.compass.size(); i++) {
		if (layout.compass[i].contains(position)) {
			Facing = CompassDirections[i];
			return true;
		}
	}
	if (layout.spawn.contains(position) && TrySpawn())
		requestClose = true;
	return true;
}

} // namespace

std::string_view MonsterSpawnerFilterName(MonsterSpawnerFilter filter)
{
	switch (filter) {
	case MonsterSpawnerFilter::All:
		return _("All");
	case MonsterSpawnerFilter::Normal:
		return _("Normal");
	case MonsterSpawnerFilter::Unique:
		return _("Unique");
	default:
		return {};
	}
}

bool MonsterSpritesExist(const MonsterData &data)
{
	if (data.spriteId >= GetNumMonsterSprites())
		return false;
	const size_t numAnims = data.hasSpecial ? 6 : 5;
	bool any = false;
	for (size_t i = 0; i < numAnims; i++) {
		if (!data.hasAnim(i))
			continue;
		const std::string path = StrCat("monsters\\", data.spritePath(), std::string_view(&AnimLetters[i], 1), DEVILUTIONX_CL2_EXT);
		if (!FindAsset(path).ok())
			return false;
		any = true;
	}
	return any;
}

std::vector<MonsterSpawnerEntry> BuildMonsterSpawnerList(const std::function<bool(const MonsterData &)> &hasSprites)
{
	std::vector<MonsterSpawnerEntry> list;
	const auto folder = [](const MonsterData &data) {
		std::string path = AsciiStrToLower(std::string_view(data.spritePath()));
		const size_t slash = path.find_first_of("\\/");
		return slash != std::string::npos ? path.substr(0, slash) : path;
	};
	std::vector<bool> usable(MonstersData.size(), false);
	for (size_t i = 0; i < MonstersData.size(); i++) {
		const MonsterData &data = MonstersData[i];
		if (data.name.empty() || (gbIsSpawn && data.availability == MonsterAvailability::Retail) || !hasSprites(data))
			continue;
		usable[i] = true;
		list.push_back(MonsterSpawnerEntry {
		    .name = std::string(pgettext("monster", data.name)),
		    .detail = folder(data),
		    .type = static_cast<_monster_id>(i),
		});
	}
	for (size_t i = 0; i < UniqueMonstersData.size(); i++) {
		const UniqueMonsterData &unique = UniqueMonstersData[i];
		const auto type = static_cast<size_t>(unique.mtype);
		if (unique.mName.empty() || unique.mtype < 0 || type >= usable.size() || !usable[type])
			continue;
		list.push_back(MonsterSpawnerEntry {
		    .name = std::string(pgettext("monster", unique.mName)),
		    .detail = folder(MonstersData[type]),
		    .type = unique.mtype,
		    .unique = static_cast<int>(i),
		});
	}
	return list;
}

std::vector<size_t> FilterMonsterSpawnerList(const std::vector<MonsterSpawnerEntry> &list, std::string_view query, MonsterSpawnerFilter filter)
{
	std::vector<std::string> words;
	const std::string lowerQuery = AsciiStrToLower(query);
	size_t start = 0;
	while (start < lowerQuery.size()) {
		const size_t end = std::min(lowerQuery.find(' ', start), lowerQuery.size());
		if (end > start)
			words.emplace_back(lowerQuery.substr(start, end - start));
		start = end + 1;
	}

	std::vector<size_t> results;
	for (size_t i = 0; i < list.size(); i++) {
		const MonsterSpawnerEntry &entry = list[i];
		const bool unique = entry.unique >= 0;
		if ((filter == MonsterSpawnerFilter::Normal && unique) || (filter == MonsterSpawnerFilter::Unique && !unique))
			continue;
		const std::string haystack = AsciiStrToLower(StrCat(entry.name, " ", entry.detail, unique ? " unique" : "", entry.hdArt ? " hd" : ""));
		const bool matches = std::all_of(words.begin(), words.end(), [&haystack](const std::string &word) {
			return haystack.find(word) != std::string::npos;
		});
		if (matches)
			results.push_back(i);
	}
	return results;
}

std::string MonsterArtName(const MonsterData &data, MonsterGraphic graphic, Direction direction, int frame)
{
	const auto anim = static_cast<size_t>(graphic);
	std::string name = StrCat("monsters\\", data.spritePath(), std::string_view(&AnimLetters[anim], 1));
	if (!data.trnFile.empty())
		StrAppend(name, "@", data.trnFile);
	std::replace(name.begin(), name.end(), '\\', '/');
	AsciiStrToLower(name);
	char suffix[32];
	std::snprintf(suffix, sizeof(suffix), "/d%d_f%02d", static_cast<int>(direction), frame);
	name += suffix;
	return name;
}

Direction TurnDirection(Direction direction, int steps)
{
	const int turned = ((static_cast<int>(direction) + steps) % 8 + 8) % 8;
	return static_cast<Direction>(turned);
}

MonsterPreviewFrame MonsterPreviewFrameAt(int frames, int ticksPerFrame, uint32_t elapsedMs, bool holdLast)
{
	frames = std::max(frames, 1);
	const uint32_t frameMs = MsPerGameTick * static_cast<uint32_t>(std::max(ticksPerFrame, 1));
	const uint32_t playMs = frameMs * static_cast<uint32_t>(frames);
	if (holdLast) {
		elapsedMs %= playMs - frameMs + MonsterPreviewDeathHoldMs;
		if (elapsedMs >= playMs - frameMs)
			return { frames - 1, 0.F };
	} else {
		elapsedMs %= playMs;
	}
	return { static_cast<int>(elapsedMs / frameMs), static_cast<float>(elapsedMs % frameMs) / static_cast<float>(frameMs) };
}

void OpenMonsterSpawner()
{
	if (List.empty()) {
		List = BuildMonsterSpawnerList(MonsterSpritesExist);
		// A unique needs its colours too.
		std::erase_if(List, [](const MonsterSpawnerEntry &entry) { return entry.unique >= 0 && !UniqueColoursExist(UniqueMonstersData[entry.unique]); });
		for (MonsterSpawnerEntry &entry : List) {
			// The game shows HD art for normal monsters only: a unique is drawn in its own colours.
			entry.hdArt = entry.unique < 0 && hd::HasArt(MonsterArtName(MonstersData[entry.type], MonsterGraphic::Stand, Direction::South, 0));
		}
	}
	LastFilter = MonsterSpawnerFilter::Count; // Forces a refresh, keeping the pick when the list is the same.
	const size_t keep = Selected;
	const size_t keepTop = ScrollTop;
	const std::string query = LastQuery;
	RefreshResults();
	if (query == LastQuery && keep < Results.size()) {
		Selected = keep;
		ScrollTop = keepTop;
	}
	AnimStart = SDL_GetTicks();
}

void CloseMonsterSpawner()
{
	FreePreview();
}

size_t MonsterSpawnerResultCount()
{
	return Results.size();
}

bool MonsterSpawnerHandleEvent(const SDL_Event &event, const Rectangle &area, bool &requestClose)
{
	const Layout layout = ComputeLayout(area, HasAnim(MonsterGraphic::Special));
	// Left and right turn the monster rather than move the search's cursor.
	if (event.type == SDL_EVENT_KEY_DOWN) {
		switch (SDLC_EventKey(event)) {
		case SDLK_LEFT:
			Turn(-1);
			return true;
		case SDLK_RIGHT:
			Turn(1);
			return true;
		default:
			break;
		}
	}
	if (HandleTextInputEvent(event, QueryInput)) {
		RefreshResults();
		return true;
	}

	const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		switch (SDLC_EventKey(event)) {
		case SDLK_UP:
			Select(Selected > 0 ? Selected - 1 : 0, layout);
			break;
		case SDLK_DOWN:
			Select(Selected + 1, layout);
			break;
		case SDLK_PAGEUP:
			Select(Selected > layout.visibleRows() ? Selected - layout.visibleRows() : 0, layout);
			break;
		case SDLK_PAGEDOWN:
			Select(Selected + layout.visibleRows(), layout);
			break;
		case SDLK_TAB: {
			const int count = static_cast<int>(MonsterSpawnerFilter::Count);
			Filter = static_cast<MonsterSpawnerFilter>((static_cast<int>(Filter) + (shift ? count - 1 : 1)) % count);
			RefreshResults();
			break;
		}
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			if (TrySpawn())
				requestClose = true;
			break;
		default:
			break;
		}
		return true;
	case SDL_EVENT_KEY_UP:
#ifndef USE_SDL1
	case SDL_EVENT_TEXT_INPUT:
#endif
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event.button.button == SDL_BUTTON_LEFT)
			return HandleClick(MousePosition, layout, requestClose);
#ifdef USE_SDL1
		if (event.button.button == SDL_BUTTON_WHEELUP)
			Scroll(-3, layout);
		else if (event.button.button == SDL_BUTTON_WHEELDOWN)
			Scroll(3, layout);
#endif
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return true;
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
		Scroll(SDLC_EventWheelIntY(event) > 0 ? -3 : 3, layout);
		return true;
#endif
	default:
		return false;
	}
}

void DrawMonsterSpawner(const Surface &out, const Rectangle &area)
{
	RefreshResults();
	const Layout layout = ComputeLayout(area, HasAnim(MonsterGraphic::Special));
	KeepSelectionVisible(layout);

	// Search box
	FillRectangle(out, layout.search, FieldColor);
	DrawFrame(out, layout.search, BorderColor);
	const Rectangle searchText { layout.search.position + Displacement { 8, 0 }, { layout.search.size.width - 16, layout.search.size.height } };
	if (QueryInput.empty())
		DrawString(out, _("Search monsters, e.g. \"zombie\", \"hd\""), searchText, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
	DrawString(out, QueryInput.value(), searchText, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite, .cursorPosition = static_cast<int>(QueryCursor.position) });

	for (size_t i = 0; i < layout.filters.size(); i++) {
		const auto filter = static_cast<MonsterSpawnerFilter>(i);
		DrawChip(out, layout.filters[i], MonsterSpawnerFilterName(filter), filter == Filter, layout.filters[i].contains(MousePosition));
	}

	// The list
	FillRectangle(out, layout.list, FieldColor);
	const size_t rows = layout.visibleRows();
	for (size_t row = 0; row < rows && ScrollTop + row < Results.size(); row++) {
		const size_t index = ScrollTop + row;
		const MonsterSpawnerEntry &entry = List[Results[index]];
		const Rectangle rowRect { layout.list.position + Displacement { 0, static_cast<int>(row) * RowHeight }, { layout.list.size.width, RowHeight } };
		if (index == Selected)
			FillRectangle(out, rowRect, RowSelectedColor);
		else if (rowRect.contains(MousePosition))
			FillRectangle(out, rowRect, RowHoverColor);
		const Rectangle textRect { rowRect.position + Displacement { 6, 0 }, { rowRect.size.width - 12, RowHeight } };
		DrawString(out, entry.name, textRect, { .flags = UiFlags::VerticalCenter | (entry.unique >= 0 ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
		if (entry.hdArt)
			DrawString(out, "HD", textRect, { .flags = UiFlags::VerticalCenter | UiFlags::AlignRight | UiFlags::ColorGold });
		else
			DrawString(out, entry.detail, textRect, { .flags = UiFlags::VerticalCenter | UiFlags::AlignRight | UiFlags::ColorButtonface });
	}
	if (Results.size() > rows) {
		const int trackHeight = layout.list.size.height;
		const int thumbHeight = std::max(trackHeight * static_cast<int>(rows) / static_cast<int>(Results.size()), 12);
		const int thumbY = (trackHeight - thumbHeight) * static_cast<int>(ScrollTop) / std::max(static_cast<int>(Results.size() - rows), 1);
		FillRect(out, layout.list.position.x + layout.list.size.width - 3, layout.list.position.y + thumbY, 3, thumbHeight, BorderColor);
	}

	DrawPreview(out, layout);

	const MonsterGraphic shownAnim = ShownAnim();
	for (size_t i = 0; i < Anims.size(); i++) {
		const bool available = HasAnim(Anims[i]);
		// Special only shows for monsters that have one.
		if (Anims[i] == MonsterGraphic::Special && !available)
			continue;
		DrawChip(out, layout.anims[i], AnimName(Anims[i]), available && Anims[i] == shownAnim, layout.anims[i].contains(MousePosition), available);
	}

	for (size_t i = 0; i < layout.compass.size(); i++) {
		const Rectangle &button = layout.compass[i];
		const bool active = CompassDirections[i] == Facing;
		FillRectangle(out, button, active ? ChipActiveColor : (button.contains(MousePosition) ? RowHoverColor : ChipColor));
		DrawArrow(out, button, CompassDirections[i], active ? ArrowActiveColor : ArrowColor);
	}
	// The middle of the compass: a dot.
	const Rectangle &west = layout.compass[3];
	const Point middle = west.position + Displacement { CompassButton + (CompassButton - 2) / 2 - 2, (CompassButton - 2) / 2 - 2 };
	FillRect(out, middle.x, middle.y, 4, 4, ArrowColor);

	const Rectangle &spawn = layout.spawn;
	FillRectangle(out, spawn, spawn.contains(MousePosition) ? ChipActiveColor : ChipColor);
	DrawFrame(out, spawn, BorderColor);
	DrawString(out, _("Spawn"), spawn, { .flags = UiFlags::FontSize24 | UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
}

} // namespace devilution
