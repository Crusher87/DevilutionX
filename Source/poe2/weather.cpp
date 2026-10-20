#include "poe2/weather.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "poe2/vfx.hpp"
#include "utils/log.hpp"
#include "utils/soundsample.h"
#include "utils/str_cat.hpp"

namespace devilution::weather {

namespace {

constexpr float Pi = 3.14159265F;

/** @brief The weather's own dice: cosmetic, so they never touch the game's seeded RNG. Reseed() restarts them. */
uint32_t Seed = 0x2F6E2B1DU;

uint32_t NextRandom()
{
	Seed ^= Seed << 13;
	Seed ^= Seed >> 17;
	Seed ^= Seed << 5;
	return Seed;
}

float Random(float lo, float hi)
{
	return lo + (hi - lo) * static_cast<float>(NextRandom() & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

void PlayThunder(float gain);

/** @brief The schedule: dry spells and showers taking turns, each with a length drawn when it starts. */
struct Schedule {
	bool wet = false;
	/** @brief How long this spell has left, in ms. */
	float left = 0;
	/** @brief The shower's strength, around which it wanders. */
	float base = 0;
};

/** @brief A skill or map asking for weather: it wins over the schedule until its time is up. */
struct Override {
	bool active = false;
	Kind kind = Kind::Clear;
	float strength = 0;
	float left = 0;
};

Schedule Plan;
bool Started = false;
Override Asked;
std::optional<Kind> Forced;
float ForcedStrength = 1;
Kind LevelPreset = Kind::Rain;
State Now;
/** @brief Real time the weather has run, for its wandering and wind. */
float Clock = 0;
/** @brief Thunder waiting to follow a flash, in ms; 0 is none. */
float ThunderIn = 0;
float ThunderGain = 1;
/** @brief For tests: how long was simulated and how much of it was wet. */
float SimulatedMs = 0;
float WetMs = 0;

/** @brief A shower fades in and out over about this long. */
constexpr float FadeMs = 30000;
/** @brief A dungeon has no sky, so going in stops the rain at once. */
constexpr float NoSkyFadeMs = 300;
/** @brief A look forced from the console comes quickly, to be seen. */
constexpr float ForcedFadeMs = 3000;

/** @brief Smooth noise in -1..1 from a few slow sines, so showers swell and ease and the wind gusts. */
float Wander(float ms, float scale, float phase)
{
	const float t = ms * 0.001F * scale;
	return (std::sin(t * 0.37F + phase) + 0.6F * std::sin(t * 1.13F + phase * 2.1F) + 0.3F * std::sin(t * 2.71F + phase * 0.7F)) / 1.9F;
}

void StartSpell(bool wet)
{
	Plan.wet = wet;
	if (wet) {
		// Showers of 2 to 5 minutes, dry spells of 3 to 8: about a third of the time is wet.
		Plan.left = Random(120000, 300000);
		Plan.base = Random(0.25F, 1.F);
	} else {
		Plan.left = Random(180000, 480000);
		Plan.base = 0;
	}
}

/** @brief What the weather wants to be this moment: the override, the console's look, or the schedule. */
void Target(Kind &kind, float &strength)
{
	if (Forced) {
		kind = *Forced;
		strength = kind == Kind::Clear ? 0 : ForcedStrength;
		return;
	}
	if (Asked.active) {
		kind = Asked.kind;
		strength = Asked.kind == Kind::Clear ? 0 : Asked.strength;
		return;
	}
	kind = LevelPreset;
	strength = Plan.wet && kind != Kind::Clear ? std::clamp(Plan.base + 0.2F * Wander(Clock, 0.08F, 1.3F), 0.1F, 1.F) : 0.F;
}

void AdvanceLightning(float ms)
{
	Now.flash = std::max(0.F, Now.flash - ms / 150.F);
	if (ThunderIn > 0) {
		ThunderIn -= ms;
		if (ThunderIn <= 0) {
			ThunderIn = 0;
			PlayThunder(ThunderGain);
		}
	}
	if (Now.kind != Kind::Rain || Now.strength < 0.6F || Now.flash > 0)
		return;
	// Up to one strike every dozen seconds or so in a downpour, none below a steady rain.
	const float perMs = (Now.strength - 0.6F) / 0.4F * 0.08F / 1000.F;
	if (Random(0, 1) < perMs * ms) {
		Now.flash = 1;
		ThunderIn = Random(400, 1800);
		ThunderGain = Random(0.5F, 1.F);
	}
}

// --- Sound: rain and thunder made in memory, so no audio files are needed. --------------------------------------

constexpr int SampleRate = 22050;

/** @brief Wraps 16-bit mono samples in a WAV header, the format the game's decoder reads. */
std::unique_ptr<SoundSample> MakeSample(const std::vector<float> &samples)
{
	const size_t dataBytes = samples.size() * 2;
	const size_t total = 44 + dataBytes;
	auto data = MakeArraySharedPtr<std::uint8_t>(total);
	uint8_t *p = data.get();
	auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) *p++ = static_cast<uint8_t>(v >> (8 * i)); };
	auto put16 = [&](uint16_t v) { *p++ = static_cast<uint8_t>(v); *p++ = static_cast<uint8_t>(v >> 8); };
	std::memcpy(p, "RIFF", 4);
	p += 4;
	put32(static_cast<uint32_t>(total - 8));
	std::memcpy(p, "WAVEfmt ", 8);
	p += 8;
	put32(16);
	put16(1);
	put16(1);
	put32(SampleRate);
	put32(SampleRate * 2);
	put16(2);
	put16(16);
	std::memcpy(p, "data", 4);
	p += 4;
	put32(static_cast<uint32_t>(dataBytes));
	for (float s : samples) {
		const auto v = static_cast<int16_t>(std::clamp(s, -1.F, 1.F) * 32000.F);
		put16(static_cast<uint16_t>(v));
	}
	auto sample = std::make_unique<SoundSample>();
	if (sample->SetChunk(data, total, /*isMp3=*/false) != 0) {
		LogError(LogCategory::Audio, "Weather sound could not be made: {}", SDL_GetError());
		return nullptr;
	}
	return sample;
}

/** @brief Blends a loop's end into its start, so it goes round without a click. */
void MakeSeamless(std::vector<float> &s, size_t blend)
{
	const size_t n = s.size();
	for (size_t i = 0; i < blend && i < n; i++) {
		const float t = static_cast<float>(i) / static_cast<float>(blend);
		s[i] = s[i] * t + s[n - blend + i] * (1 - t);
	}
	s.resize(n - blend);
}

/** @brief Noise with its own dice, so making sound never moves the weather's. */
struct Noise {
	uint32_t seed = 0xA5A5F00DU;
	float next()
	{
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		return static_cast<float>(seed & 0xFFFFFF) / static_cast<float>(0x7FFFFF) - 1.F;
	}
};

/**
 * @brief Rain: a hiss of filtered noise with drops pattering on top of it. Light rain is sparse patter on a thin hiss;
 * heavy rain is a dense roar with a low rumble under it.
 */
std::vector<float> RainLoop(bool heavy)
{
	const size_t n = SampleRate * 4;
	std::vector<float> s(n + SampleRate / 5);
	Noise noise { heavy ? 0x1234567U : 0x7654321U };
	float lp = 0;
	float lp2 = 0;
	float rumble = 0;
	// Drops in flight: each a short ring that dies in a few ms.
	struct Drop {
		float phase, step, amp, decay;
	};
	std::vector<Drop> drops;
	const float dropsPerSample = (heavy ? 1.F / 60.F : 1.F / 400.F);
	for (size_t i = 0; i < s.size(); i++) {
		const float white = noise.next();
		lp += 0.35F * (white - lp);
		lp2 += 0.04F * (lp - lp2);
		const float hiss = (lp - lp2) * (heavy ? 0.55F : 0.22F);
		rumble += 0.004F * (white - rumble);
		if (noise.next() * 0.5F + 0.5F < dropsPerSample)
			drops.push_back({ 0, Pi * 2 * (1500 + 4500 * (noise.next() * 0.5F + 0.5F)) / SampleRate, 0.15F + 0.5F * (noise.next() * 0.5F + 0.5F), 1.F - 1.F / (SampleRate * (0.002F + 0.004F * (noise.next() * 0.5F + 0.5F))) });
		float patter = 0;
		for (size_t d = 0; d < drops.size();) {
			Drop &drop = drops[d];
			patter += std::sin(drop.phase) * drop.amp;
			drop.phase += drop.step;
			drop.amp *= drop.decay;
			if (drop.amp < 0.003F) {
				drops[d] = drops.back();
				drops.pop_back();
			} else {
				d++;
			}
		}
		s[i] = hiss + patter * (heavy ? 0.25F : 0.35F) + (heavy ? rumble * 1.2F : 0.F);
	}
	MakeSeamless(s, SampleRate / 5);
	return s;
}

/** @brief Thunder: a low rumble rolling in two or three waves and dying away over a few seconds. */
std::vector<float> ThunderClip()
{
	const size_t n = SampleRate * 4;
	std::vector<float> s(n);
	Noise noise { 0x7A7A7A7AU };
	float lp = 0;
	float lp2 = 0;
	struct Roll {
		float at, gain, tau;
	};
	const Roll rolls[] = { { 0.F, 1.F, 0.9F }, { 0.7F, 0.7F, 1.1F }, { 1.6F, 0.5F, 1.4F } };
	for (size_t i = 0; i < n; i++) {
		const float t = static_cast<float>(i) / SampleRate;
		lp += 0.03F * (noise.next() - lp);
		lp2 += 0.006F * (lp - lp2);
		float env = 0;
		for (const Roll &roll : rolls) {
			if (t >= roll.at)
				env += roll.gain * std::min(1.F, (t - roll.at) / 0.08F) * std::exp(-(t - roll.at) / roll.tau);
		}
		s[i] = (lp2 * 3.F + lp * 0.6F) * env * 0.9F;
	}
	return s;
}

/** @brief Wind, for snow: deep filtered noise swelling and easing. */
std::vector<float> WindLoop()
{
	const size_t n = SampleRate * 6;
	std::vector<float> s(n + SampleRate / 2);
	Noise noise { 0x51515151U };
	float lp = 0;
	float lp2 = 0;
	for (size_t i = 0; i < s.size(); i++) {
		const float t = static_cast<float>(i) / SampleRate;
		lp += 0.02F * (noise.next() - lp);
		lp2 += 0.1F * (lp - lp2);
		const float swell = (0.55F + 0.45F * std::sin(Pi * 2 * t / 3.1F)) * (0.7F + 0.3F * std::sin(Pi * 2 * t / 1.3F + 1));
		s[i] = lp2 * 2.2F * swell;
	}
	MakeSeamless(s, SampleRate / 2);
	return s;
}

struct Sounds {
	std::unique_ptr<SoundSample> light, heavy, wind, thunder;
	bool made = false;
	bool playing = false;
};

Sounds Audio;

bool SoundReady()
{
	return gbSndInited && gbSoundOn && !HeadlessMode;
}

void MakeSounds()
{
	if (Audio.made)
		return;
	Audio.made = true;
	Audio.light = MakeSample(RainLoop(false));
	Audio.heavy = MakeSample(RainLoop(true));
	Audio.wind = MakeSample(WindLoop());
	Audio.thunder = MakeSample(ThunderClip());
}

/** @brief Sets a looping sample's loudness, 0 to 1, against the player's sound volume; 0 stops it. */
void SetLoop(SoundSample *sample, float gain)
{
	if (sample == nullptr)
		return;
	if (gain <= 0.01F) {
		if (sample->IsPlaying())
			sample->Stop();
		return;
	}
	const int attenuation = std::clamp(static_cast<int>(2000.F * std::log10(gain)), ATTENUATION_MIN, 0);
	sample->SetVolume(attenuation + *GetOptions().Audio.soundVolume * (ATTENUATION_MIN / VOLUME_MIN), ATTENUATION_MIN, 0);
	if (!sample->IsPlaying())
		sample->Play(/*numIterations=*/0);
}

void UpdateSound()
{
	if (!SoundReady()) {
		if (Audio.playing)
			StopSound();
		return;
	}
	const float s = Now.strength;
	if (s <= 0.01F) {
		if (Audio.playing)
			StopSound();
		return;
	}
	MakeSounds();
	Audio.playing = true;
	const float rain = Now.kind == Kind::Rain ? 1.F : 0.F;
	const float snow = Now.kind == Kind::Snow ? 1.F : 0.F;
	// Light rain gives way to the downpour loop as it gets heavier; the two overlap in the middle.
	const float light = std::clamp(1.F - std::abs(s - 0.35F) / 0.4F, 0.F, 1.F) * std::min(1.F, s * 4);
	const float heavy = std::clamp((s - 0.3F) / 0.6F, 0.F, 1.F);
	SetLoop(Audio.light.get(), rain * light * 0.8F);
	SetLoop(Audio.heavy.get(), rain * heavy * 0.9F);
	SetLoop(Audio.wind.get(), snow * std::clamp(0.2F + s * 0.7F, 0.F, 0.9F));
}

void PlayThunder(float gain)
{
	if (!SoundReady())
		return;
	MakeSounds();
	if (Audio.thunder == nullptr)
		return;
	if (Audio.thunder->IsPlaying())
		Audio.thunder->Stop();
	const int attenuation = std::clamp(static_cast<int>(2000.F * std::log10(std::max(gain, 0.05F))), ATTENUATION_MIN, 0);
	Audio.thunder->SetVolume(attenuation + *GetOptions().Audio.soundVolume * (ATTENUATION_MIN / VOLUME_MIN), ATTENUATION_MIN, 0);
	Audio.thunder->Play(1);
}

/** @brief Opens on a dry spell, but a shorter one, so the first visit to town can be caught in a shower. */
void Start()
{
	Started = true;
	StartSpell(false);
	Plan.left = Random(60000, 240000);
}

} // namespace

bool Outdoors()
{
	return leveltype == DTYPE_TOWN;
}

bool Enabled()
{
#ifdef USE_SDL1
	return false;
#else
	return gbIsPoe2 && *GetOptions().Graphics.weather && vfx::Enabled();
#endif
}

const State &Current()
{
	return Now;
}

Kind Preset()
{
	return LevelPreset;
}

void SetPreset(Kind kind)
{
	LevelPreset = kind;
}

void Request(Kind kind, float strength, float seconds)
{
	Asked.active = true;
	Asked.kind = kind;
	Asked.strength = std::clamp(strength, 0.F, 1.F);
	Asked.left = std::max(0.F, seconds) * 1000.F;
}

void Force(std::optional<Kind> kind, float strength)
{
	Forced = kind;
	ForcedStrength = std::clamp(strength, 0.F, 1.F);
}

void Advance(float ms)
{
	if (ms <= 0)
		return;
	Clock += ms;
	SimulatedMs += ms;
	if (!Started)
		Start();
	if (Plan.left <= 0)
		StartSpell(!Plan.wet);
	Plan.left -= ms;
	if (Asked.active) {
		Asked.left -= ms;
		if (Asked.left <= 0)
			Asked.active = false;
	}

	Kind kind;
	float target;
	Target(kind, target);
	// No sky, or the weather switched off: whatever was falling stops at once.
	const bool sky = Outdoors() && Enabled();
	if (!sky)
		target = 0;
	// A change of kind waits until the old weather has died down, so rain never turns into snow mid-air.
	if (kind != Now.kind && Now.strength > 0.01F)
		target = 0;
	else if (kind != Now.kind)
		Now.kind = kind;
	float fade = FadeMs;
	if (!sky)
		fade = NoSkyFadeMs;
	else if (Forced)
		fade = ForcedFadeMs;
	const float step = ms / fade;
	if (target > Now.strength)
		Now.strength = std::min(target, Now.strength + step);
	else
		Now.strength = std::max(target, Now.strength - step);
	if (Now.strength > 0.01F)
		WetMs += ms;

	// Wind: a slow lean that gusts, stronger in heavier weather, and never quite still while it rains.
	const float gust = std::max(0.F, Wander(Clock, 0.9F, 4.2F));
	Now.wind = std::clamp(Wander(Clock, 0.15F, 2.7F) * (0.3F + 0.7F * Now.strength) + gust * gust * 0.5F * Now.strength, -1.F, 1.F);

	AdvanceLightning(ms);
	UpdateSound();
}

void Reseed(uint32_t seed)
{
	Seed = seed == 0 ? 0x2F6E2B1DU : seed;
	Plan = {};
	Asked = {};
	Forced = std::nullopt;
	Now = {};
	Clock = 0;
	ThunderIn = 0;
	SimulatedMs = 0;
	WetMs = 0;
	Start();
}

void StopSound()
{
	// The samples go too, so nothing of theirs outlives the audio; they're made again when needed.
	Audio.playing = false;
	for (std::unique_ptr<SoundSample> *sample : { &Audio.light, &Audio.heavy, &Audio.wind, &Audio.thunder }) {
		if (*sample != nullptr && (*sample)->IsPlaying())
			(*sample)->Stop();
		sample->reset();
	}
	Audio.made = false;
}

std::optional<Kind> KindNamed(std::string_view name)
{
	if (name == "rain")
		return Kind::Rain;
	if (name == "snow")
		return Kind::Snow;
	if (name == "clear" || name == "dry")
		return Kind::Clear;
	return std::nullopt;
}

std::string Text()
{
	const char *kindName = "Clear";
	if (Now.kind == Kind::Rain)
		kindName = "Rain";
	else if (Now.kind == Kind::Snow)
		kindName = "Snow";
	std::string text;
	if (!Enabled())
		text = "Weather is off (Settings > Graphics: Weather and Modern Effects, needs Upscale).\n";
	if (!Outdoors())
		text += "No sky here: dungeons stay dry.\n";
	const int percent = static_cast<int>(Now.strength * 100 + 0.5F);
	const int wind = static_cast<int>(std::abs(Now.wind) * 100 + 0.5F);
	if (Now.strength > 0.01F)
		text += StrCat(kindName, " at ", percent, "%, wind ", wind, "% from the ", Now.wind < 0 ? "right" : "left", ".\n");
	else
		text += "Dry right now.\n";
	if (Forced)
		text += "Forced from the console; weather(\"auto\") hands it back to the schedule.\n";
	else if (Asked.active)
		text += StrCat("Called by a skill for another ", static_cast<int>(Asked.left / 1000), " s.\n");
	else
		text += StrCat(Plan.wet ? "This shower has " : "Dry spell, ", static_cast<int>(std::max(0.F, Plan.left) / 1000), " s left, then ", Plan.wet ? "it clears." : "a shower.");
	return text;
}

float WetShare()
{
	return SimulatedMs > 0 ? WetMs / SimulatedMs : 0;
}

} // namespace devilution::weather
