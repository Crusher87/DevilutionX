#include "poe2/camera_shake.hpp"

#include <algorithm>
#include <array>

#include "game_mode.hpp"
#include "monster.h"
#include "poe2/skill_looks.hpp"

namespace devilution {

namespace {

int ShakePixels = 0;
int ShakeTicksLeft = 0;
int ShakeTicksTotal = 1;
int ShakeStep = 0;

/** A fixed jolt pattern, so the shake never touches the game's random numbers. */
constexpr std::array<Displacement, 8> Pattern { {
	{ 1, -1 },
	{ -1, 1 },
	{ 1, 1 },
	{ -1, -1 },
	{ 0, 1 },
	{ 1, 0 },
	{ 0, -1 },
	{ -1, 0 },
} };

void StartShake(int pixels, int ticks)
{
	if (pixels <= 0 || ticks <= 0)
		return;
	// Keep whichever shake is currently stronger.
	if (ShakeTicksLeft > 0 && ShakePixels * ShakeTicksLeft / ShakeTicksTotal >= pixels)
		return;
	ShakePixels = pixels;
	ShakeTicksLeft = ticks;
	ShakeTicksTotal = ticks;
}

} // namespace

void ShakeCamera(ShakeStrength strength, int ticks)
{
	if (!gbIsPoe2 || ticks <= 0)
		return;
	int pixels = static_cast<int>(strength);
	if (const SkillLook *look = CurrentSkillLook(); look != nullptr)
		pixels = static_cast<int>(pixels * look->shake + 0.5F);
	StartShake(pixels, ticks);
}

void ShakeCameraFor(ReforgedShake moment)
{
	if (!gbIsReforged)
		return;
	// Gentle, short and fading at once, like the blow it follows: the impact lands on the first frame (vfx rules:
	// weighty, impact early, eased fade). The ladder: the hero's spells light, Apocalypse and the bosses' blows medium,
	// and only Diablo's own Apocalypse heavy.
	switch (moment) {
	case ReforgedShake::Nova:
		StartShake(static_cast<int>(ShakeStrength::Light), 5);
		break;
	case ReforgedShake::Immolation:
		StartShake(static_cast<int>(ShakeStrength::Light), 6);
		break;
	case ReforgedShake::Apocalypse:
		StartShake(static_cast<int>(ShakeStrength::Medium), 10);
		break;
	case ReforgedShake::DiabloApocalypse:
		StartShake(static_cast<int>(ShakeStrength::Heavy), 12);
		break;
	case ReforgedShake::BossBlow:
		StartShake(static_cast<int>(ShakeStrength::Medium), 6);
		break;
	}
}

bool BlowsShakeCamera(const Monster &monster)
{
	return monster.uniqueType == UniqueMonsterType::Butcher || monster.type().type == MT_DIABLO;
}

void StopCameraShake()
{
	ShakeTicksLeft = 0;
}

void TickCameraShake()
{
	if (ShakeTicksLeft > 0)
		ShakeTicksLeft--;
	ShakeStep++;
}

Displacement CameraShakeOffset()
{
	if (ShakeTicksLeft <= 0)
		return {};
	// Fades out over the shake.
	const int pixels = std::max(1, ShakePixels * ShakeTicksLeft / ShakeTicksTotal);
	return Pattern[static_cast<size_t>(ShakeStep) % Pattern.size()] * pixels;
}

} // namespace devilution
