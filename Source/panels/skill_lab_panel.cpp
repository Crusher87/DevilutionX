#include "panels/skill_lab_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <vector>

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
#include "controls/wasd_movement.hpp"
#include "engine/dx.h"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "poe2/skill_lab.hpp"
#include "poe2/skill_lab_notes.hpp"
#include "poe2/skill_looks.hpp"
#include "poe2/support_gems.hpp"
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
char NoteBuffer[200];
TextInputCursorState NoteCursor;
TextInputState NoteInput {
	TextInputState::Options {
	    .value = NoteBuffer,
	    .cursor = &NoteCursor,
	    .maxLength = sizeof(NoteBuffer) - 1,
	}
};
bool Typing;
/** Typing goes to the note rather than the search. */
bool TypingNote;
/** Folded down to a small tab, to see the whole arena. */
bool Hidden;

/** A short message under the bar ("Note saved"), until FlashUntil; the game's own messages sit behind the panel. */
std::string FlashText;
uint32_t FlashUntil;

void Flash(std::string_view text)
{
	FlashText = std::string(text);
	FlashUntil = static_cast<uint32_t>(SDL_GetTicks()) + 2000;
}

/** The look sliders are showing. */
bool LookOpen;
/** The extras (light, support, sound, compare, record) are showing. */
bool ExtrasOpen;
/** The slider being dragged, or -1. */
int Dragging = -1;

/** @brief One of a skill look's numbers, as a slider. */
struct Knob {
	const char *label;
	float SkillLook::*field;
	float lo;
	float hi;
};

const std::array<Knob, 7> Knobs { {
	{ N_("Size"), &SkillLook::size, 0.25F, 3.F },
	{ N_("Amount"), &SkillLook::amount, 0.F, 3.F },
	{ N_("Length"), &SkillLook::length, 0.25F, 3.F },
	{ N_("Bright"), &SkillLook::brightness, 0.F, 2.F },
	{ N_("Speed"), &SkillLook::speed, 0.25F, 3.F },
	{ N_("Shake"), &SkillLook::shake, 0.F, 3.F },
	{ N_("Tint"), &SkillLook::tint, 0.F, 1.F },
} };

struct Swatch {
	const char *name;
	uint8_t r, g, b;
};

const std::array<Swatch, 9> Swatches { {
	{ N_("White"), 255, 255, 255 },
	{ N_("Fire"), 255, 140, 40 },
	{ N_("Ice"), 140, 210, 255 },
	{ N_("Storm"), 170, 180, 255 },
	{ N_("Holy"), 255, 210, 120 },
	{ N_("Chaos"), 120, 220, 60 },
	{ N_("Blood"), 220, 30, 30 },
	{ N_("Arcane"), 120, 140, 255 },
	{ N_("Shadow"), 80, 48, 112 },
} };

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
	/** The note box in the bar. */
	Rectangle note;
	std::vector<Button> tabs;
	Rectangle list;
	/** The strip along the top holding the controls. */
	Rectangle bar;
	std::vector<Button> controls;
	/** Labels drawn left of a row of controls. */
	std::vector<std::pair<Rectangle, std::string>> labels;
	/** The look sliders' tracks, one per knob. */
	std::vector<Rectangle> sliders;

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

void StartTyping(bool note)
{
	TypingNote = note;
	if (Typing)
		return;
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

std::string SelectedName()
{
	if (LabSelectedSkill() < 0)
		return {};
	return LabSkills()[LabSelectedSkill()].name;
}

/** @brief Seconds into the cast at normal speed, e.g. "0.70s". */
std::string MomentText(const LabMoment &moment)
{
	const int hundredths = moment.tick * 5;
	return StrCat(hundredths / 100, ".", hundredths % 100 < 10 ? "0" : "", hundredths % 100, "s");
}

void MarkMoment()
{
	if (!LabMarkMoment()) {
		Flash(_("Cast first, then mark"));
		return;
	}
	StartTyping(true);
}

void SaveNote()
{
	if (!LabSaveNote(NoteInput.value())) {
		StartTyping(true);
		return;
	}
	NoteInput.clear();
	StopTyping();
	Flash(_("Note saved"));
}

std::string SelectedLookKey()
{
	if (LabSelectedSkill() < 0)
		return {};
	return SkillLookKey(LabSkills()[LabSelectedSkill()].skill);
}

/** @brief The selected skill's look as it shows now. */
SkillLook SelectedLook()
{
	return GetSkillLook(SelectedLookKey());
}

void ChangeLook(const SkillLook &look)
{
	const std::string key = SelectedLookKey();
	if (key.empty())
		return;
	SetSkillLookEdit(key, SelectedName(), look);
	SetSkillLookEditsOn(true);
}

void CycleSwatch()
{
	SkillLook look = SelectedLook();
	size_t next = 0;
	for (size_t i = 0; i < Swatches.size(); i++) {
		if (Swatches[i].r == look.r && Swatches[i].g == look.g && Swatches[i].b == look.b) {
			next = (i + 1) % Swatches.size();
			break;
		}
	}
	look.r = Swatches[next].r;
	look.g = Swatches[next].g;
	look.b = Swatches[next].b;
	// A colour with no tint shows nothing, so picking one tints halfway.
	if (look.tint <= 0 && next != 0)
		look.tint = 0.5F;
	ChangeLook(look);
	if (!LabLooping())
		LabCast();
}

std::string SwatchName(const SkillLook &look)
{
	for (const Swatch &swatch : Swatches) {
		if (swatch.r == look.r && swatch.g == look.g && swatch.b == look.b)
			return std::string(_(swatch.name));
	}
	return std::string(_("Custom"));
}

std::string KnobText(float value)
{
	const int hundredths = static_cast<int>(value * 100 + 0.5F);
	return StrCat(hundredths / 100, ".", hundredths % 100 < 10 ? "0" : "", hundredths % 100);
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

/**
 * @brief The look sliders, at the bottom of the left panel under a shorter list, so the arena stays in sight: a label,
 * the track and the number for each knob, then the colour and the before/after, reset and save chips.
 */
void AddLookSliders(Layout &layout, int top)
{
	const SkillLook look = SelectedLook();
	const int left = layout.list.position.x;
	const int width = layout.list.size.width;
	int labelWidth = 0;
	for (const Knob &knob : Knobs)
		labelWidth = std::max(labelWidth, GetLineWidth(_(knob.label)) + 6);
	const int valueWidth = GetLineWidth("0.00") + 8;
	const int trackWidth = width - labelWidth - valueWidth;

	// The chips go under the sliders: laid out from 0, then moved down.
	Layout chips;
	Flow flow { left, left + width, left, 0 };
	AddGroup(chips, flow, _("Colour"), { Button { {}, SwatchName(look), false, [](int) { CycleSwatch(); }, 0 } });
	AddGroup(chips, flow, {},
	    {
	        Button { {}, std::string(_("Before")), !SkillLookEditsOn(), [](int) { SetSkillLookEditsOn(false); if (!LabLooping()) LabCast(); }, 0 },
	        Button { {}, std::string(_("After")), SkillLookEditsOn(), [](int) { SetSkillLookEditsOn(true); if (!LabLooping()) LabCast(); }, 0 },
	    });
	AddGroup(chips, flow, {},
	    {
	        Button { {}, std::string(_("Reset")), false, [](int) { ClearSkillLookEdit(SelectedLookKey()); if (!LabLooping()) LabCast(); }, 0 },
	        Button { {}, std::string(_("Save look")), false, [](int) { Flash(SaveSkillLookEdits() ? _("Look saved") : _("Couldn't save the look")); }, 0 },
	    });
	const int rowHeight = ChipHeight + 2;
	int y = top;
	for (const Knob &knob : Knobs) {
		layout.labels.emplace_back(Rectangle { { left, y }, { labelWidth, ChipHeight } }, std::string(_(knob.label)));
		layout.sliders.push_back({ { left + labelWidth, y }, { trackWidth, ChipHeight } });
		layout.controls.push_back(Button { { { left + labelWidth + trackWidth, y }, { valueWidth, ChipHeight } }, KnobText(look.*knob.field), false, nullptr, 0 });
		y += rowHeight;
	}
	y += Gap;
	for (auto &[rect, text] : chips.labels) {
		rect.position.y += y;
		layout.labels.emplace_back(rect, text);
	}
	for (Button &button : chips.controls) {
		button.rect.position.y += y;
		layout.controls.push_back(std::move(button));
	}
}

void AddBar(Layout &layout, const Surface &out);

std::string SoundName(uint8_t sound)
{
	const std::span<const SkillLookSound> sounds = SkillLookSounds();
	if (sound == 0 || sound >= sounds.size())
		return std::string(_("None"));
	std::string_view name = sounds[sound].name;
	if (name.starts_with("Spell"))
		name.remove_prefix(5);
	return std::string(name);
}

void CycleSound(int delta)
{
	SkillLook look = SelectedLook();
	const int count = static_cast<int>(SkillLookSounds().size());
	look.sound = static_cast<uint8_t>(((look.sound + delta) % count + count) % count);
	ChangeLook(look);
	if (!LabLooping())
		LabCast();
}

/** @brief The extras page: light, a support gem, an extra sound, comparing looks and recording, a line each. */
void AddExtras(Layout &layout, int top)
{
	const int left = layout.list.position.x;
	Flow flow { left, left + layout.list.size.width, left, top };
	auto nextLine = [&flow]() {
		flow.x = flow.left;
		flow.y += ChipHeight + Gap;
	};
	std::vector<Button> lights;
	for (const auto &[light, label] : { std::pair { LabLight::Normal, N_("Normal") }, std::pair { LabLight::Bright, N_("Bright") }, std::pair { LabLight::Dark, N_("Dark") } })
		lights.push_back(Button { {}, std::string(_(label)), LabLighting() == light, [](int value) { LabSetLighting(static_cast<LabLight>(value)); }, static_cast<int>(light) });
	AddGroup(layout, flow, _("Light"), std::move(lights));
	nextLine();
	const SupportGem support = LabSupport();
	AddGroup(layout, flow, _("Support"),
	    {
	        Button { {}, "<", false, [](int) { LabCycleSupport(-1); }, 0 },
	        Button { {}, support == SupportGem::None ? std::string(_("None")) : std::string(SupportGemName(support)), support != SupportGem::None, [](int) { LabCycleSupport(1); }, 0 },
	        Button { {}, ">", false, [](int) { LabCycleSupport(1); }, 0 },
	    });
	nextLine();
	const uint8_t sound = SelectedLook().sound;
	AddGroup(layout, flow, _("Sound"),
	    {
	        Button { {}, "<", false, [](int) { CycleSound(-1); }, 0 },
	        Button { {}, SoundName(sound), sound != 0, [](int) { CycleSound(1); }, 0 },
	        Button { {}, ">", false, [](int) { CycleSound(1); }, 0 },
	    });
	nextLine();
	AddGroup(layout, flow, {},
	    {
	        Button { {}, std::string(_("Compare looks")), LabComparing(), [](int) { LabSetComparing(!LabComparing()); }, 0 },
	        Button { {}, std::string(LabRecording() ? _("Recording") : _("Record")), LabRecording(), [](int) { LabRecord(); }, 0 },
	    });
}

/** @brief The cast's timeline along the bottom of the arena: seconds, where the hit lands, pinned notes and now. */
void DrawTimeline(const Surface &out, const Layout &layout)
{
	const int now = LabTicksSinceCast();
	if (now < 0)
		return;
	constexpr int TicksPerSecond = 20;
	const int left = (Hidden ? 4 : layout.panel.position.x + layout.panel.size.width) + 12;
	const int width = std::min(260, out.w() - left - 8);
	const int y = GetMainPanel().position.y - 16;
	// Whole seconds, long enough for the cast before and this one so far.
	const int span = std::max((std::max({ LabLastCastTicks(), now, TicksPerSecond * 2 }) + TicksPerSecond - 1) / TicksPerSecond * TicksPerSecond, TicksPerSecond);
	auto xOf = [&](int tick) { return left + std::min(tick, span) * width / span; };
	FillRect(out, left, y, width, 1, ChipColor);
	for (int tick = 0; tick <= span; tick += TicksPerSecond / 4) {
		const bool second = tick % TicksPerSecond == 0;
		FillRect(out, xOf(tick), y - (second ? 4 : 2), 1, second ? 9 : 5, second ? BorderColor : ChipColor);
		if (second)
			DrawString(out, StrCat(tick / TicksPerSecond, "s"), { { xOf(tick) - 10, y - 18 }, { 20, 12 } }, { .flags = UiFlags::AlignCenter | UiFlags::ColorButtonface | UiFlags::FontSize12 });
	}
	for (const int tick : LabNoteTicks(SelectedName()))
		FillRect(out, xOf(tick) - 1, y - 6, 3, 3, PAL16_GRAY);
	if (const int hit = LabActionTick(); hit >= 0)
		FillRect(out, xOf(hit) - 1, y - 5, 3, 11, PAL16_RED + 4);
	FillRect(out, xOf(now) - 1, y - 7, 3, 15, PAL16_YELLOW + 2);
	if (LabComparing()) {
		DrawString(out, SkillLookEditsOn() ? _("After") : _("Before"), { { left, y - 34 }, { width, 14 } },
		    { .flags = UiFlags::ColorWhitegold });
	}
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

	// What the panel shows: the skill list, the selected skill's look sliders, or the extras.
	int pageX = innerX;
	for (const auto &[page, label] : { std::pair { 0, N_("Skills") }, std::pair { 1, N_("Look") }, std::pair { 2, N_("Extras") } }) {
		const std::string text(_(label));
		const int width = GetLineWidth(text) + 12;
		const bool shown = (page == 1 && LookOpen) || (page == 2 && ExtrasOpen) || (page == 0 && !LookOpen && !ExtrasOpen);
		layout.tabs.push_back(Button { { { pageX, y }, { width, ChipHeight } }, text, shown, [](int value) {
			LookOpen = value == 1;
			ExtrasOpen = value == 2;
			Dragging = -1; }, page });
		pageX += width + Gap;
	}
	y += ChipHeight + Gap;

	if ((LookOpen || ExtrasOpen) && LabSelectedSkill() >= 0) {
		// The sliders or extras take the list's place, under the skill's name; their chip again brings the list back.
		layout.labels.emplace_back(Rectangle { { innerX, y }, { innerWidth, 20 } }, LabSkills()[LabSelectedSkill()].name);
		y += 24;
		layout.list = { { innerX, y }, { innerWidth, 0 } };
		if (LookOpen)
			AddLookSliders(layout, y);
		else
			AddExtras(layout, y);
		AddBar(layout, out);
		return layout;
	}

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
	AddBar(layout, out);
	return layout;
}

/** @brief The controls in the bar along the top, right of the panel, so the list keeps the panel's height. */
void AddBar(Layout &layout, const Surface &out)
{
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

	// Notes get a line of their own: status, mark, the note box and save.
	flow.x = flow.left;
	flow.y += ChipHeight + Gap;
	std::string status;
	if (LabSelectedSkill() >= 0)
		status = LabSkills()[LabSelectedSkill()].name;
	const LabStatus current = LabSkillStatus(status);
	AddGroup(layout, flow, {},
	    {
	        Button { {}, std::string(_("Needs work")), current == LabStatus::NeedsWork, [](int) { LabSetStatus(LabSkillStatus(SelectedName()) == LabStatus::NeedsWork ? LabStatus::Unseen : LabStatus::NeedsWork); }, 0 },
	        Button { {}, std::string(_("Approved")), current == LabStatus::Approved, [](int) { LabSetStatus(LabSkillStatus(SelectedName()) == LabStatus::Approved ? LabStatus::Unseen : LabStatus::Approved); }, 0 },
	    });
	const std::optional<LabMoment> moment = LabHeldMoment();
	const std::string markLabel = moment ? StrCat(_("Mark"), " ", MomentText(*moment)) : std::string(_("Mark"));
	AddGroup(layout, flow, {}, { Button { {}, markLabel, moment.has_value(), [](int) { MarkMoment(); }, 0 } });
	const std::string saveLabel(_("Save"));
	const int saveWidth = GetLineWidth(saveLabel) + 12;
	if (flow.right - flow.x - saveWidth - Gap < 160) {
		flow.x = flow.left;
		flow.y += ChipHeight + Gap;
	}
	const int noteWidth = flow.right - flow.x - saveWidth - Gap;
	layout.note = { { flow.x, flow.y }, { noteWidth, ChipHeight } };
	flow.x += noteWidth + Gap;
	layout.controls.push_back(Button { { { flow.x, flow.y }, { saveWidth, ChipHeight } }, saveLabel, false, [](int) { SaveNote(); }, 0 });

	layout.bar = { { barLeft, layout.panel.position.y }, { out.w() - 4 - barLeft, flow.y + ChipHeight + 6 - layout.panel.position.y } };
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

/** @brief Sets the dragged slider's knob from where the mouse is along its track. */
void DragSlider(const Layout &layout, Point position)
{
	if (Dragging < 0 || static_cast<size_t>(Dragging) >= layout.sliders.size())
		return;
	const Rectangle &track = layout.sliders[Dragging];
	const Knob &knob = Knobs[Dragging];
	const float t = std::clamp(static_cast<float>(position.x - track.position.x - 3) / static_cast<float>(track.size.width - 6), 0.F, 1.F);
	// Steps of 0.05, so the numbers stay tidy.
	const float value = std::round((knob.lo + (knob.hi - knob.lo) * t) * 20) / 20;
	SkillLook look = SelectedLook();
	if (look.*knob.field == value && HasSkillLookEdit(SelectedLookKey()))
		return;
	look.*knob.field = value;
	ChangeLook(look);
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
		StartTyping(false);
		return true;
	}
	if (layout.note.contains(position)) {
		StartTyping(true);
		return true;
	}
	StopTyping();
	if (layout.exit.contains(position)) {
		LabExit();
		return true;
	}
	for (const Button &tab : layout.tabs) {
		if (tab.rect.contains(position)) {
			if (tab.onClick != nullptr) {
				tab.onClick(tab.value);
				return true;
			}
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
	for (size_t i = 0; i < layout.sliders.size(); i++) {
		if (layout.sliders[i].contains(position)) {
			Dragging = static_cast<int>(i);
			DragSlider(layout, position);
			return true;
		}
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
		if (HandleTextInputEvent(event, TypingNote ? NoteInput : QueryInput)) {
			RefreshResults();
			return true;
		}
		switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
			switch (SDLC_EventKey(event)) {
			case SDLK_ESCAPE:
				StopTyping();
				break;
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				if (TypingNote)
					SaveNote();
				else
					StopTyping();
				break;
			case SDLK_UP:
				if (!TypingNote)
					MoveSelection(-1);
				break;
			case SDLK_DOWN:
				if (!TypingNote)
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
		case SDLK_M:
			MarkMoment();
			return true;
		case SDLK_N:
			StartTyping(true);
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
		if (Dragging >= 0 && event.button.button == SDL_BUTTON_LEFT) {
			DragSlider(CurrentLayout(), MousePosition);
			Dragging = -1;
			// Show the change straight away.
			if (!LabLooping())
				LabCast();
			return true;
		}
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
	SkillLabCaptureFrame(out);
	// The panel stays out of the recording's pictures.
	if (SkillLabRecordingNow())
		return;
	RefreshResults();
	if (Dragging >= 0)
		DragSlider(ComputeLayout(out), MousePosition);
	const Layout layout = ComputeLayout(out);
	const Rectangle &panel = layout.panel;
	DrawTimeline(out, layout);
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

	if (layout.search.size.width > 0) {
		Fill(out, layout.search, FieldColor);
		DrawFrame(out, layout.search, Typing && !TypingNote ? BorderColor : ChipColor);
		const Rectangle searchText { layout.search.position + Displacement { 6, 0 }, { layout.search.size.width - 12, layout.search.size.height } };
		if (QueryInput.empty() && !(Typing && !TypingNote))
			DrawString(out, _("Search"), searchText, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
		DrawString(out, QueryInput.value(), searchText,
		    { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite, .cursorPosition = Typing && !TypingNote ? static_cast<int>(QueryCursor.position) : -1 });
	}

	for (const Button &tab : layout.tabs)
		DrawChip(out, tab.rect, tab.label, tab.active, tab.rect.contains(MousePosition));

	const std::vector<LabSkill> &skills = LabSkills();
	const size_t rows = layout.list.size.height > 0 ? layout.visibleRows() : 0;
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
		UiFlags color = index == LabSelectedSkill() ? UiFlags::ColorWhitegold : UiFlags::ColorWhite;
		switch (LabSkillStatus(skill.name)) {
		case LabStatus::NeedsWork:
			color = UiFlags::ColorOrange;
			break;
		case LabStatus::Approved:
			color = UiFlagsColorGreen;
			break;
		default:
			break;
		}
		DrawString(out, skill.name, nameRect, { .flags = UiFlags::VerticalCenter | color });
	}
	if (rows > 0 && Results.size() > rows) {
		const int trackHeight = layout.list.size.height;
		const int thumbHeight = std::max(trackHeight * static_cast<int>(rows) / static_cast<int>(Results.size()), 12);
		const int thumbY = (trackHeight - thumbHeight) * static_cast<int>(ScrollTop) / std::max(static_cast<int>(Results.size() - rows), 1);
		FillRect(out, layout.list.position.x + layout.list.size.width - 3, layout.list.position.y + thumbY, 3, thumbHeight, BorderColor);
	}

	Fill(out, layout.note, FieldColor);
	DrawFrame(out, layout.note, Typing && TypingNote ? BorderColor : ChipColor);
	const Rectangle noteText { layout.note.position + Displacement { 6, 0 }, { layout.note.size.width - 12, layout.note.size.height } };
	if (NoteInput.empty() && !(Typing && TypingNote)) {
		const int notes = LabNoteCount(SelectedName());
		DrawString(out, notes > 0 ? StrCat(_("Note for Claude"), " (", notes, ")") : std::string(_("Note for Claude")), noteText, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
	}
	DrawString(out, NoteInput.value(), noteText,
	    { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite, .cursorPosition = Typing && TypingNote ? static_cast<int>(NoteCursor.position) : -1 });

	if (!layout.sliders.empty()) {
		const SkillLook look = SelectedLook();
		for (size_t i = 0; i < layout.sliders.size(); i++) {
			const Rectangle &track = layout.sliders[i];
			const Knob &knob = Knobs[i];
			const int middle = track.position.y + track.size.height / 2;
			FillRect(out, track.position.x + 3, middle - 1, track.size.width - 6, 3, ChipColor);
			// A tick where the knob does nothing (1, or 0 for tint).
			const float plain = knob.field == &SkillLook::tint ? 0.F : 1.F;
			const int plainX = track.position.x + 3 + static_cast<int>((plain - knob.lo) / (knob.hi - knob.lo) * (track.size.width - 6));
			FillRect(out, plainX, middle - 4, 1, 9, PAL16_GRAY + 8);
			const float t = std::clamp((look.*knob.field - knob.lo) / (knob.hi - knob.lo), 0.F, 1.F);
			const int handleX = track.position.x + 3 + static_cast<int>(t * (track.size.width - 6));
			const bool hot = Dragging == static_cast<int>(i) || track.contains(MousePosition);
			FillRect(out, handleX - 3, middle - 6, 7, 13, hot ? PAL16_YELLOW + 2 : ChipActiveColor);
		}
	}
	if (!FlashText.empty() && static_cast<uint32_t>(SDL_GetTicks()) < FlashUntil) {
		const Rectangle flash { { bar.position.x, bar.position.y + bar.size.height + 4 }, { bar.size.width, 20 } };
		DrawString(out, FlashText, flash, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
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
