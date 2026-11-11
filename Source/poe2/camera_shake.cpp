#include "poe2/camera_shake.hpp"

#include <algorithm>
#include <array>

#include "game_mode.hpp"
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

} // namespace

void ShakeCamera(ShakeStrength strength, int ticks)
{
	if (!gbIsPoe2 || ticks <= 0)
		return;
	int pixels = static_cast<int>(strength);
	if (const SkillLook *look = CurrentSkillLook(); look != nullptr)
		pixels = static_cast<int>(pixels * look->shake + 0.5F);
	if (pixels <= 0)
		return;
	// Keep whichever shake is currently stronger.
	if (ShakeTicksLeft > 0 && ShakePixels * ShakeTicksLeft / ShakeTicksTotal >= pixels)
		return;
	ShakePixels = pixels;
	ShakeTicksLeft = ticks;
	ShakeTicksTotal = ticks;
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
