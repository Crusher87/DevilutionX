#include "panels/skill_lab_panel.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#else
#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#include "DiabloUI/text_input.hpp"
#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "engine/dx.h"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "poe2/skill_lab.hpp"
#include "tables/playerdat.hpp"
#include "utils/display.h"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr int PanelWidth = 248;
constexpr int Padding = 8;
constexpr int RowHeight = 18;
constexpr int ChipHeight = 20;
constexpr int Gap = 4;

constexpr uint8_t BorderColor = PAL16_YELLOW + 8;
constexpr uint8_t FieldColor = PAL16_GRAY + 14;
constexpr uint8_t ChipColor = PAL16_GRAY + 12;
constexpr uint8_t ChipActiveColor = PAL16_YELLOW + 11;
constexpr uint8_t RowHoverColor = PAL16_GRAY + 13;
constexpr uint8_t RowSelectedColor = PAL16_YELLOW + 13;

char QueryBuffer[48];
TextInputCursorState QueryCursor;
TextInputState QueryInput {
	TextInputState::Options {
	    .value = QueryBuffer,
	    .cursor = &QueryCursor,
	    .maxLength = sizeof(QueryBuffer) - 1,
	}
};
bool Typing;
/** Folded down to a small tab, to see the whole arena. */
bool Hidden;

/** The tab the list shows; Count is every tab. */
GemCategory Category = GemCategory::All;
std::vector<int> Results;
std::string LastQuery;
GemCategory LastCategory = GemCategory::Count;
size_t ScrollTop;

/** A clickable spot on the panel and what it does. */
struct Button {
	Rectangle rect;
	std::string label;
	bool active;
	void (*onClick)(int value);
	int value;
};

struct Layout {
	Rectangle panel;
	Rectangle title;
	Rectangle exit;
	Rectangle hide;
	Rectangle search;
	std::vector<Button> tabs;
	Rectangle list;
	/** The strip along the top holding the controls. */
	Rectangle bar;
	std::vector<Button> controls;
	/** Labels drawn left of a row of controls. */
	std::vector<std::pair<Rectangle, std::string>> labels;

	[[nodiscard]] size_t visibleRows() const
	{
		return static_cast<size_t>(std::max(list.size.height / RowHeight, 1));
	}
};

std::vector<GemCategory> Tabs()
{
	std::vector<GemCategory> tabs = { GemCategory::All };
	for (uint8_t i = 0; i < static_cast<uint8_t>(GemCategory::All); i++) {
		const auto category = static_cast<GemCategory>(i);
		const bool used = std::any_of(LabSkills().begin(), LabSkills().end(), [category](const LabSkill &skill) { return skill.category == category; });
		if (used)
			tabs.push_back(category);
	}
	return tabs;
}

void RefreshResults()
{
	const std::string query = AsciiStrToLower(QueryInput.value());
	if (query == LastQuery && Category == LastCategory)
		return;
	LastQuery = query;
	LastCategory = Category;
	std::vector<std::string> words;
	size_t start = 0;
	while (start < query.size()) {
		const size_t end = std::min(query.find(' ', start), query.size());
		if (end > start)
			words.emplace_back(query.substr(start, end - start));
		start = end + 1;
	}
	Results.clear();
	const std::vector<LabSkill> &skills = LabSkills();
	for (size_t i = 0; i < skills.size(); i++) {
		const LabSkill &skill = skills[i];
		if (Category != GemCategory::All && skill.category != Category && skill.category != GemCategory::All)
			continue;
		const std::string name = AsciiStrToLower(skill.name);
		if (std::all_of(words.begin(), words.end(), [&name](const std::string &word) { return name.find(word) != std::string::npos; }))
			Results.push_back(static_cast<int>(i));
	}
	ScrollTop = 0;
}

std::string_view ClassName(HeroClass heroClass)
{
	return _(GetPlayerDataForClass(heroClass).className);
}

std::string_view SlowdownLabel(int slowdown)
{
	switch (slowdown) {
	case 2:
		return "1/2";
	case 4:
		return "1/4";
	default:
		return "1x";
	}
}

std::string_view ShortFloorName(LabFloor floor)
{
	switch (floor) {
	case LabFloor::Cathedral:
		return _("Cath");
	case LabFloor::Catacombs:
		return _("Cata");
	default:
		return LabFloorName(floor);
	}
}

/** @brief Where the next group of controls goes in the bar along the top. */
struct Flow {
	int left;
	int right;
	int x;
	int y;
};

/** @brief Adds a group of buttons to the bar, from left to right, starting a new line when the group doesn't fit. */
void AddGroup(Layout &layout, Flow &flow, std::string_view label, std::vector<Button> buttons)
{
	const int labelWidth = label.empty() ? 0 : GetLineWidth(label) + 6;
	int width = labelWidth;
	for (const Button &button : buttons)
		width += GetLineWidth(button.label) + 12 + Gap;
	if (flow.x > flow.left && flow.x + width > flow.right) {
		flow.x = flow.left;
		flow.y += ChipHeight + Gap;
	}
	if (!label.empty()) {
		layout.labels.emplace_back(Rectangle { { flow.x, flow.y }, { labelWidth, ChipHeight } }, std::string(label));
		flow.x += labelWidth;
	}
	for (Button &button : buttons) {
		const int buttonWidth = GetLineWidth(button.label) + 12;
		button.rect = { { flow.x, flow.y }, { buttonWidth, ChipHeight } };
		flow.x += buttonWidth + Gap;
		layout.controls.push_back(std::move(button));
	}
	flow.x += 3 * Gap;
}

Layout ComputeLayout(const Surface &out)
{
	Layout layout;
	if (Hidden) {
		layout.panel = { { 4, 4 }, { GetLineWidth(_("Skill Lab")) + 16, ChipHeight } };
		return layout;
	}
	const int height = std::max(GetMainPanel().position.y - 8, 160);
	layout.panel = { { 4, 4 }, { std::min(PanelWidth, out.w() - 8), height } };
	const int innerX = layout.panel.position.x + Padding;
	const int innerWidth = layout.panel.size.width - (2 * Padding);
	int y = layout.panel.position.y + 6;

	layout.title = { { innerX, y }, { innerWidth, 24 } };
	const int exitWidth = GetLineWidth(_("Exit")) + 14;
	layout.exit = { { innerX + innerWidth - exitWidth, y + 2 }, { exitWidth, ChipHeight } };
	const int hideWidth = GetLineWidth(_("Hide")) + 14;
	layout.hide = { { layout.exit.position.x - Gap - hideWidth, y + 2 }, { hideWidth, ChipHeight } };
	y += 28;

	layout.search = { { innerX, y }, { innerWidth, 20 } };
	y += 24;

	int x = innerX;
	for (const GemCategory category : Tabs()) {
		const std::string label = category == GemCategory::All ? std::string(_("All")) : std::string(GemCategoryName(category));
		const int width = GetLineWidth(label) + 10;
		if (x + width > innerX + innerWidth) {
			x = innerX;
			y += ChipHeight + 2;
		}
		layout.tabs.push_back(Button { { { x, y }, { width, ChipHeight } }, label, category == Category, nullptr, static_cast<int>(category) });
		x += width + 2;
	}
	y += ChipHeight + Gap;

	layout.list = { { innerX, y }, { innerWidth, std::max(layout.panel.position.y + layout.panel.size.height - Padding - y, RowHeight) } };

	// The controls go in a bar along the top, right of the list, so the list keeps the panel's height.
	const int barLeft = layout.panel.position.x + layout.panel.size.width + Gap;
	Flow flow { barLeft + Padding, out.w() - 4 - Padding, barLeft + Padding, layout.panel.position.y + 6 };
	AddGroup(layout, flow, {},
	    {
	        Button { {}, std::string(_("Cast")), false, [](int) { LabCast(); }, 0 },
	        Button { {}, std::string(_("Loop")), LabLooping(), [](int) { LabSetLooping(!LabLooping()); }, 0 },
	        Button { {}, std::string(_("Pause")), LabPaused(), [](int) { LabSetPaused(!LabPaused()); }, 0 },
	        Button { {}, std::string(_("Step")), false, [](int) { LabStep(); }, 0 },
	    });
	std::vector<Button> speeds;
	for (const int slowdown : { 1, 2, 4 })
		speeds.push_back(Button { {}, std::string(SlowdownLabel(slowdown)), LabSlowdown() == slowdown, [](int value) { LabSetSlowdown(value); }, slowdown });
	AddGroup(layout, flow, _("Speed"), std::move(speeds));

	std::vector<Button> targets = {
		Button { {}, std::string(_("Monsters")), LabTargetMode() == LabTargets::Monsters, [](int) { LabSetTargetMode(LabTargets::Monsters); }, 0 },
		Button { {}, std::string(_("Floor")), LabTargetMode() == LabTargets::Floor, [](int) { LabSetTargetMode(LabTargets::Floor); }, 0 },
	};
	// The counts stay put in Floor mode too, so the bar doesn't move under the mouse.
	for (const int count : { 1, 5, 12 })
		targets.push_back(Button { {}, StrCat(count), LabTargetMode() == LabTargets::Monsters && LabTargetCount() == count, [](int value) { LabSetTargetMode(LabTargets::Monsters); LabSetTargetCount(value); }, count });
	AddGroup(layout, flow, {}, std::move(targets));

	const std::string monster = LabMonsterName();
	// Keep the monster's name in Floor mode too, so the bar keeps its shape.
	AddGroup(layout, flow, {},
	    {
	        Button { {}, "<", false, [](int) { LabCycleMonster(-1); }, 0 },
	        Button { {}, monster.empty() ? std::string(_("No monsters")) : monster, false, [](int) { LabCycleMonster(1); }, 0 },
	        Button { {}, ">", false, [](int) { LabCycleMonster(1); }, 0 },
	    });

	const std::string heroLabel = LabHeroAuto() ? StrCat(_("Auto"), ": ", ClassName(LabHeroClass())) : std::string(ClassName(LabHeroClass()));
	AddGroup(layout, flow, _("Hero"),
	    {
	        Button { {}, "<", false, [](int) { LabCycleHeroClass(-1); }, 0 },
	        Button { {}, heroLabel, false, [](int) { LabCycleHeroClass(1); }, 0 },
	        Button { {}, ">", false, [](int) { LabCycleHeroClass(1); }, 0 },
	    });

	const std::string weapon = LabWeaponName();
	const std::string weaponLabel = LabWeaponAuto() ? StrCat(_("Auto"), ": ", weapon) : weapon;
	AddGroup(layout, flow, {},
	    {
	        Button { {}, "<", false, [](int) { LabCycleWeapon(-1); }, 0 },
	        Button { {}, weaponLabel, false, [](int) { LabCycleWeapon(1); }, 0 },
	        Button { {}, ">", false, [](int) { LabCycleWeapon(1); }, 0 },
	    });

	AddGroup(layout, flow, _("Gem level"),
	    {
	        Button { {}, "-", false, [](int) { LabSetGemLevel(LabGemLevel() - 1); }, 0 },
	        Button { {}, StrCat(LabGemLevel()), false, nullptr, 0 },
	        Button { {}, "+", false, [](int) { LabSetGemLevel(LabGemLevel() + 1); }, 0 },
	    });

	std::vector<Button> floors;
	for (uint8_t i = 0; i < static_cast<uint8_t>(LabFloor::Count); i++) {
		const auto floor = static_cast<LabFloor>(i);
		if (LabFloorAvailable(floor))
			floors.push_back(Button { {}, std::string(ShortFloorName(floor)), LabCurrentFloor() == floor, [](int value) { LabSetFloor(static_cast<LabFloor>(value)); }, i });
	}
	AddGroup(layout, flow, {}, std::move(floors));
	layout.bar = { { barLeft, layout.panel.position.y }, { out.w() - 4 - barLeft, flow.y + ChipHeight + 6 - layout.panel.position.y } };
	return layout;
}

Layout CurrentLayout()
{
	return ComputeLayout(GlobalBackBuffer());
}

void DrawFrame(const Surface &out, const Rectangle &rect, uint8_t color)
{
	DrawHorizontalLine(out, rect.position, rect.size.width, color);
	DrawHorizontalLine(out, rect.position + Displacement { 0, rect.size.height - 1 }, rect.size.width, color);
	DrawVerticalLine(out, rect.position, rect.size.height, color);
	DrawVerticalLine(out, rect.position + Displacement { rect.size.width - 1, 0 }, rect.size.height, color);
}

void Fill(const Surface &out, const Rectangle &rect, uint8_t color)
{
	FillRect(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height, color);
}

void DrawChip(const Surface &out, const Rectangle &rect, std::string_view text, bool active, bool hovered)
{
	Fill(out, rect, active ? ChipActiveColor : (hovered ? RowHoverColor : ChipColor));
	DrawString(out, text, rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (active ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
}

void MoveSelection(int delta)
{
	RefreshResults();
	if (Results.empty())
		return;
	const auto it = std::find(Results.begin(), Results.end(), LabSelectedSkill());
	int position = it == Results.end() ? -1 : static_cast<int>(it - Results.begin());
	position = std::clamp(position + delta, 0, static_cast<int>(Results.size()) - 1);
	LabSelectSkill(Results[position]);
	const size_t rows = CurrentLayout().visibleRows();
	if (static_cast<size_t>(position) < ScrollTop)
		ScrollTop = static_cast<size_t>(position);
	else if (static_cast<size_t>(position) >= ScrollTop + rows)
		ScrollTop = static_cast<size_t>(position) - rows + 1;
}

void Scroll(int rows)
{
	const int maxTop = std::max(static_cast<int>(Results.size()) - static_cast<int>(CurrentLayout().visibleRows()), 0);
	ScrollTop = static_cast<size_t>(std::clamp(static_cast<int>(ScrollTop) + rows, 0, maxTop));
}

void StartTyping()
{
	Typing = true;
	ClearWasdMovementKeys();
	SDLC_StartTextInput(ghMainWnd);
}

void StopTyping()
{
	if (!Typing)
		return;
	Typing = false;
	ClearWasdMovementKeys();
	SDLC_StopTextInput(ghMainWnd);
}

bool HandleClick(Point position)
{
	const Layout layout = CurrentLayout();
	if (!layout.panel.contains(position) && !layout.bar.contains(position)) {
		StopTyping();
		return false;
	}
	if (Hidden) {
		Hidden = false;
		return true;
	}
	if (layout.hide.contains(position)) {
		Hidden = true;
		return true;
	}
	if (layout.search.contains(position)) {
		StartTyping();
		return true;
	}
	StopTyping();
	if (layout.exit.contains(position)) {
		LabExit();
		return true;
	}
	for (const Button &tab : layout.tabs) {
		if (tab.rect.contains(position)) {
			Category = static_cast<GemCategory>(tab.value);
			RefreshResults();
			return true;
		}
	}
	if (layout.list.contains(position)) {
		RefreshResults();
		const size_t row = ScrollTop + static_cast<size_t>((position.y - layout.list.position.y) / RowHeight);
		if (row < Results.size())
			LabSelectSkill(Results[row]);
		return true;
	}
	for (const Button &button : layout.controls) {
		if (button.rect.contains(position) && button.onClick != nullptr) {
			button.onClick(button.value);
			return true;
		}
	}
	return true;
}

bool MouseOverPanel()
{
	const Layout layout = CurrentLayout();
	return layout.panel.contains(MousePosition) || layout.bar.contains(MousePosition);
}

} // namespace

bool SkillLabPanelHandleEvent(const SDL_Event &event)
{
	if (!SkillLabActive())
		return false;

	if (Typing) {
		if (HandleTextInputEvent(event, QueryInput)) {
			RefreshResults();
			return true;
		}
		switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
			switch (SDLC_EventKey(event)) {
			case SDLK_ESCAPE:
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				StopTyping();
				break;
			case SDLK_UP:
				MoveSelection(-1);
				break;
			case SDLK_DOWN:
				MoveSelection(1);
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
		default:
			break;
		}
	}

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		switch (SDLC_EventKey(event)) {
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			LabCast();
			return true;
		case SDLK_UP:
			MoveSelection(-1);
			return true;
		case SDLK_DOWN:
			MoveSelection(1);
			return true;
		case SDLK_L:
			LabSetLooping(!LabLooping());
			return true;
		case SDLK_P:
			LabSetPaused(!LabPaused());
			return true;
		case SDLK_PERIOD:
			LabStep();
			return true;
		default:
			return false;
		}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event.button.button == SDL_BUTTON_LEFT)
			return HandleClick(MousePosition);
#ifdef USE_SDL1
		if (MouseOverPanel() && event.button.button == SDL_BUTTON_WHEELUP) {
			Scroll(-3);
			return true;
		}
		if (MouseOverPanel() && event.button.button == SDL_BUTTON_WHEELDOWN) {
			Scroll(3);
			return true;
		}
#endif
		return MouseOverPanel();
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return MouseOverPanel();
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
		if (!MouseOverPanel())
			return false;
		Scroll(SDLC_EventWheelIntY(event) > 0 ? -3 : 3);
		return true;
#endif
	default:
		return false;
	}
}

void DrawSkillLabPanel(const Surface &out)
{
	if (!SkillLabActive())
		return;
	RefreshResults();
	const Layout layout = ComputeLayout(out);
	const Rectangle &panel = layout.panel;
	if (Hidden) {
		DrawChip(out, panel, _("Skill Lab"), false, panel.contains(MousePosition));
		return;
	}
	DrawHalfTransparentRectTo(out, panel.position.x, panel.position.y, panel.size.width, panel.size.height);
	DrawHalfTransparentRectTo(out, panel.position.x, panel.position.y, panel.size.width, panel.size.height);
	DrawFrame(out, panel, BorderColor);
	const Rectangle &bar = layout.bar;
	DrawHalfTransparentRectTo(out, bar.position.x, bar.position.y, bar.size.width, bar.size.height);
	DrawHalfTransparentRectTo(out, bar.position.x, bar.position.y, bar.size.width, bar.size.height);
	DrawFrame(out, bar, BorderColor);

	DrawString(out, _("Skill Lab"), layout.title, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::VerticalCenter });
	DrawChip(out, layout.exit, _("Exit"), false, layout.exit.contains(MousePosition));
	DrawChip(out, layout.hide, _("Hide"), false, layout.hide.contains(MousePosition));

	Fill(out, layout.search, FieldColor);
	DrawFrame(out, layout.search, Typing ? BorderColor : ChipColor);
	const Rectangle searchText { layout.search.position + Displacement { 6, 0 }, { layout.search.size.width - 12, layout.search.size.height } };
	if (QueryInput.empty() && !Typing)
		DrawString(out, _("Search"), searchText, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
	DrawString(out, QueryInput.value(), searchText,
	    { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite, .cursorPosition = Typing ? static_cast<int>(QueryCursor.position) : -1 });

	for (const Button &tab : layout.tabs)
		DrawChip(out, tab.rect, tab.label, tab.active, tab.rect.contains(MousePosition));

	const std::vector<LabSkill> &skills = LabSkills();
	const size_t rows = layout.visibleRows();
	for (size_t row = 0; row < rows && ScrollTop + row < Results.size(); row++) {
		const int index = Results[ScrollTop + row];
		const LabSkill &skill = skills[index];
		const Rectangle rowRect { layout.list.position + Displacement { 0, static_cast<int>(row) * RowHeight }, { layout.list.size.width, RowHeight } };
		if (index == LabSelectedSkill())
			Fill(out, rowRect, RowSelectedColor);
		else if (rowRect.contains(MousePosition))
			Fill(out, rowRect, RowHoverColor);
		const Rectangle tierRect { rowRect.position + Displacement { 4, 0 }, { 30, RowHeight } };
		if (skill.skill.kind != SkillKind::Default)
			DrawString(out, GemTierNumeral(skill.tier), tierRect, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonface });
		const Rectangle nameRect { rowRect.position + Displacement { 36, 0 }, { rowRect.size.width - 40, RowHeight } };
		DrawString(out, skill.name, nameRect, { .flags = UiFlags::VerticalCenter | (index == LabSelectedSkill() ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
	}
	if (Results.size() > rows) {
		const int trackHeight = layout.list.size.height;
		const int thumbHeight = std::max(trackHeight * static_cast<int>(rows) / static_cast<int>(Results.size()), 12);
		const int thumbY = (trackHeight - thumbHeight) * static_cast<int>(ScrollTop) / std::max(static_cast<int>(Results.size() - rows), 1);
		FillRect(out, layout.list.position.x + layout.list.size.width - 3, layout.list.position.y + thumbY, 3, thumbHeight, BorderColor);
	}

	for (const auto &[rect, text] : layout.labels)
		DrawString(out, text, rect, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonface });
	for (const Button &button : layout.controls) {
		if (button.onClick == nullptr) {
			DrawString(out, button.label, button.rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
			continue;
		}
		DrawChip(out, button.rect, button.label, button.active, button.rect.contains(MousePosition));
	}
}

} // namespace devilution
