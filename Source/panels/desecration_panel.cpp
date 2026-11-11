#include "panels/desecration_panel.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif

#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "diablo.h"
#include "engine/dx.h"
#include "game_mode.hpp"
#include "inv.h"
#include "panels/poe2_widgets.hpp"
#include "panels/skill_bar.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/desecration.hpp"
#include "poe2/telemetry.hpp"
#include "qol/stash.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr int Width = 460;
constexpr int Padding = 14;
constexpr int HeaderHeight = 64;
constexpr int OfferGap = 8;
constexpr int LineHeight = 18;

bool IsOpen;
/** The item being revealed, found again each time by its seed, since it may sit in the stash. */
struct RevealTarget {
	int idx;
	uint32_t seed;
	uint16_t createInfo;
};
std::optional<RevealTarget> Target;
bool Kinship;

bool Matches(const Item &item)
{
	return Target && !item.isEmpty() && item.IDidx == Target->idx && static_cast<uint32_t>(item._iSeed) == Target->seed && item._iCreateInfo == Target->createInfo;
}

Item *TargetItem()
{
	if (!Target || MyPlayer == nullptr)
		return nullptr;
	Player &player = *MyPlayer;
	for (Item &item : player.InvBody) {
		if (Matches(item))
			return &item;
	}
	for (int i = 0; i < player._pNumInv; i++) {
		if (Matches(player.InvList[i]))
			return &player.InvList[i];
	}
	for (Item &item : Stash.stashList) {
		if (Matches(item))
			return &item;
	}
	return nullptr;
}

bool HoldsKinship()
{
	const Player &player = *MyPlayer;
	for (int i = 0; i < player._pNumInv; i++) {
		if (GetOmenId(player.InvList[i]) == OmenId::Kinship)
			return true;
	}
	return false;
}

struct OfferRow {
	Poe2CraftMod mod;
	std::vector<std::string> lines;
	Rectangle rect;
};

struct Layout {
	Rectangle panel;
	std::vector<OfferRow> offers;
};

Layout ComputeLayout(const Surface &out, const Item &item)
{
	Layout layout;
	const std::vector<Poe2CraftMod> offers = DesecrationOffers(item, Kinship);
	int height = HeaderHeight + Padding;
	for (const Poe2CraftMod &offer : offers) {
		OfferRow row { offer, DesecratedModText(item, offer), {} };
		if (const DesecratedMod *desecrated = GetDesecratedMod(offer.family); desecrated != nullptr)
			row.lines.insert(row.lines.begin(), std::string(desecrated->name));
		row.rect.size = { Width - (2 * Padding), (static_cast<int>(row.lines.size()) * LineHeight) + 12 };
		height += row.rect.size.height + OfferGap;
		layout.offers.push_back(std::move(row));
	}
	const int availableHeight = out.h() - GetMainPanel().size.height;
	const int width = std::min(Width, out.w() - 16);
	layout.panel = { { (out.w() - width) / 2, std::max((availableHeight - height) / 2, 4) }, { width, height } };
	int y = layout.panel.position.y + HeaderHeight;
	for (OfferRow &row : layout.offers) {
		row.rect.position = { layout.panel.position.x + Padding, y };
		row.rect.size.width = width - (2 * Padding);
		y += row.rect.size.height + OfferGap;
	}
	return layout;
}

void Reveal(Item &item, const Poe2CraftMod &choice)
{
	Player &player = *MyPlayer;
	if (!RevealDesecratedMod(player, item, choice))
		return;
	if (Kinship) {
		for (int i = 0; i < player._pNumInv; i++) {
			if (GetOmenId(player.InvList[i]) == OmenId::Kinship) {
				EventPlrMsg(FormatRuntime(_("{:s} is used up."), GetOmen(OmenId::Kinship).name));
				player.RemoveInvItem(i);
				break;
			}
		}
	}
	EventPlrMsg(FormatRuntime(_("Revealed: {:s}"), item.getName().str()));
	TelemetryCrafted(player, "Desecration reveal", item);
	Stash.dirty = true;
	CalcPlrInv(player, true);
	CloseDesecrationReveal();
}

} // namespace

bool IsDesecrationRevealOpen()
{
	return IsOpen;
}

void OpenDesecrationReveal(const Item &item)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || !HasUnrevealedMod(item))
		return;
	Target = RevealTarget { item.IDidx, static_cast<uint32_t>(item._iSeed), item._iCreateInfo };
	Kinship = HoldsKinship();
	IsOpen = true;
	// The panel takes the mouse button's release, like the gemcutting panel.
	sgbMouseDown = CLICK_NONE;
	LastPlayerAction = PlayerActionType::None;
	ReleaseAllTraySlots();
	ClearWasdMovementKeys();
}

void CloseDesecrationReveal()
{
	if (!IsOpen)
		return;
	IsOpen = false;
	Target = std::nullopt;
	ClearWasdMovementKeys();
}

bool DesecrationRevealHandleEvent(const SDL_Event &event)
{
	if (!IsOpen)
		return false;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		if (SDLC_EventKey(event) == SDLK_ESCAPE) {
			// The modifier stays unrevealed; another Ancient Bone on the item brings the choice back.
			EventPlrMsg(_("The modifier stays unrevealed. Use an Ancient Bone on the item to choose later."));
			CloseDesecrationReveal();
		}
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		if (event.button.button != SDL_BUTTON_LEFT)
			return true;
		Item *item = TargetItem();
		if (item == nullptr) {
			CloseDesecrationReveal();
			return true;
		}
		const Layout layout = ComputeLayout(GlobalBackBuffer(), *item);
		for (const OfferRow &row : layout.offers) {
			if (row.rect.contains(MousePosition)) {
				Reveal(*item, row.mod);
				return true;
			}
		}
		return true;
	}
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_MOUSE_BUTTON_UP:
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
#endif
		return true;
	default:
		return false;
	}
}

void DrawDesecrationReveal(const Surface &out)
{
	if (!IsOpen)
		return;
	const Item *item = TargetItem();
	if (item == nullptr || !HasUnrevealedMod(*item)) {
		CloseDesecrationReveal();
		return;
	}
	const Layout layout = ComputeLayout(out, *item);
	DrawPanel(out, layout.panel);
	const Rectangle title { layout.panel.position + Displacement { Padding, 8 }, { layout.panel.size.width - (2 * Padding), 26 } };
	DrawString(out, _("Reveal a Desecrated Modifier"), title, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	const Rectangle subtitle { title.position + Displacement { 0, 28 }, { title.size.width, 20 } };
	DrawString(out, item->getName().str(), subtitle, { .flags = UiFlags::ColorYellow | UiFlags::AlignCenter | UiFlags::VerticalCenter });

	for (const OfferRow &row : layout.offers) {
		const bool hovered = row.rect.contains(MousePosition);
		FillRectangle(out, row.rect, hovered ? RowHoverColor : RowColor);
		DrawFrame(out, row.rect, hovered ? HoverColor : InnerBorderColor);
		Rectangle line { row.rect.position + Displacement { 8, 6 }, { row.rect.size.width - 16, LineHeight } };
		const bool exclusive = GetDesecratedMod(row.mod.family) != nullptr;
		for (size_t i = 0; i < row.lines.size(); i++) {
			const UiFlags color = exclusive && i == 0 ? UiFlags::ColorOrange : UiFlags::ColorBlue;
			DrawString(out, row.lines[i], line, { .flags = color | UiFlags::VerticalCenter });
			line.position.y += LineHeight;
		}
	}
}

} // namespace devilution
