#include "panels/skill_bar.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "control/control.hpp"
#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "diablo.h"
#include "effects.h"
#include "engine/palette.h"
#include "engine/rectangle.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "msg.h"
#include "options.h"
#include "panels/item_spawner.hpp"
#include "panels/poe2_widgets.hpp"
#include "panels/skills_panel.hpp"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/default_attack.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
#include "poe2/weapon_swap.hpp"
#include "spells.h"
#include "stores.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr Size SlotSize { 37, 38 };
constexpr int SlotGap = 2;
constexpr int TrayColumns = 5;
constexpr int TrayRows = 2;
constexpr int TrayWidth = (TrayColumns * (SlotSize.width + SlotGap)) - SlotGap;
constexpr int TrayHeight = (TrayRows * (SlotSize.height + SlotGap)) - SlotGap;
constexpr int PortalCell = 0;
/** @brief The tray cell of each slot, in TraySlot order: the mouse buttons and G on the top row after the Town Portal button, Q to F below. */
constexpr std::array<int, NumTraySlots> SlotCells { 1, 2, 3, 5, 6, 7, 8, 9, 4 };

constexpr int PopupWidth = 280;
constexpr int PopupPad = 8;
constexpr Size ActionButtonSize { 84, 20 };
constexpr int ActionButtonGap = 4;
constexpr int GemColumns = 6;
constexpr int PopupSocketSize = 26;
constexpr int PopupSocketGap = 6;
constexpr int PickerRowHeight = 18;
constexpr size_t MaxPickerRows = 8;
/** @brief The actions the left and right mouse buttons can take instead of a skill. */
constexpr std::array<TrayAction, 3> MouseActions { TrayAction::MoveAndAttack, TrayAction::Interact, TrayAction::ReadiedSpell };

/** @brief The tray slot whose popup is open, and the support socket its gem list fills. */
struct TrayPopup {
	bool open = false;
	TraySlot slot = TraySlot::LeftMouse;
	bool pickerOpen = false;
	size_t pickerSocket = 0;
};

TrayPopup Popup;

/** @brief A kind of support gem the character carries, and where the first one is. */
struct OwnedSupport {
	SupportGem gem;
	int tier;
	int count;
	bool inBelt;
	int index;
};

struct PopupLayout {
	Rectangle box;
	Rectangle title;
	bool hasActions = false;
	std::array<Rectangle, MouseActions.size()> actions;
	Rectangle weaponAttack;
	Rectangle gemsLabel;
	std::array<Rectangle, NumSkillSlots> gems;
	Rectangle supportsLabel;
	std::array<Rectangle, MaxSupportSockets> sockets;
	Rectangle picker;
};

struct PopupHit {
	enum class Kind : uint8_t {
		None,
		Box,
		Action,
		WeaponAttack,
		Gem,
		Socket,
		Picker,
	} kind
	    = Kind::None;
	size_t index = 0;
};

bool Overlaps(const Rectangle &a, const Rectangle &b)
{
	return a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width
	    && a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
}

/** @brief The tray sits just above the right end of the main panel, or the left end if the inventory covers that. */
Rectangle TrayRect()
{
	const Rectangle &panel = GetMainPanel();
	Rectangle tray { { panel.position.x + panel.size.width - TrayWidth - 4, panel.position.y - TrayHeight - 4 }, { TrayWidth, TrayHeight } };
	if (IsRightPanelOpen() && Overlaps(tray, GetRightPanel()))
		tray.position.x = panel.position.x + 4;
	return tray;
}

Rectangle CellRect(int cell)
{
	const Rectangle tray = TrayRect();
	return { { tray.position.x + ((cell % TrayColumns) * (SlotSize.width + SlotGap)), tray.position.y + ((cell / TrayColumns) * (SlotSize.height + SlotGap)) }, SlotSize };
}

Rectangle SlotRect(TraySlot slot)
{
	return CellRect(SlotCells[static_cast<size_t>(slot)]);
}

std::optional<TraySlot> TraySlotAt(Point position)
{
	for (size_t i = 0; i < NumTraySlots; i++) {
		const auto slot = static_cast<TraySlot>(i);
		if (SlotRect(slot).contains(position))
			return slot;
	}
	return std::nullopt;
}

const TrayBinding &Binding(const Player &player, TraySlot slot)
{
	return player.poe2.tray[static_cast<size_t>(slot)];
}

Point BottomLeft(const Rectangle &rect)
{
	return { rect.position.x, rect.position.y + rect.size.height - 1 };
}

std::string_view ActionName(TrayAction action)
{
	switch (action) {
	case TrayAction::MoveAndAttack:
		return _("Move and attack");
	case TrayAction::Interact:
		return _("Interact");
	case TrayAction::ReadiedSpell:
		return _("Readied spell");
	default:
		return {};
	}
}

std::string_view ActionButtonLabel(TrayAction action)
{
	switch (action) {
	case TrayAction::MoveAndAttack:
		return _("Move/attack");
	case TrayAction::Interact:
		return _("Interact");
	default:
		return _("Readied spell");
	}
}

SpellIcon AttackIcon(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Cleave:
		return SpellIcon::Berserk;
	case WeaponSkill::GroundSlam:
		return SpellIcon::PentaStar;
	case WeaponSkill::Barrage:
		return SpellIcon::Sentinel;
	case WeaponSkill::WhirlingStaff:
		return SpellIcon::Immolation;
	case WeaponSkill::Flurry:
		return SpellIcon::Jester;
	case WeaponSkill::LeapSlam:
		return SpellIcon::RedSkull;
	case WeaponSkill::RainOfArrows:
		return SpellIcon::FireCloud;
	case WeaponSkill::ShieldCharge:
		return SpellIcon::Reflect;
	case WeaponSkill::BattleCry:
		return SpellIcon::LongHorn;
	case WeaponSkill::LightningArrow:
		return SpellIcon::Elemental;
	case WeaponSkill::EscapeShot:
		return SpellIcon::Phasing;
	case WeaponSkill::EmberFusillade:
		return SpellIcon::BloodStar;
	case WeaponSkill::Spark:
		return SpellIcon::LightningWall;
	case WeaponSkill::IceBolt:
		return SpellIcon::StoneCurse;
	case WeaponSkill::FrostNova:
		return SpellIcon::Nova;
	case WeaponSkill::Riposte:
		return SpellIcon::ItemRepair;
	case WeaponSkill::ShadowStep:
		return SpellIcon::Invisibility;
	case WeaponSkill::Frenzy:
		return SpellIcon::BloodBoil;
	case WeaponSkill::Smite:
		return SpellIcon::HolyBolt;
	case WeaponSkill::FistsOfThunder:
		return SpellIcon::ChargedBolt;
	case WeaponSkill::RapidFire:
		return SpellIcon::Search;
	case WeaponSkill::ShieldBash:
		return SpellIcon::Telekinesis;
	case WeaponSkill::AmplifyDamage:
		return SpellIcon::Mana;
	default:
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			return static_cast<SpellIcon>(table->icon);
		return SpellIcon::Empty;
	}
}

SpellIcon PersistentIcon(PersistentSkill skill)
{
	switch (skill) {
	case PersistentSkill::HeraldOfAsh:
		return SpellIcon::FlameWave;
	case PersistentSkill::HeraldOfThunder:
		return SpellIcon::ChainLightning;
	case PersistentSkill::PurityOfElements:
		return SpellIcon::Pentagram;
	case PersistentSkill::SummonGolem:
		return SpellIcon::Golem;
	case PersistentSkill::SkeletalWarriors:
		return SpellIcon::BoneSpirit;
	case PersistentSkill::HeraldOfFrost:
		return SpellIcon::Infravision;
	case PersistentSkill::SkeletalMages:
		return SpellIcon::Resurrect;
	case PersistentSkill::BloodGolem:
		return SpellIcon::BloodStar;
	case PersistentSkill::FireGolem:
		return SpellIcon::Immolation;
	case PersistentSkill::Guardian:
		return SpellIcon::Sentinel;
	case PersistentSkill::ShadowWarrior:
		return SpellIcon::Etherealize;
	case PersistentSkill::Revive:
		return SpellIcon::Resurrect;
	case PersistentSkill::ManaShield:
		return SpellIcon::ManaShield;
	case PersistentSkill::VizjereiFocus:
		return SpellIcon::Lightning;
	case PersistentSkill::Berserk:
		return SpellIcon::Berserk;
	case PersistentSkill::Reflect:
		return SpellIcon::Reflect;
	case PersistentSkill::CastOnDodge:
		return SpellIcon::Phasing;
	case PersistentSkill::CastWhenDamageTaken:
		return SpellIcon::BloodBoil;
	case PersistentSkill::CastWhileChannelling:
		return SpellIcon::Apocalypse;
	case PersistentSkill::ReapersInvocation:
		return SpellIcon::RedSkull;
	case PersistentSkill::MirageArcher:
		return SpellIcon::Invisibility;
	case PersistentSkill::Blasphemy:
		return SpellIcon::Pentagram;
	case PersistentSkill::HoradricTotem:
		return SpellIcon::LightningWall;
	case PersistentSkill::CastOnCritical:
		return SpellIcon::Flash;
	case PersistentSkill::CastOnElementalAilment:
		return SpellIcon::Elemental;
	case PersistentSkill::CastOnMinionDeath:
		return SpellIcon::BoneSpirit;
	case PersistentSkill::BarrierInvocation:
		return SpellIcon::RingOfFire;
	case PersistentSkill::ElementalInvocation:
		return SpellIcon::Inferno;
	case PersistentSkill::WindDancer:
		return SpellIcon::Warp;
	case PersistentSkill::GhostDance:
		return SpellIcon::Etherealize;
	case PersistentSkill::Barkskin:
		return SpellIcon::StoneCurse;
	case PersistentSkill::ArcticArmour:
		return SpellIcon::Nova;
	case PersistentSkill::HeraldOfPlague:
		return SpellIcon::DoomSerpents;
	case PersistentSkill::CombatFrenzy:
		return SpellIcon::LongHorn;
	case PersistentSkill::EternalRage:
		return SpellIcon::BloodRitual;
	case PersistentSkill::ElementalConflux:
		return SpellIcon::PentaStar;
	case PersistentSkill::AlchemistsBoon:
		return SpellIcon::Healing;
	case PersistentSkill::OverwhelmingPresence:
		return SpellIcon::Telekinesis;
	case PersistentSkill::TrailOfCaltrops:
		return SpellIcon::TrapDisarm;
	case PersistentSkill::ManaRemnants:
		return SpellIcon::Mana;
	case PersistentSkill::Attrition:
		return SpellIcon::Search;
	case PersistentSkill::Blink:
		return SpellIcon::Teleport;
	case PersistentSkill::IronWard:
		return SpellIcon::ItemRepair;
	case PersistentSkill::PlagueBearer:
		return SpellIcon::FireCloud;
	case PersistentSkill::Convalescence:
		return SpellIcon::HealOther;
	case PersistentSkill::GrimFeast:
		return SpellIcon::BloodStar;
	case PersistentSkill::WarBanner:
		return SpellIcon::FlameWave;
	case PersistentSkill::DefianceBanner:
		return SpellIcon::Sentinel;
	case PersistentSkill::DreadBanner:
		return SpellIcon::Pentagram;
	case PersistentSkill::Briarpatch:
		return SpellIcon::TrapDisarm;
	case PersistentSkill::MagmaBarrier:
		return SpellIcon::Immolation;
	case PersistentSkill::ScavengedPlating:
		return SpellIcon::Golem;
	case PersistentSkill::TimeOfNeed:
		return SpellIcon::Healing;
	case PersistentSkill::ChargeRegulation:
		return SpellIcon::ChargedBolt;
	case PersistentSkill::WitheringPresence:
		return SpellIcon::Infravision;
	case PersistentSkill::Trinity:
		return SpellIcon::Elemental;
	case PersistentSkill::SiphonElements:
		return SpellIcon::StaffRecharge;
	case PersistentSkill::HeraldOfBlood:
		return SpellIcon::BloodBoil;
	case PersistentSkill::SkeletalSnipers:
		return SpellIcon::Search;
	case PersistentSkill::SkeletalFrostMages:
		return SpellIcon::StoneCurse;
	case PersistentSkill::SkeletalStormMages:
		return SpellIcon::Lightning;
	case PersistentSkill::SkeletalReavers:
		return SpellIcon::Berserk;
	case PersistentSkill::SkeletalBrutes:
		return SpellIcon::LongHorn;
	case PersistentSkill::SkeletalClerics:
		return SpellIcon::HealOther;
	case PersistentSkill::WolfPack:
		return SpellIcon::RedSkull;
	case PersistentSkill::RagingSpirits:
		return SpellIcon::Inferno;
	case PersistentSkill::RavenousSwarm:
		return SpellIcon::DoomSerpents;
	case PersistentSkill::Sacrifice:
		return SpellIcon::BloodRitual;
	case PersistentSkill::LingeringIllusion:
		return SpellIcon::Invisibility;
	default:
		return SpellIcon::Empty;
	}
}

std::string MissingRequirements(const Player &player, SkillSlot skill)
{
	const std::optional<size_t> gemIndex = SkillGemItemIndex(skill);
	if (!gemIndex)
		return {};
	const ItemData &gem = AllItemsList[*gemIndex];
	std::string text;
	const auto add = [&text](int needed, int have, std::string_view stat) {
		if (have >= needed)
			return;
		StrAppend(text, text.empty() ? "" : ", ", FormatRuntime(_("{:d} {:s}"), needed, stat));
	};
	add(gem.iMinStr, player._pStrength, _("Strength"));
	add(gem.iMinMag, player._pMagic, _("Magic"));
	add(gem.iMinDex, player._pDexterity, _("Dexterity"));
	return FormatRuntime(_("Needs {:s}"), text);
}

void PlayGemSound(const Item &gem)
{
	PlaySFX(ItemInvSnds[ItemCAnimTbl[gem._iCurs]]);
}

void HoldItem(Player &player, Item &&item)
{
	player.HoldItem = std::move(item);
	NewCursor(player.HoldItem);
}

/** @brief The support gems in the inventory and belt, one entry per kind, in gem order. */
std::vector<OwnedSupport> OwnedSupports(const Player &player)
{
	std::vector<OwnedSupport> owned;
	const auto add = [&owned](const Item &item, bool inBelt, int index) {
		const SupportGem gem = SupportGemForItem(item);
		if (gem == SupportGem::None)
			return;
		const int tier = SupportGemItemTier(item);
		for (OwnedSupport &entry : owned) {
			if (entry.gem == gem && entry.tier == tier) {
				entry.count++;
				return;
			}
		}
		owned.push_back({ gem, tier, 1, inBelt, index });
	};
	for (int i = 0; i < player._pNumInv; i++)
		add(player.InvList[i], false, i);
	for (int i = 0; i < MaxBeltItems; i++)
		add(player.SpdList[i], true, i);
	std::sort(owned.begin(), owned.end(), [](const OwnedSupport &a, const OwnedSupport &b) { return a.gem != b.gem ? a.gem < b.gem : a.tier > b.tier; });
	return owned;
}

/** @brief The Skills panel socket of the popup's slot, if it uses a gem that takes supports. */
std::optional<size_t> PopupSupportsSocket(const Player &player)
{
	const TrayBinding &binding = Binding(player, Popup.slot);
	if (!binding.isGem() || binding.socket >= NumSkillSlots || !SkillTakesSupports(player.poe2.skillSlots[binding.socket]))
		return std::nullopt;
	return binding.socket;
}

/** @brief Why a support gem can't go in a support socket of a skill, or empty if it can. */
std::string SupportBlocker(const Player &player, size_t slot, size_t socket, SupportGem gem, int tier)
{
	const SkillSlot skill = player.poe2.skillSlots[slot];
	if (!SupportGemCanSupport(gem, skill)) {
		const std::string_view needs = SupportGemNeeds(gem);
		return needs.empty() ? FormatRuntime(_("Can't support {:s}"), SkillName(skill)) : std::string(needs);
	}
	const SupportGem current = player.poe2.slotSupports[slot][socket];
	// Another tier of the support in its own socket swaps in.
	if (current == gem ? player.poe2.socketTier(slot, socket) == tier : player.poe2.hasSupport(skill, gem))
		return std::string(_("Already socketed"));
	return {};
}

PopupLayout LayoutPopup(const std::vector<OwnedSupport> &owned)
{
	PopupLayout layout {};
	const int innerWidth = PopupWidth - (2 * PopupPad);
	int y = PopupPad;
	layout.title = { { PopupPad, y }, { innerWidth, 18 } };
	y += 22;
	layout.hasActions = TraySlotTakesActions(Popup.slot);
	if (layout.hasActions) {
		for (size_t i = 0; i < MouseActions.size(); i++)
			layout.actions[i] = { { PopupPad + (static_cast<int>(i) * (ActionButtonSize.width + ActionButtonGap)), y }, ActionButtonSize };
		y += ActionButtonSize.height + 6;
	}
	layout.weaponAttack = { { PopupPad, y }, { innerWidth, ActionButtonSize.height } };
	y += ActionButtonSize.height + 6;
	layout.gemsLabel = { { PopupPad, y }, { innerWidth, 14 } };
	y += 16;
	const int gridWidth = (GemColumns * (SlotSize.width + SlotGap)) - SlotGap;
	const int gridX = (PopupWidth - gridWidth) / 2;
	for (size_t i = 0; i < NumSkillSlots; i++) {
		const int column = static_cast<int>(i) % GemColumns;
		const int row = static_cast<int>(i) / GemColumns;
		layout.gems[i] = { { gridX + (column * (SlotSize.width + SlotGap)), y + (row * (SlotSize.height + SlotGap)) }, SlotSize };
	}
	y += (static_cast<int>(NumSkillSlots / GemColumns) * (SlotSize.height + SlotGap)) + 4;
	layout.supportsLabel = { { PopupPad, y }, { innerWidth, 14 } };
	y += 18;
	for (size_t i = 0; i < MaxSupportSockets; i++)
		layout.sockets[i] = { { PopupPad + (static_cast<int>(i) * (PopupSocketSize + PopupSocketGap)), y }, { PopupSocketSize, PopupSocketSize } };
	y += PopupSocketSize + 6;
	if (Popup.pickerOpen) {
		const int rows = static_cast<int>(std::clamp<size_t>(owned.size(), 1, MaxPickerRows));
		layout.picker = { { PopupPad, y }, { innerWidth, rows * PickerRowHeight } };
		y += layout.picker.size.height + 6;
	}
	const int height = y + PopupPad - 6;

	// Open above the slot, kept on the screen.
	const Rectangle slot = SlotRect(Popup.slot);
	Point origin { slot.Center().x - (PopupWidth / 2), TrayRect().position.y - height - 4 };
	origin.x = std::clamp(origin.x, 0, std::max(static_cast<int>(gnScreenWidth) - PopupWidth, 0));
	origin.y = std::max(origin.y, 0);
	const Displacement offset { origin.x, origin.y };
	layout.box = { origin, { PopupWidth, height } };
	const auto place = [&offset](Rectangle &rect) { rect.position = rect.position + offset; };
	place(layout.title);
	for (Rectangle &rect : layout.actions)
		place(rect);
	place(layout.weaponAttack);
	place(layout.gemsLabel);
	for (Rectangle &rect : layout.gems)
		place(rect);
	place(layout.supportsLabel);
	for (Rectangle &rect : layout.sockets)
		place(rect);
	place(layout.picker);
	return layout;
}

Rectangle PickerRowRect(const PopupLayout &layout, size_t row)
{
	return { layout.picker.position + Displacement { 0, static_cast<int>(row) * PickerRowHeight }, { layout.picker.size.width, PickerRowHeight } };
}

PopupHit PopupHitAt(const Player &player, const PopupLayout &layout, const std::vector<OwnedSupport> &owned, Point position)
{
	if (!Popup.open || !layout.box.contains(position))
		return {};
	if (layout.hasActions) {
		for (size_t i = 0; i < MouseActions.size(); i++) {
			if (layout.actions[i].contains(position))
				return { PopupHit::Kind::Action, i };
		}
	}
	if (layout.weaponAttack.contains(position))
		return { PopupHit::Kind::WeaponAttack };
	for (size_t i = 0; i < NumSkillSlots; i++) {
		if (layout.gems[i].contains(position))
			return { PopupHit::Kind::Gem, i };
	}
	if (PopupSupportsSocket(player)) {
		for (size_t i = 0; i < MaxSupportSockets; i++) {
			if (layout.sockets[i].contains(position))
				return { PopupHit::Kind::Socket, i };
		}
		if (Popup.pickerOpen) {
			for (size_t row = 0; row < std::min(owned.size(), MaxPickerRows); row++) {
				if (PickerRowRect(layout, row).contains(position))
					return { PopupHit::Kind::Picker, row };
			}
		}
	}
	return { PopupHit::Kind::Box };
}

bool IsSkillBarVisible()
{
	// It hides when the left panel covers the spot it moved to for the inventory.
	return IsSkillBarShown() && !(IsLeftPanelOpen() && Overlaps(TrayRect(), GetLeftPanel()));
}

bool IsOverPopup(Point position)
{
	if (!Popup.open)
		return false;
	return LayoutPopup(OwnedSupports(*MyPlayer)).box.contains(position);
}

void DrawSupportSockets(const Surface &out, const Player &player, const PopupLayout &layout, size_t slot, const PopupHit &hover)
{
	const SkillSlot skill = player.poe2.skillSlots[slot];
	const size_t open = SupportSocketCount(player, slot);
	const SupportGem held = SupportGemForItem(player.HoldItem);
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		const Rectangle &rect = layout.sockets[socket];
		const Point center = rect.Center();
		if (socket >= open) {
			FillCircle(out, center, (PopupSocketSize / 2) - 1, SocketLockedColor);
			continue;
		}
		const SupportGem gem = player.poe2.slotSupports[slot][socket];
		const bool picking = Popup.pickerOpen && Popup.pickerSocket == socket;
		uint8_t rim = SocketRimColor;
		if (gem != SupportGem::None)
			rim = GemPaletteColor(SupportGemColor(gem));
		else if (held != SupportGem::None && SupportGemCanSupport(held, skill))
			rim = GemPaletteColor(SupportGemColor(held));
		const bool hovered = hover.kind == PopupHit::Kind::Socket && hover.index == socket;
		FillCircle(out, center, PopupSocketSize / 2, hovered || picking ? HoverColor : rim);
		FillCircle(out, center, (PopupSocketSize / 2) - 2, SocketColor);
		if (gem != SupportGem::None)
			DrawSupportGemIcon(out, rect, gem);
	}
}

void DrawPicker(const Surface &out, const Player &player, const PopupLayout &layout, size_t slot, const std::vector<OwnedSupport> &owned, const PopupHit &hover)
{
	FillRectangle(out, layout.picker, RowColor);
	DrawFrame(out, layout.picker, InnerBorderColor);
	if (owned.empty()) {
		const Rectangle row = PickerRowRect(layout, 0);
		DrawString(out, _("No support gems. Cut some in Gemcutting."), { row.position + Displacement { 4, 0 }, row.size },
		    { .flags = UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
		return;
	}
	for (size_t row = 0; row < std::min(owned.size(), MaxPickerRows); row++) {
		const OwnedSupport &entry = owned[row];
		const Rectangle rect = PickerRowRect(layout, row);
		if (hover.kind == PopupHit::Kind::Picker && hover.index == row)
			FillRectangle(out, rect, RowHoverColor);
		const std::string blocker = SupportBlocker(player, slot, Popup.pickerSocket, entry.gem, entry.tier);
		const Rectangle text { rect.position + Displacement { 4, 0 }, { rect.size.width - 8, rect.size.height } };
		const std::string name = entry.count > 1 ? FormatRuntime("{:s} x{:d}", SupportGemTierName(entry.gem, entry.tier), entry.count) : SupportGemTierName(entry.gem, entry.tier);
		DrawString(out, name, text, { .flags = (blocker.empty() ? GemTextColor(SupportGemColor(entry.gem)) : UiFlags::ColorButtonpushed) | UiFlags::VerticalCenter });
		if (!blocker.empty())
			DrawString(out, blocker, text, { .flags = UiFlags::AlignRight | UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
	}
}

void DrawPopupTooltip(const Surface &out, const Player &player, const PopupHit &hover, const std::vector<OwnedSupport> &owned)
{
	const Rectangle bounds { { 0, 0 }, { out.w(), out.h() } };
	std::vector<std::pair<std::string, UiFlags>> lines;
	const std::optional<size_t> slot = PopupSupportsSocket(player);
	switch (hover.kind) {
	case PopupHit::Kind::Gem: {
		const SkillSlot skill = player.poe2.skillSlots[hover.index];
		if (skill.isEmpty())
			return;
		if (!SkillGoesOnTray(skill))
			lines.emplace_back(_("Spirit gems are active while socketed"), UiFlags::ColorButtonpushed);
		else
			lines.emplace_back(FormatRuntime(_("Click to use it with {:s}"), TraySlotKeyName(Popup.slot)), UiFlags::ColorWhite);
		DrawTooltip(out, bounds, MousePosition, std::string(SkillName(skill)), UiFlags::ColorWhitegold, lines);
		return;
	}
	case PopupHit::Kind::Socket: {
		if (!slot)
			return;
		if (hover.index >= SupportSocketCount(player, *slot)) {
			lines.emplace_back(JewellerSocketHint(hover.index), UiFlags::ColorWhite);
			DrawTooltip(out, bounds, MousePosition, std::string(_("Locked socket")), UiFlags::ColorButtonpushed, lines);
			return;
		}
		const SupportGem gem = player.poe2.slotSupports[*slot][hover.index];
		if (gem == SupportGem::None) {
			lines.emplace_back(_("Click to pick a support gem"), UiFlags::ColorWhite);
			DrawTooltip(out, bounds, MousePosition, std::string(_("Empty support socket")), UiFlags::ColorWhite, lines);
			return;
		}
		const int tier = player.poe2.socketTier(*slot, hover.index);
		lines.emplace_back(std::string(SupportGemDescription(gem, tier)), UiFlags::ColorWhite);
		lines.emplace_back(_("Click to swap it, right-click to take it out"), UiFlags::ColorButtonpushed);
		DrawTooltip(out, bounds, MousePosition, SupportGemTierName(gem, tier), GemTextColor(SupportGemColor(gem)), lines);
		return;
	}
	case PopupHit::Kind::Picker: {
		if (!slot || hover.index >= owned.size())
			return;
		const SupportGem gem = owned[hover.index].gem;
		const int tier = owned[hover.index].tier;
		lines.emplace_back(std::string(SupportGemDescription(gem, tier)), UiFlags::ColorWhite);
		const std::string blocker = SupportBlocker(player, *slot, Popup.pickerSocket, gem, tier);
		lines.emplace_back(blocker.empty() ? std::string(_("Click to socket it")) : blocker, blocker.empty() ? UiFlags::ColorButtonpushed : UiFlags::ColorRed);
		DrawTooltip(out, bounds, MousePosition, SupportGemTierName(gem, tier), GemTextColor(SupportGemColor(gem)), lines);
		return;
	}
	default:
		return;
	}
}

void DrawPopup(const Surface &out, const Player &player)
{
	const std::vector<OwnedSupport> owned = OwnedSupports(player);
	const PopupLayout layout = LayoutPopup(owned);
	const PopupHit hover = ControlMode == ControlTypes::KeyboardAndMouse ? PopupHitAt(player, layout, owned, MousePosition) : PopupHit {};
	const TrayBinding &binding = Binding(player, Popup.slot);

	DrawPanel(out, layout.box);
	DrawString(out, FormatRuntime(_("Skill for {:s}"), TraySlotKeyName(Popup.slot)), layout.title, { .flags = UiFlags::ColorUiGold | UiFlags::VerticalCenter });
	if (layout.hasActions) {
		for (size_t i = 0; i < MouseActions.size(); i++) {
			const Rectangle &button = layout.actions[i];
			const bool current = binding.action == MouseActions[i];
			FillRectangle(out, button, current || (hover.kind == PopupHit::Kind::Action && hover.index == i) ? RowHoverColor : RowColor);
			DrawFrame(out, button, current ? BorderColor : InnerBorderColor);
			DrawString(out, ActionButtonLabel(MouseActions[i]), button,
			    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (current ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
		}
	}

	{
		const bool current = binding.action == TrayAction::WeaponAttack;
		FillRectangle(out, layout.weaponAttack, current || hover.kind == PopupHit::Kind::WeaponAttack ? RowHoverColor : RowColor);
		DrawFrame(out, layout.weaponAttack, current ? BorderColor : InnerBorderColor);
		DrawString(out, FormatRuntime(_("{:s} (no mana)"), DefaultAttackName(DefaultAttackFor(player))), layout.weaponAttack,
		    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (current ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
	}

	DrawString(out, _("Skill gems"), layout.gemsLabel, { .flags = UiFlags::ColorWhite | UiFlags::VerticalCenter });
	for (size_t socket = 0; socket < NumSkillSlots; socket++) {
		const Rectangle &cell = layout.gems[socket];
		const SkillSlot skill = player.poe2.skillSlots[socket];
		FillRectangle(out, cell, RowColor);
		const bool usable = SkillGoesOnTray(skill) && GetSkillUsability(player, skill) == SkillUsability::Ready;
		DrawSkillIcon(out, BottomLeft(cell), skill, usable);
		if (binding.isGem() && binding.socket == socket) {
			DrawFrame(out, cell, BorderColor);
			DrawFrame(out, { cell.position + Displacement { 1, 1 }, { cell.size.width - 2, cell.size.height - 2 } }, BorderColor);
		} else if (hover.kind == PopupHit::Kind::Gem && hover.index == socket && SkillGoesOnTray(skill)) {
			DrawSmallSpellIconBorder(out, BottomLeft(cell));
		}
		const std::string key = SkillSlotKeyName(socket);
		if (!key.empty() && key.length() <= 3)
			DrawString(out, key, { cell.position + Displacement { 2, 1 }, { cell.size.width - 4, 12 } }, { .flags = UiFlags::ColorWhite | UiFlags::Outlined });
	}

	const std::optional<size_t> slot = PopupSupportsSocket(player);
	if (!slot) {
		DrawString(out, binding.isGem() ? _("Spirit gems take no supports") : _("Pick a skill gem to fill its supports"), layout.supportsLabel,
		    { .flags = UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
	} else {
		DrawString(out, FormatRuntime(_("Supports for {:s}"), SkillName(player.poe2.skillSlots[*slot])), layout.supportsLabel,
		    { .flags = UiFlags::ColorWhite | UiFlags::VerticalCenter });
		DrawSupportSockets(out, player, layout, *slot, hover);
		if (Popup.pickerOpen)
			DrawPicker(out, player, layout, *slot, owned, hover);
	}

	if (player.HoldItem.isEmpty())
		DrawPopupTooltip(out, player, hover, owned);
}

/** @brief Takes a support out of the popup skill's socket and puts it in the inventory. */
void TakeOutSupport(Player &player, size_t slot, size_t socket)
{
	int tier = 1;
	const SupportGem gem = UnsocketSupportGem(player.poe2, slot, socket, &tier);
	if (gem == SupportGem::None)
		return;
	GiveSupportGems(player, { { gem, tier } });
	EventPlrMsg(FormatRuntime(_("{:s} is back in your inventory."), SupportGemTierName(gem, tier)));
}

void SocketOwnedSupport(Player &player, size_t slot, const OwnedSupport &entry)
{
	const std::string blocker = SupportBlocker(player, slot, Popup.pickerSocket, entry.gem, entry.tier);
	if (!blocker.empty()) {
		EventPlrMsg(blocker, UiFlags::ColorRed);
		return;
	}
	SupportGem replaced = SupportGem::None;
	int replacedTier = 1;
	const SupportSocketResult result = SocketSupportGem(player, slot, Popup.pickerSocket, entry.gem, replaced, entry.tier, &replacedTier);
	if (!IsAnyOf(result, SupportSocketResult::Socketed, SupportSocketResult::Replaced)) {
		EventPlrMsg(SupportSocketMessage(result, player, slot, entry.gem), UiFlags::ColorRed);
		return;
	}
	if (entry.inBelt) {
		PlayGemSound(player.SpdList[entry.index]);
		player.RemoveSpdBarItem(entry.index);
	} else {
		PlayGemSound(player.InvList[entry.index]);
		player.RemoveInvItem(entry.index);
	}
	if (replaced != SupportGem::None)
		GiveSupportGems(player, { { replaced, replacedTier } });
	EventPlrMsg(SupportSocketMessage(result, player, slot, entry.gem));
	Popup.pickerOpen = false;
}

void ClickPopup(Player &player, const PopupHit &hit, const std::vector<OwnedSupport> &owned)
{
	const std::optional<size_t> slot = PopupSupportsSocket(player);
	switch (hit.kind) {
	case PopupHit::Kind::Action:
		BindTraySlot(player, Popup.slot, { MouseActions[hit.index], 0 });
		CloseSkillTrayPopup();
		return;
	case PopupHit::Kind::WeaponAttack:
		BindTraySlot(player, Popup.slot, { TrayAction::WeaponAttack, 0 });
		CloseSkillTrayPopup();
		return;
	case PopupHit::Kind::Gem: {
		const SkillSlot skill = player.poe2.skillSlots[hit.index];
		if (skill.isEmpty()) {
			EventPlrMsg(_("That socket is empty. Socket skill gems in the Skills panel (K)."), UiFlags::ColorRed);
			return;
		}
		if (!SkillGoesOnTray(skill)) {
			EventPlrMsg(FormatRuntime(_("{:s} is always active while socketed."), SkillName(skill)));
			return;
		}
		// Stay open so the supports can be filled.
		BindTraySlot(player, Popup.slot, TrayBinding::ForGem(hit.index));
		Popup.pickerOpen = false;
		return;
	}
	case PopupHit::Kind::Socket: {
		if (!slot)
			return;
		if (hit.index >= SupportSocketCount(player, *slot)) {
			EventPlrMsg(JewellerSocketHint(hit.index) + ".", UiFlags::ColorRed);
			return;
		}
		if (!player.HoldItem.isEmpty()) {
			if (SupportGemForItem(player.HoldItem) == SupportGem::None)
				EventPlrMsg(_("Only support gems go in support sockets."), UiFlags::ColorRed);
			else
				SocketHeldSupportGem(player, *slot, hit.index);
			return;
		}
		if (Popup.pickerOpen && Popup.pickerSocket == hit.index) {
			Popup.pickerOpen = false;
			return;
		}
		Popup.pickerOpen = true;
		Popup.pickerSocket = hit.index;
		return;
	}
	case PopupHit::Kind::Picker:
		if (slot && hit.index < owned.size())
			SocketOwnedSupport(player, *slot, owned[hit.index]);
		return;
	default:
		return;
	}
}

/** @brief A gem dropped on a tray slot: a skill gem is socketed and put on the slot, a support goes into the slot's skill. */
void DropOnTraySlot(Player &player, TraySlot traySlot)
{
	const TrayBinding binding = Binding(player, traySlot);
	if (IsSkillGem(player.HoldItem)) {
		const SkillSlot skill = SkillForGem(player.HoldItem);
		if (FindSkillSlot(player, skill) || player.poe2.metaSocketOf(skill)) {
			EventPlrMsg(FormatRuntime(_("{:s} is already in the Skills panel."), SkillName(skill)), UiFlags::ColorRed);
			return;
		}
		// A skill gem on a slot with a gem takes that gem's socket; Spirit gems just go in a free socket.
		std::optional<size_t> socket;
		if (binding.isGem() && SkillGoesOnTray(skill))
			socket = binding.socket;
		else
			socket = FirstFreeSkillSlot(player);
		if (!socket) {
			EventPlrMsg(_("The Skills panel is full. Open it (K) to swap a gem out."), UiFlags::ColorRed);
			return;
		}
		PlayGemSound(player.HoldItem);
		const int previousLevel = player.poe2.gemLevel(*socket);
		const SkillSlot previous = ChangeSlotSkill(player, *socket, skill, SkillGemItemLevel(player.HoldItem));
		player.HoldItem.clear();
		if (SkillGoesOnTray(skill))
			BindTraySlot(player, traySlot, TrayBinding::ForGem(*socket));
		Item returned;
		if (!previous.isEmpty() && CreateSkillGemItem(returned, previous, previousLevel))
			HoldItem(player, std::move(returned));
		else
			NewCursor(CURSOR_HAND);
		return;
	}
	if (SupportGemForItem(player.HoldItem) != SupportGem::None) {
		if (!binding.isGem()) {
			EventPlrMsg(_("Put a skill on this slot first, then its supports."), UiFlags::ColorRed);
			return;
		}
		SocketHeldSupportGem(player, binding.socket, std::nullopt);
		return;
	}
	EventPlrMsg(_("Only skill gems and support gems go on the skill tray."), UiFlags::ColorRed);
}

void SocketHeldSkillGem(Player &player, size_t slot)
{
	const SkillSlot skill = SkillForGem(player.HoldItem);
	if (const std::optional<size_t> existing = FindSkillSlot(player, skill); existing && *existing != slot) {
		EventPlrMsg(FormatRuntime(_("{:s} is already in the Skills panel."), SkillName(skill)), UiFlags::ColorRed);
		return;
	}
	// A spell inside a meta gem is in no other socket.
	if (const auto meta = player.poe2.metaSocketOf(skill)) {
		EventPlrMsg(FormatRuntime(_("{:s} is already inside {:s}."), SkillName(skill), SkillName(player.poe2.skillSlots[meta->first])), UiFlags::ColorRed);
		return;
	}

	PlayGemSound(player.HoldItem);
	const int previousLevel = player.poe2.gemLevel(slot);
	const SkillSlot previous = ChangeSlotSkill(player, slot, skill, SkillGemItemLevel(player.HoldItem));
	player.HoldItem.clear();
	AutoBindSocket(player, slot);
	Item returned;
	if (!previous.isEmpty() && CreateSkillGemItem(returned, previous, previousLevel))
		HoldItem(player, std::move(returned));
	else
		NewCursor(CURSOR_HAND);
}

/** @brief Whether a click there belongs to a panel, so it shouldn't close the popup. */
bool IsOverPanel(Point position)
{
	return GetMainPanel().contains(position)
	    || (IsLeftPanelOpen() && GetLeftPanel().contains(position))
	    || (IsRightPanelOpen() && GetRightPanel().contains(position));
}

} // namespace

void GiveSupportGems(Player &player, const std::vector<TieredSupport> &gems)
{
	for (const TieredSupport &gem : gems) {
		Item item;
		if (CreateSupportGemItem(item, gem.gem, gem.tier))
			GiveItemToPlayer(player, item);
	}
}

SkillSlot ChangeSlotSkill(Player &player, size_t slot, SkillSlot skill, int level)
{
	// Supports stay socketed if the new skill can use them, like moving them over in POE2; the rest come back out.
	std::vector<TieredSupport> returned;
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		const SupportGem gem = player.poe2.slotSupports[slot][socket];
		if (gem == SupportGem::None || SupportGemCanSupport(gem, skill))
			continue;
		int tier = 1;
		UnsocketSupportGem(player.poe2, slot, socket, &tier);
		returned.push_back({ gem, tier });
	}
	// The spells inside a meta gem come out with it.
	if (IsMetaGem(player.poe2.skillSlots[slot]) && player.poe2.skillSlots[slot] != skill)
		ReturnMetaSkills(player, slot);
	const SkillSlot previous = SocketSkill(player, slot, skill, level);
	GiveSupportGems(player, returned);
	return previous;
}

std::string SupportSocketMessage(SupportSocketResult result, const Player &player, size_t slot, SupportGem gem)
{
	const SkillSlot skill = RowSkill(player, slot);
	switch (result) {
	case SupportSocketResult::NoSkill:
		return std::string(_("Socket a skill gem in this slot first."));
	case SupportSocketResult::NotApplicable:
		return FormatRuntime(_("{:s} can't support {:s}."), SupportGemName(gem), SkillName(skill));
	case SupportSocketResult::AlreadySocketed:
		return FormatRuntime(_("{:s} already supports {:s}."), SupportGemName(gem), SkillName(skill));
	case SupportSocketResult::Locked:
		return std::string(_("That socket is locked: a Jeweller's Orb opens it."));
	case SupportSocketResult::NoFreeSocket:
		return FormatRuntime(_("{:s} has no free support socket."), SkillName(skill));
	case SupportSocketResult::PowerTaken:
		return FormatRuntime(_("{:s} already has a Power support ({:s}); a skill takes only one."), SkillName(skill), SupportGemName(OtherPowerSupport(player.poe2.supportsAt(slot), std::nullopt)));
	case SupportSocketResult::Conflict:
		return FormatRuntime(_("{:s} clashes with a support {:s} already has."), SupportGemName(gem), SkillName(skill));
	case SupportSocketResult::LineageInUse:
		return FormatRuntime(_("{:s} is a lineage support and already in use on another skill."), SupportGemName(gem));
	case SupportSocketResult::SpellInSocket:
		return std::string(_("A spell is in that socket: take it out first."));
	default:
		return FormatRuntime(_("{:s} now supports {:s}."), SupportGemName(gem), SkillName(skill));
	}
}

bool SocketHeldSupportGem(Player &player, size_t slot, std::optional<size_t> socket)
{
	const SupportGem gem = SupportGemForItem(player.HoldItem);
	SupportGem replaced = SupportGem::None;
	int replacedTier = 1;
	const SupportSocketResult result = SocketSupportGem(player, slot, socket, gem, replaced, SupportGemItemTier(player.HoldItem), &replacedTier);
	if (!IsAnyOf(result, SupportSocketResult::Socketed, SupportSocketResult::Replaced)) {
		EventPlrMsg(SupportSocketMessage(result, player, slot, gem), UiFlags::ColorRed);
		return false;
	}

	PlayGemSound(player.HoldItem);
	player.HoldItem.clear();
	EventPlrMsg(SupportSocketMessage(result, player, slot, gem));
	Item returned;
	if (replaced != SupportGem::None && CreateSupportGemItem(returned, replaced, replacedTier))
		HoldItem(player, std::move(returned));
	else
		NewCursor(CURSOR_HAND);
	return true;
}

bool IsSkillBarShown()
{
	// The speedbook opens in the same spot, so hide the tray while it's up.
	return gbIsPoe2 && MyPlayer != nullptr && !SpellSelectFlag;
}

bool IsSkillBarHovered()
{
	if (!IsSkillBarVisible() || ControlMode != ControlTypes::KeyboardAndMouse)
		return false;
	return TrayRect().contains(MousePosition) || IsOverPopup(MousePosition);
}

bool IsSkillTrayPopupOpen()
{
	return Popup.open;
}

void CloseSkillTrayPopup()
{
	Popup = {};
}

void DrawSkillBar(const Surface &out)
{
	if (!IsSkillBarVisible()) {
		CloseSkillTrayPopup();
		return;
	}

	const Player &player = *MyPlayer;
	const bool mouse = ControlMode == ControlTypes::KeyboardAndMouse && !IsOverPopup(MousePosition);
	const bool inTown = leveltype == DTYPE_TOWN;

	const Rectangle portal = CellRect(PortalCell);
	DrawHalfTransparentRectTo(out, portal.position.x, portal.position.y, portal.size.width, portal.size.height);
	SetSpellTrans(inTown ? SpellType::Invalid : SpellType::Spell);
	DrawSmallSpellIcon(out, BottomLeft(portal), SpellID::TownPortal);
	if (mouse && !inTown && portal.contains(MousePosition))
		DrawSmallSpellIconBorder(out, BottomLeft(portal));

	const std::optional<TraySlot> hovered = mouse ? TraySlotAt(MousePosition) : std::nullopt;
	for (size_t i = 0; i < NumTraySlots; i++) {
		const auto slot = static_cast<TraySlot>(i);
		const Rectangle rect = SlotRect(slot);
		const Point bottomLeft = BottomLeft(rect);
		const TrayBinding &binding = Binding(player, slot);

		DrawHalfTransparentRectTo(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height);
		switch (binding.action) {
		case TrayAction::Gem: {
			const SkillSlot skill = player.poe2.skillSlots[binding.socket];
			// A Spirit gem used from the tray greys out until using it would do something.
			const bool spiritUse = skill.kind == SkillKind::Persistent && IsSpiritUseGem(skill.persistent());
			DrawSkillIcon(out, bottomLeft, skill, GetSkillUsability(player, skill) == SkillUsability::Ready && (!spiritUse || SpiritGemReady(player, skill.persistent())));
			break;
		}
		case TrayAction::ReadiedSpell:
			if (IsValidSpell(player._pRSpell)) {
				SetSpellTrans(player._pRSplType);
				DrawSmallSpellIcon(out, bottomLeft, player._pRSpell);
			} else {
				DrawString(out, _("Spell"), rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });
			}
			break;
		case TrayAction::MoveAndAttack:
			DrawString(out, _("Move"), rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
			break;
		case TrayAction::Interact:
			DrawString(out, _("Use"), rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
			break;
		case TrayAction::WeaponAttack:
			DrawString(out, DefaultAttackLabel(DefaultAttackFor(player)), rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
			break;
		default:
			break;
		}
		if (hovered == slot || (Popup.open && Popup.slot == slot))
			DrawSmallSpellIconBorder(out, bottomLeft);

		const std::string key = TraySlotKeyName(slot);
		if (key.length() <= 3)
			DrawString(out, key, { rect.position + Displacement { 2, 1 }, { rect.size.width - 4, 12 } }, { .flags = UiFlags::ColorWhite | UiFlags::Outlined });
	}

	if (Popup.open)
		DrawPopup(out, player);
}

void SetSkillBarInfo()
{
	InfoString = StringOrView {};
	if (IsOverPopup(MousePosition))
		return;

	const Player &player = *MyPlayer;
	if (CellRect(PortalCell).contains(MousePosition)) {
		InfoColor = UiFlags::ColorBlue;
		InfoString = _("Town Portal");
		AddInfoBoxString(leveltype == DTYPE_TOWN ? _("You're in town") : _("Opens a portal to town, for free"));
		return;
	}

	const std::optional<TraySlot> slot = TraySlotAt(MousePosition);
	if (!slot)
		return;
	const TrayBinding &binding = Binding(player, *slot);
	const std::string key = TraySlotKeyName(*slot);
	if (!binding.isGem()) {
		InfoColor = UiFlags::ColorWhite;
		if (binding.action == TrayAction::None) {
			InfoString = FormatRuntime(_("Empty tray slot [{:s}]"), key);
		} else if (binding.action == TrayAction::ReadiedSpell && IsValidSpell(player._pRSpell)) {
			InfoString = FormatRuntime("{:s} [{:s}]", pgettext("spell", GetSpellData(player._pRSpell).sNameText), key);
		} else if (binding.action == TrayAction::WeaponAttack) {
			InfoString = FormatRuntime("{:s} [{:s}]", DefaultAttackName(DefaultAttackFor(player)), key);
			AddInfoBoxString(_("Your weapon's attack, no mana cost"));
		} else {
			InfoString = FormatRuntime("{:s} [{:s}]", ActionName(binding.action), key);
		}
		AddInfoBoxString(_("Click to pick a skill"));
		return;
	}

	const SkillSlot skill = player.poe2.skillSlots[binding.socket];
	InfoColor = skill.kind == SkillKind::Spell ? UiFlags::ColorBlue : UiFlags::ColorWhitegold;
	InfoString = FormatRuntime(_("{:s} [{:s}]"), SkillName(skill), key);
	if (skill.kind == SkillKind::Spell) {
		AddInfoBoxString(FormatRuntime(_("Spell level {:d}, {:d} mana"), player.GetSpellLevel(skill.spell()), SkillManaCost(player, skill)));
	} else {
		if (WeaponSkillNeedsWeapon(skill.attack()))
			AddInfoBoxString(FormatRuntime(_("{:d} mana, needs a {:s}"), SkillManaCost(player, skill), WeaponSkillWeapons(skill.attack())));
		else
			AddInfoBoxString(FormatRuntime(_("{:d} mana"), SkillManaCost(player, skill)));
	}
	std::string supports;
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		const SupportGem gem = player.poe2.slotSupports[binding.socket][socket];
		if (gem != SupportGem::None)
			StrAppend(supports, supports.empty() ? "" : ", ", SupportGemTierName(gem, player.poe2.socketTier(binding.socket, socket)));
	}
	if (!supports.empty())
		AddInfoBoxString(FormatRuntime(_("Supports: {:s}"), supports));

	switch (GetSkillUsability(player, skill)) {
	case SkillUsability::Requirements:
		InfoColor = UiFlags::ColorRed;
		AddInfoBoxString(MissingRequirements(player, skill));
		break;
	case SkillUsability::Weapon:
		InfoColor = UiFlags::ColorRed;
		break;
	default:
		AddInfoBoxString(_("Click to change, right-click to clear"));
		break;
	}
}

bool HandleSkillBarClick(Point mousePosition)
{
	if (!IsSkillBarVisible()) {
		CloseSkillTrayPopup();
		return false;
	}

	Player &player = *MyPlayer;
	if (Popup.open) {
		const std::vector<OwnedSupport> owned = OwnedSupports(player);
		const PopupLayout layout = LayoutPopup(owned);
		const PopupHit hit = PopupHitAt(player, layout, owned, mousePosition);
		if (hit.kind != PopupHit::Kind::None) {
			ClickPopup(player, hit, owned);
			return true;
		}
		// A click in the world closes it; panels stay usable, e.g. to pick up a gem.
		if (!TrayRect().contains(mousePosition) && !IsOverPanel(mousePosition)) {
			CloseSkillTrayPopup();
			return true;
		}
	}

	if (!TrayRect().contains(mousePosition))
		return false;
	if (CellRect(PortalCell).contains(mousePosition)) {
		if (player.HoldItem.isEmpty())
			UseFreeTownPortal();
		return true;
	}
	const std::optional<TraySlot> slot = TraySlotAt(mousePosition);
	if (!slot)
		return true;
	if (!player.HoldItem.isEmpty()) {
		DropOnTraySlot(player, *slot);
		return true;
	}
	if (Popup.open && Popup.slot == *slot)
		CloseSkillTrayPopup();
	else
		Popup = { true, *slot, false, 0 };
	return true;
}

bool HandleSkillBarRightClick(Point mousePosition)
{
	// The Skills panel's Spirit gems switch on and off with a right-click.
	if (HandleSkillsPanelRightClick(mousePosition))
		return true;
	if (!IsSkillBarVisible())
		return false;

	Player &player = *MyPlayer;
	if (Popup.open) {
		const std::vector<OwnedSupport> owned = OwnedSupports(player);
		const PopupLayout layout = LayoutPopup(owned);
		const PopupHit hit = PopupHitAt(player, layout, owned, mousePosition);
		const std::optional<size_t> slot = PopupSupportsSocket(player);
		if (hit.kind == PopupHit::Kind::Socket && slot && hit.index < SupportSocketCount(player, *slot)) {
			TakeOutSupport(player, *slot, hit.index);
			return true;
		}
		if (hit.kind != PopupHit::Kind::None) {
			CloseSkillTrayPopup();
			return true;
		}
	}

	if (!TrayRect().contains(mousePosition))
		return false;
	if (const std::optional<TraySlot> slot = TraySlotAt(mousePosition); slot) {
		ClearTraySlot(player, *slot);
		if (Popup.open && Popup.slot == *slot)
			Popup.pickerOpen = false;
	}
	return true;
}

void ClickSkillSlot(Player &player, size_t slot, bool canTakeOut)
{
	if (!player.HoldItem.isEmpty()) {
		if (IsSkillGem(player.HoldItem))
			SocketHeldSkillGem(player, slot);
		else if (SupportGemForItem(player.HoldItem) != SupportGem::None)
			SocketHeldSupportGem(player, slot, std::nullopt);
		else
			EventPlrMsg(_("Only skill gems and support gems go in the Skills panel."), UiFlags::ColorRed);
		return;
	}

	const SkillSlot skill = player.poe2.skillSlots[slot];
	if (canTakeOut && !skill.isEmpty()) {
		Item gem;
		if (CreateSkillGemItem(gem, skill, player.poe2.gemLevel(slot))) {
			ChangeSlotSkill(player, slot, {});
			PlayGemSound(gem);
			HoldItem(player, std::move(gem));
		}
	}
}

void DrawSkillIcon(const Surface &out, Point bottomLeft, SkillSlot skill, bool ready)
{
	if (skill.isEmpty())
		return;
	SetSpellTrans(!ready ? SpellType::Invalid : skill.kind == SkillKind::Spell ? SpellType::Spell
	                                                                           : SpellType::Skill);
	if (skill.kind == SkillKind::Spell)
		DrawSmallSpellIcon(out, bottomLeft, skill.spell());
	else if (skill.kind == SkillKind::Persistent)
		DrawSmallSpellIcon(out, bottomLeft, PersistentIcon(skill.persistent()));
	else
		DrawSmallSpellIcon(out, bottomLeft, AttackIcon(skill.attack()));
}

std::string TraySlotKeyName(TraySlot slot)
{
	switch (slot) {
	case TraySlot::LeftMouse:
		return std::string(_("LMB"));
	case TraySlot::RightMouse:
		return std::string(_("RMB"));
	case TraySlot::MiddleMouse: {
		const std::string name { GetOptions().Keymapper.KeyNameForAction("TraySlotMiddle") };
		return name == "MMOUSE" ? std::string(_("MMB")) : name;
	}
	default:
		return std::string(GetOptions().Keymapper.KeyNameForAction(StrCat("TraySlot", static_cast<size_t>(slot) - static_cast<size_t>(TraySlot::Key1) + 1)));
	}
}

std::string SkillSlotKeyName(size_t slot)
{
	if (MyPlayer == nullptr)
		return {};
	const std::optional<TraySlot> traySlot = TraySlotForSocket(*MyPlayer, slot);
	return traySlot ? TraySlotKeyName(*traySlot) : std::string {};
}

std::string MissingSkillRequirements(const Player &player, SkillSlot skill)
{
	return MissingRequirements(player, skill);
}

namespace {

/** @brief Casts the spells inside an invocation at the cursor, if it has the Energy, or places a Horadric Totem there. */
void UseMetaGemFromTray(Player &player, size_t slot)
{
	const SkillSlot skill = player.poe2.skillSlots[slot];
	const bool totem = skill.persistent() == PersistentSkill::HoradricTotem;
	switch (totem ? PlaceHoradricTotem(player, slot, cursPosition) : InvokeMetaGem(player, slot, cursPosition)) {
	case InvokeResult::NoSpells:
		EventPlrMsg(FormatRuntime(_("{:s} has no spells inside. Put spell gems in its sockets (K)."), SkillName(skill)), UiFlags::ColorRed);
		break;
	case InvokeResult::Town:
		EventPlrMsg(_("Spells don't cast in town."), UiFlags::ColorRed);
		break;
	case InvokeResult::NotEnoughEnergy:
		EventPlrMsg(FormatRuntime(_("{:s} has {:d} Energy; casting takes {:d}."), SkillName(skill), MetaEnergy(player, slot), MetaMaxEnergy(player.poe2, slot)), UiFlags::ColorRed);
		break;
	case InvokeResult::NoMana:
		EventPlrMsg(FormatRuntime(_("{:s} needs {:d} mana."), SkillName(skill), HoradricTotemManaCost(player.poe2.gemLevel(slot))), UiFlags::ColorRed);
		break;
	case InvokeResult::Off:
		EventPlrMsg(FormatRuntime(_("{:s} is switched off. Right-click it in the Skills panel (K)."), SkillName(skill)), UiFlags::ColorRed);
		break;
	default:
		break;
	}
}

/** @brief Releases the gem's store, starts its buff or plants its banner, or says why it can't. */
void UseSpiritGemFromTray(Player &player, PersistentSkill skill)
{
	const std::string_view name = SkillName(SkillSlot::ForPersistent(skill));
	switch (UseSpiritGem(player, skill)) {
	case SpiritUseResult::Off:
		EventPlrMsg(FormatRuntime(_("{:s} is switched off. Right-click it in the Skills panel (K)."), name), UiFlags::ColorRed);
		break;
	case SpiritUseResult::Town:
		EventPlrMsg(FormatRuntime(_("{:s} can't be used in town."), name), UiFlags::ColorRed);
		break;
	case SpiritUseResult::Empty:
		if (skill == PersistentSkill::GrimFeast)
			EventPlrMsg(_("Grim Feast hasn't stored enough life to raise a skeleton. Walk over the remnants of fallen ones."), UiFlags::ColorRed);
		else if (skill == PersistentSkill::IronWard)
			EventPlrMsg(_("Iron Ward has nothing stored. It stores the damage your armour stops."), UiFlags::ColorRed);
		else
			EventPlrMsg(_("Plague Bearer has nothing stored. It stores part of the poison damage you deal."), UiFlags::ColorRed);
		break;
	case SpiritUseResult::NotEnoughGlory: {
		const SpiritStore glory = SpiritGemStore(player, skill);
		EventPlrMsg(FormatRuntime(_("{:s} has {:d} Glory; planting it takes {:d}. Attack hits build Glory."), name, glory.value, glory.cap), UiFlags::ColorRed);
		break;
	}
	case SpiritUseResult::Cooldown:
		EventPlrMsg(FormatRuntime(_("{:s} isn't ready yet."), name), UiFlags::ColorRed);
		break;
	case SpiritUseResult::NoMana:
		EventPlrMsg(FormatRuntime(_("{:s} needs {:d} mana."), name, ConvalescenceManaCost), UiFlags::ColorRed);
		break;
	case SpiritUseResult::ShieldFull:
		EventPlrMsg(_("Your energy shield is already full."), UiFlags::ColorRed);
		break;
	case SpiritUseResult::NothingToRaise:
		EventPlrMsg(_("None of your skeletons are waiting to rise."), UiFlags::ColorRed);
		break;
	default:
		break;
	}
}

} // namespace

void UseSkillSlot(size_t slot)
{
	if (!IsSkillBarShown() || slot >= NumSkillSlots)
		return;

	Player &player = *MyPlayer;
	const SkillSlot skill = player.poe2.skillSlots[slot];
	// Like POE2, an attack whose weapon is in the other set swaps to it.
	if (skill.kind == SkillKind::Attack && GetSkillUsability(player, skill) == SkillUsability::Weapon && OtherWeaponSetAllowsSkill(player, skill.attack()))
		SwapWeapons(player);
	switch (GetSkillUsability(player, skill)) {
	case SkillUsability::Empty:
		EventPlrMsg(_("No skill gem in that socket. Socket one in the Skills panel (K)."), UiFlags::ColorRed);
		return;
	case SkillUsability::Requirements:
		EventPlrMsg(FormatRuntime(_("{:s}: {:s}"), SkillName(skill), MissingRequirements(player, skill)), UiFlags::ColorRed);
		return;
	default:
		break;
	}

	if (skill.kind == SkillKind::Persistent) {
		if (GetSkillUsability(player, skill) == SkillUsability::Spirit)
			EventPlrMsg(FormatRuntime(_("{:s} needs {:d} Spirit; you have {:d} left."), SkillName(skill),
			                IsMinionCountSkill(skill.persistent()) ? SpiritPerMinion(player, skill.persistent()) : GemSpiritCost(player, skill.persistent()), AvailableSpirit(player)),
			    UiFlags::ColorRed);
		else if (IsInvocationGem(skill) || skill.persistent() == PersistentSkill::HoradricTotem)
			UseMetaGemFromTray(player, slot);
		else if (IsSpiritUseGem(skill.persistent()))
			UseSpiritGemFromTray(player, skill.persistent());
		else
			EventPlrMsg(FormatRuntime(_("{:s} is always active while socketed."), SkillName(skill)));
		return;
	}
	if (skill.kind == SkillKind::Attack) {
		TryUseWeaponSkill(player, skill.attack());
		return;
	}

	const SpellID spell = skill.spell();
	const PlayerActionType previousAction = LastPlayerAction;
	if (ControlMode != ControlTypes::KeyboardAndMouse)
		UpdateSpellTarget(spell);
	CheckPlrSpell(false, spell, SpellType::Spell);
	LastPlayerAction = previousAction;
}

void UseTraySlot(TraySlot slot)
{
	if (!IsSkillBarShown())
		return;
	const TrayBinding &binding = Binding(*MyPlayer, slot);
	switch (binding.action) {
	case TrayAction::Gem:
		UseSkillSlot(binding.socket);
		return;
	case TrayAction::WeaponAttack:
		UseDefaultAttack(*MyPlayer);
		return;
	case TrayAction::None:
		EventPlrMsg(FormatRuntime(_("Nothing on {:s}. Click its tray slot to pick a skill."), TraySlotKeyName(slot)), UiFlags::ColorRed);
		return;
	default:
		// Moving, interacting and the readied spell happen through the mouse click itself.
		return;
	}
}

namespace {

/** The tray slot whose key or button is held down, most recent press first. */
std::optional<TraySlot> HeldTraySlot;

} // namespace

void PressTraySlot(TraySlot slot)
{
	UseTraySlot(slot);
	if (IsSkillBarShown() && IsAnyOf(Binding(*MyPlayer, slot).action, TrayAction::Gem, TrayAction::WeaponAttack))
		HeldTraySlot = slot;
}

void ReleaseTraySlot(TraySlot slot)
{
	if (HeldTraySlot == slot)
		HeldTraySlot = std::nullopt;
}

void ReleaseAllTraySlots()
{
	HeldTraySlot = std::nullopt;
}

void RepeatHeldTraySlot()
{
	if (!HeldTraySlot || MyPlayer == nullptr)
		return;
	if (!IsSkillBarShown() || pcurs != CURSOR_HAND || IsPlayerInStore() || leveltype == DTYPE_TOWN) {
		HeldTraySlot = std::nullopt;
		return;
	}
	Player &player = *MyPlayer;
	if (player.destAction != ACTION_NONE || player._pInvincible || !player.CanChangeAction())
		return;
	const TrayBinding &binding = Binding(player, *HeldTraySlot);
	if (binding.action == TrayAction::WeaponAttack) {
		UseDefaultAttack(player);
		return;
	}
	if (binding.action != TrayAction::Gem || binding.socket >= NumSkillSlots)
		return;
	const SkillSlot skill = player.poe2.skillSlots[binding.socket];
	// Out of mana or missing a weapon: wait quietly rather than repeating the warning every tick.
	if (skill.kind == SkillKind::Persistent || GetSkillUsability(player, skill) != SkillUsability::Ready)
		return;
	UseSkillSlot(binding.socket);
}

void UseFreeTownPortal()
{
	if (!IsSkillBarShown())
		return;
	if (leveltype == DTYPE_TOWN) {
		EventPlrMsg(_("You're already in town."), UiFlags::ColorRed);
		return;
	}
	const Player &player = *MyPlayer;
	// Cast as a skill, which costs no mana and needs no scroll; the portal opens in front of the character.
	const Point target = player.position.tile + player._pdir;
	NetSendCmdLocParam3(true, CMD_SPELLXY, target, static_cast<int8_t>(SpellID::TownPortal), static_cast<uint8_t>(SpellType::Skill), 0);
}

bool SocketSkillGemFromInventory(Player &player, bool fromBelt, int index)
{
	const Item &gem = fromBelt ? player.SpdList[index] : player.InvList[index];
	const SkillSlot skill = SkillForGem(gem);
	if (FindSkillSlot(player, skill)) {
		EventPlrMsg(FormatRuntime(_("{:s} is already in the Skills panel."), SkillName(skill)), UiFlags::ColorRed);
		return true;
	}
	const std::optional<size_t> slot = FirstFreeSkillSlot(player);
	if (!slot) {
		EventPlrMsg(_("The Skills panel is full. Open it (K) to swap a gem out."), UiFlags::ColorRed);
		return false;
	}

	PlayGemSound(gem);
	ChangeSlotSkill(player, *slot, skill, SkillGemItemLevel(gem));
	if (!SkillGoesOnTray(skill))
		EventPlrMsg(FormatRuntime(_("{:s} socketed. It's active while socketed."), SkillName(skill)));
	else if (const std::optional<TraySlot> traySlot = AutoBindSocket(player, *slot); traySlot)
		EventPlrMsg(FormatRuntime(_("{:s} socketed. Use it with {:s}."), SkillName(skill), TraySlotKeyName(*traySlot)));
	else
		EventPlrMsg(FormatRuntime(_("{:s} socketed. Click a tray slot to use it."), SkillName(skill)));
	if (fromBelt)
		player.RemoveSpdBarItem(index);
	else
		player.RemoveInvItem(index);
	return true;
}

} // namespace devilution
