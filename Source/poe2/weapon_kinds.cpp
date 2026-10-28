#include "poe2/weapon_kinds.hpp"

#include <functional>
#include <string_view>
#include <vector>

#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "tables/itemdat.h"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

/** Each row's kind in POE2 mode; empty outside it. */
std::vector<WeaponKind> Kinds;

/** The kind without base metadata: what every row was before the rework, by its item data alone. */
WeaponKind LegacyKind(const ItemData &data)
{
	if (data.iClass != ICLASS_WEAPON)
		return WeaponKind::None;
	// A sceptre is known by its name, as ItemGrantsSpirit always did.
	if (std::string_view(data.iName).ends_with("Sceptre") || data.iSName == "Sceptre")
		return WeaponKind::LegacySceptre;
	if (data.itype == ItemType::Staff) {
		if (gbIsPoe2 && data.iSName == "Quarterstaff")
			return WeaponKind::Quarterstaff;
		return WeaponKind::LegacyStaff;
	}
	return WeaponKind::Martial;
}

WeaponKind KindOfRow(int itemIndex)
{
	const ItemData &data = AllItemsList[static_cast<size_t>(itemIndex)];
	if (data.iClass == ICLASS_WEAPON) {
		if (const BaseMeta *base = BaseMetaFor(itemIndex); base != nullptr) {
			if (base->itemClass == "staff")
				return WeaponKind::CasterStaff;
			if (base->itemClass == "wand")
				return WeaponKind::Wand;
			if (base->itemClass == "sceptre")
				return WeaponKind::Sceptre;
		}
	}
	return LegacyKind(data);
}

} // namespace

void LoadWeaponKinds()
{
	Kinds.clear();
	if (!gbIsPoe2)
		return;
	Kinds.reserve(AllItemsList.size());
	for (size_t i = 0; i < AllItemsList.size(); i++)
		Kinds.push_back(KindOfRow(static_cast<int>(i)));
}

WeaponKind WeaponKindOf(int itemIndex)
{
	if (itemIndex < 0 || static_cast<size_t>(itemIndex) >= AllItemsList.size())
		return WeaponKind::None;
	if (gbIsPoe2 && static_cast<size_t>(itemIndex) < Kinds.size())
		return Kinds[static_cast<size_t>(itemIndex)];
	return LegacyKind(AllItemsList[static_cast<size_t>(itemIndex)]);
}

WeaponKind WeaponKindOf(const Item &item)
{
	if (item.isEmpty())
		return WeaponKind::None;
	if (item.IDidx < 0 || static_cast<size_t>(item.IDidx) >= AllItemsList.size()) {
		// An item without a row (made by hand): its type says what it was before the rework.
		if (item._itype == ItemType::Staff)
			return WeaponKind::LegacyStaff;
		return IsAnyOf(item._itype, ItemType::Sword, ItemType::Axe, ItemType::Mace, ItemType::Bow) ? WeaponKind::Martial : WeaponKind::None;
	}
	return WeaponKindOf(item.IDidx);
}

bool IsCasterWeapon(int itemIndex)
{
	const WeaponKind kind = WeaponKindOf(itemIndex);
	return kind == WeaponKind::CasterStaff || kind == WeaponKind::Wand || kind == WeaponKind::Sceptre;
}

bool IsCasterWeapon(const Item &item)
{
	return !item.isEmpty() && IsCasterWeapon(item.IDidx);
}

bool IsWand(const Item &item)
{
	return WeaponKindOf(item) == WeaponKind::Wand;
}

bool IsNewSceptre(const Item &item)
{
	return WeaponKindOf(item) == WeaponKind::Sceptre;
}

bool IsAnySceptre(const Item &item)
{
	const WeaponKind kind = WeaponKindOf(item);
	return kind == WeaponKind::Sceptre || kind == WeaponKind::LegacySceptre;
}

bool IsCasterStaff(const Item &item)
{
	return WeaponKindOf(item) == WeaponKind::CasterStaff;
}

bool IsQuarterstaff(const Item &item)
{
	return WeaponKindOf(item) == WeaponKind::Quarterstaff;
}

bool IsLegacyCaster(const Item &item)
{
	const WeaponKind kind = WeaponKindOf(item);
	return kind == WeaponKind::LegacyStaff || kind == WeaponKind::LegacySceptre;
}

bool IsMartialStaff(const Item &item)
{
	const WeaponKind kind = WeaponKindOf(item);
	return kind == WeaponKind::Quarterstaff || kind == WeaponKind::LegacyStaff;
}

bool HoldsMartialStaff(const Player &player)
{
	for (const Item &item : { std::cref(player.InvBody[INVLOC_HAND_LEFT]), std::cref(player.InvBody[INVLOC_HAND_RIGHT]) }) {
		if (item._iStatFlag && IsMartialStaff(item))
			return true;
	}
	return false;
}

const BaseMeta *GrantingBase(const Item &item)
{
	if (!IsCasterWeapon(item))
		return nullptr;
	const BaseMeta *base = ItemBaseMeta(item);
	if (base == nullptr || base->grantedSkill.kind == SkillKind::None)
		return nullptr;
	return base;
}

} // namespace devilution
