#include "poe2/menu_intro_sdl.hpp"

#include "poe2/hd.hpp"

#if !defined(USE_SDL1) && !defined(USE_SDL3)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
// The words are worked out off the game's thread where it has threads for sure (as live_fire.cpp); elsewhere on it.
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#define POE2_INTRO_THREADS 1
#include <future>
#endif
#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "engine/assets.hpp"
#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "options.h"
#include "poe2/menu_intro.hpp"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/png.h"

namespace devilution {

namespace hd {

namespace {

constexpr float Pi = 3.14159265358979F;

/** @brief Where the intro's own art and sounds are (hd\\ui_art\\intro\\), personal installs only. */
std::string IntroFile(const char *name)
{
	std::string path;
	path.reserve(64);
	path.append("hd\\ui_art\\intro\\");
	path.append(name);
	return path;
}

bool IntroArtPresent()
{
	return FindAsset(IntroFile("steel.png")).ok() && FindAsset(IntroFile("textmaps.png")).ok() && FindAsset("hd\\ui_art\\logo\\fire_maps.png").ok();
}

/** @brief An RGBA copy of a PNG of the game's assets; null when it isn't there. */
SDL_Surface *LoadRgba(const std::string &path)
{
	if (!FindAsset(path).ok())
		return nullptr;
	SDL_Surface *loaded = LoadPNG(path.c_str());
	if (loaded == nullptr)
		return nullptr;
	SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(loaded);
	return rgba;
}

void SetLinear(SDL_Texture *texture)
{
	if (texture != nullptr)
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
}

void Destroy(SDL_Texture *&texture)
{
	if (texture != nullptr)
		SDL_DestroyTexture(texture);
	texture = nullptr;
}

/** @brief Alpha premultiplied: what is added, and what is under it times one minus its alpha. */
SDL_BlendMode PremultipliedBlend()
{
	return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
	    SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}

/** @brief What is under it less it (to take the dark out of the bloom's light: max(colour - 0.32, 0)). */
SDL_BlendMode SubtractBlend()
{
	return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
	    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
}

uint8_t Byte(float v)
{
	return static_cast<uint8_t>(std::clamp(v, 0.F, 1.F) * 255.F + 0.5F);
}

/** @brief Pictures of the window's size and smaller, drawn into, for the lens (FS_POST) as SDL's renderer has no shaders. */
struct Targets {
	SDL_Renderer *renderer = nullptr;
	int width = 0;
	int height = 0;
	bool failed = false;
	/** @brief The menu as the game drew it, and everything (the scene) before the lens. */
	SDL_Texture *ui = nullptr;
	SDL_Texture *scene = nullptr;
	/** @brief The scene smaller and smaller (the graphics card's mip levels), the light left in two of them. */
	SDL_Texture *half = nullptr;
	SDL_Texture *quarter = nullptr;
	SDL_Texture *eighth = nullptr;
	SDL_Texture *bright8 = nullptr;
	SDL_Texture *sixteenth = nullptr;
	SDL_Texture *small32 = nullptr;
	SDL_Texture *bright32 = nullptr;
	SDL_Texture *small64 = nullptr;
	SDL_Texture *small128 = nullptr;
	/** @brief The rays, the bloom and the streaks added up, and the speed blur, at half size. */
	SDL_Texture *glow = nullptr;
	SDL_Texture *zoom = nullptr;

	std::array<SDL_Texture **, 13> All()
	{
		return { &ui, &scene, &half, &quarter, &eighth, &bright8, &sixteenth, &small32, &bright32, &small64, &small128, &glow, &zoom };
	}

	void Release()
	{
		for (SDL_Texture **texture : All())
			Destroy(*texture);
	}

	void Forget()
	{
		for (SDL_Texture **texture : All())
			*texture = nullptr;
		renderer = nullptr;
		width = height = 0;
		failed = false;
	}

	bool Ensure(SDL_Renderer *wanted, int w, int h)
	{
		if (wanted == renderer && w == width && h == height)
			return !failed;
		if (wanted == renderer)
			Release();
		else
			Forget();
		renderer = wanted;
		width = w;
		height = h;
		failed = true;
		if (w <= 0 || h <= 0 || SDL_RenderTargetSupported(renderer) != SDL_TRUE)
			return false;
		const auto make = [&](int divisor) {
			SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, std::max(w / divisor, 1), std::max(h / divisor, 1));
			SetLinear(texture);
			return texture;
		};
		ui = make(1);
		scene = make(1);
		half = make(2);
		quarter = make(4);
		eighth = make(8);
		bright8 = make(8);
		sixteenth = make(16);
		small32 = make(32);
		bright32 = make(32);
		small64 = make(64);
		small128 = make(128);
		glow = make(2);
		zoom = make(2);
		for (SDL_Texture **texture : All()) {
			if (*texture == nullptr) {
				Release();
				return false;
			}
		}
		failed = false;
		return true;
	}
};

#ifndef NOSOUND
/** @brief Hundredths of a decibel for a gain (1: as loud as it is). */
int LogVolume(float gain)
{
	if (gain <= 0.0001F)
		return ATTENUATION_MIN;
	return std::max(ATTENUATION_MIN, static_cast<int>(std::lround(2000.F * std::log10(gain))));
}
#endif

/** @brief One of the intro's sounds (intro::SoundCues), loaded as the intro starts. */
struct IntroSound {
#ifndef NOSOUND
	std::unique_ptr<TSnd> sound;
#endif
	const intro::SoundCue *cue = nullptr;
	bool started = false;
	float startedAt = 0;
};

class Intro {
public:
	/** @brief Reads its art and makes its fires; false (and it doesn't play) when its art isn't there. */
	bool Load()
	{
		SDL_Surface *steel = LoadRgba(IntroFile("steel.png"));
		SDL_Surface *mask = LoadRgba(IntroFile("textmaps.png"));
		bool loaded = false;
		if (steel != nullptr && mask != nullptr) {
			loaded = subtitle_.Load(static_cast<const uint8_t *>(steel->pixels), steel->w, steel->h, steel->pitch,
			    static_cast<const uint8_t *>(mask->pixels), mask->w, mask->h, mask->pitch);
		}
		if (steel != nullptr)
			SDL_FreeSurface(steel);
		if (mask != nullptr)
			SDL_FreeSurface(mask);
		if (!loaded) {
			LogWarn("Menu intro: its art (hd\\ui_art\\intro\\steel.png, textmaps.png) could not be read");
			return false;
		}
		return MenuLogoFire() != nullptr;
	}

	/** @brief From the start: dark, the logo's fire out, its sounds ready. */
	void Start()
	{
		t_ = 0;
		real_ = 0;
		fireSeconds_ = 0;
		running_ = true;
		played_ = false;
		landed_ = false;
		raked_ = false;
		restKnown_ = false;
		framesWithoutLogo_ = 0;
		counter_ = 0;
		random_ = intro::Random(77);
		embers_.Clear();
		if (!claws_.Prepared())
			claws_.Prepare();
		if (fire::FireLayer *logo = MenuLogoFire(); logo != nullptr) {
			logo->Fire().Clear();
			LogoSettings();
		}
		// The lines start cold, and catch from the middle as the claws go by.
		lines_ = MakeLines(false);
		LoadSounds();
	}

	[[nodiscard]] bool Running() const { return running_; }
	[[nodiscard]] float Time() const { return t_; }

	/** @brief The main menu's logo is where it lands, as the game would draw it this frame. */
	void LogoAt(const SDL_FRect &picture)
	{
		rest_ = picture;
		restKnown_ = picture.w > 0 && picture.h > 0;
	}

	/** @brief Jumps to its end: the logo burning in place, the lines too, the words cooled; `quiet` stops its sounds. */
	void JumpToEnd(bool quiet)
	{
		if (quiet)
			StopSounds();
		if (!running_)
			return;
		running_ = false;
		t_ = intro::End;
		if (fire::FireLayer *logo = MenuLogoFire(); logo != nullptr) {
			RestoreLogo(*logo);
			// Skipped before it had caught: it opens burning, as the menu's always does.
			if (real_ < intro::End)
				logo->Fire().WarmUp(1.5F);
		}
		if (lines_ != nullptr) {
			intro::LineFeed(lines_->Fire().Feed(), 1.F);
			lines_->Fire().SetSettings(fire::FireSettings {});
			if (reach_ < 1.F)
				lines_->Fire().WarmUp(1.5F);
			reach_ = 1.F;
		}
		claws_ = intro::Claws {};
		embers_.Clear();
	}

	void StopSounds()
	{
#ifndef NOSOUND
		for (IntroSound &s : sounds_) {
			if (s.sound != nullptr && s.sound->DSB.IsLoaded())
				s.sound->DSB.Stop();
		}
#endif
		for (IntroSound &s : sounds_)
			s.started = true;
	}

	/** @brief Moves it on by a frame's real time (the hit-stops slow its own), its sounds by real time. */
	void Advance(float realSeconds)
	{
		// Played frame by frame: its fires are worked out off the game's thread (DrawLogo).
		played_ = true;
		const float dtReal = std::clamp(realSeconds, 0.F, 0.05F);
		real_ += dtReal;
		PlaySounds();
		const float dt = dtReal * intro::TimeWarp(t_);
		const int steps = dt > 0.024F ? 2 : 1;
		for (int s = 0; s < steps; s++)
			Step(dt / static_cast<float>(steps));
		fireSeconds_ += dt;
		if (t_ >= intro::End)
			JumpToEnd(false);
	}

	/** @brief For tests: straight to `t` in steps of 1/60 s, as the reference page's seek (no hit-stops, no sounds). */
	void Seek(float t)
	{
		fire::FireLayer *logo = MenuLogoFire();
		while (t_ < t) {
			constexpr float Dt = 1.F / 60.F;
			Step(Dt);
			if (logo != nullptr) {
				LogoSettings();
				logo->Fire().Advance(Dt);
			}
			if (lines_ != nullptr && Linesburning())
				lines_->Fire().Advance(Dt);
		}
		fireSeconds_ = 0;
	}

	/** @brief Draws the frame: the scene into its picture, then through the lens onto the window. */
	void Present(SDL_Renderer *renderer, Targets &targets, float scaleX, float scaleY, bool withUi)
	{
		const float lw = static_cast<float>(targets.width) / scaleX;
		const float lh = static_cast<float>(targets.height) / scaleY;
		fire::FireLayer *logo = MenuLogoFire();
		if (logo != nullptr)
			LogoSettings();
		const float look = intro::IntroLook(t_);
		const float kRest = restKnown_ ? rest_.w / intro::BoxWidth / intro::RestScale : 1.F;
		const SDL_FPoint rest = restKnown_ ? SDL_FPoint { rest_.x + intro::AnchorX / intro::BoxWidth * rest_.w, rest_.y + (intro::BoxHeight - intro::AnchorY) / intro::PictureHeight * rest_.h } : SDL_FPoint { lw / 2, lh / 3 };
		// It flies in, lands and is clawed in the middle of the screen, as big as half the screen's width (up to half
		// as big again as on the menu), then glides up to its place on the menu. Everything of it (the words, the
		// lines, the claws, the lens) and the embers go with it.
		const float glide = intro::Glide(t_);
		const float middleScale = restKnown_ ? std::clamp(0.5F * lw / rest_.w, 1.F, 1.5F) : 1.F;
		const float grow = middleScale + (1.F - middleScale) * glide;
		const SDL_FPoint middle { lw / 2, lh * 0.47F };
		const SDL_FPoint anchor { middle.x + (rest.x - middle.x) * glide, middle.y + (rest.y - middle.y) * glide };
		const float k = kRest * grow;
		// The embers fly over the whole screen, on the reference's stage as the logo is.
		embers_.SetScreen(intro::StageAnchorX + (0.F - anchor.x) / k, intro::StageAnchorY - (lh - anchor.y) / k, intro::StageAnchorX + (lw - anchor.x) / k, intro::StageAnchorY + anchor.y / k);

		SDL_SetRenderTarget(renderer, targets.scene);
		SDL_RenderSetScale(renderer, scaleX, scaleY);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		DrawRoom(renderer, lw, lh, look);
		// The menu, faded in over the room's glow.
		const float menu = intro::MenuAlpha(t_);
		if (withUi && menu > 0.F) {
			SDL_SetTextureBlendMode(targets.ui, SDL_BLENDMODE_BLEND);
			SDL_SetTextureAlphaMod(targets.ui, Byte(menu));
			SDL_SetTextureColorMod(targets.ui, 255, 255, 255);
			SDL_RenderCopy(renderer, targets.ui, nullptr, nullptr);
		}
		if (logo != nullptr && restKnown_) {
			const float scale = intro::LogoScale(t_);
			if (scale > 0.02F / intro::RestScale) {
				const float knock = intro::Knock(t_) * k;
				const float s = scale * grow;
				const SDL_FRect picture {
					anchor.x + (rest_.x - rest.x) * s + intro::ClawDirX * knock,
					anchor.y + (rest_.y - rest.y) * s - intro::ClawDirY * knock,
					rest_.w * s,
					rest_.h * s,
				};
				DrawLogo(renderer, *logo, picture);
			} else if (played_) {
				logo->AdvanceAsync(fireSeconds_, LogoPace);
			} else {
				logo->Fire().Advance(fireSeconds_);
			}
			fireSeconds_ = 0;
			DrawEmbers(renderer, anchor, k, look);
			DrawSpots(renderer, lw, lh, look);
			DrawClaws(renderer, anchor, k);
		}
		SDL_SetRenderTarget(renderer, nullptr);
		DrawLens(renderer, targets, lw, lh, anchor, k, look);
	}

	/** @brief After the intro: the lines burning round the subtitle. */
	void DrawLinesAfter(SDL_Renderer *renderer, const SDL_FRect &picture, float seconds, const fire::FirePace &pace)
	{
		if (lines_ == nullptr) {
			lines_ = MakeLines(true);
			reach_ = 1.F;
		}
		lines_->DrawAsync(renderer, LineRect(picture), seconds, nullptr, pace);
	}

	/** @brief After the intro: the subtitle, cooled. */
	void DrawSubtitleAfter(SDL_Renderer *renderer, const SDL_FRect &picture)
	{
		DrawWords(renderer, picture, 1000.F, static_cast<float>(SDL_GetTicks()) / 1000.F);
	}

	void ReleaseLines()
	{
		if (lines_ != nullptr)
			lines_->ReleaseTextures();
		lines_ = nullptr;
	}

	void ReleaseTextures()
	{
		for (SDL_Texture **texture : { &words_, &claws_texture_, &room_, &vignette_ })
			Destroy(*texture);
		texturesFor_ = nullptr;
		wordsPainted_ = false;
		if (lines_ != nullptr)
			lines_->ReleaseTextures();
	}

	void ForgetTextures()
	{
		for (SDL_Texture **texture : { &words_, &claws_texture_, &room_, &vignette_ })
			*texture = nullptr;
		texturesFor_ = nullptr;
		wordsPainted_ = false;
		if (lines_ != nullptr)
			lines_->ForgetTextures();
	}

	/** @brief Real time since the last frame (the first: a 60th of a second). */
	float FrameSeconds()
	{
		const uint64_t now = SDL_GetPerformanceCounter();
		float seconds = 1.F / 60.F;
		if (counter_ != 0) {
			const float since = static_cast<float>(static_cast<double>(now - counter_) / static_cast<double>(SDL_GetPerformanceFrequency()));
			if (since > 0 && since < 0.25F)
				seconds = since;
		}
		counter_ = now;
		return seconds;
	}

	/** @brief How many frames in a row the game hasn't drawn the main menu's logo (then the intro can't land). */
	int framesWithoutLogo_ = 0;

private:
	bool Linesburning() const
	{
		return intro::LineReach(t_) > 0.F || t_ > intro::Slash;
	}

	void LogoSettings()
	{
		fire::FireLayer *logo = MenuLogoFire();
		if (logo == nullptr)
			return;
		fire::FireSettings settings;
		const float amount = intro::FireAmount(t_);
		settings.amount = amount;
		settings.sparks = t_ >= intro::FlightStart - 0.4F ? std::min(1.8F, amount) : 0.F;
		// No flare-ups of its own until the menu has shown (the landing flares them all).
		settings.flares = t_ > intro::MenuAt + 0.4F ? 1.F : 0.F;
		logo->Fire().SetSettings(settings);
		logo->Fire().SetPaintExtras(intro::Trail(t_), intro::BackLight(t_));
	}

	static void RestoreLogo(fire::FireLayer &logo)
	{
		logo.Fire().SetSettings(fire::FireSettings {});
		logo.Fire().SetPaintExtras(0.F, 0.F);
	}

	std::unique_ptr<fire::FireLayer> MakeLines(bool warm)
	{
		fire::FireSource source = intro::LineSource();
		if (warm)
			intro::LineFeed(source.feed, 1.F);
		auto lines = std::make_unique<fire::FireLayer>(std::move(source), fire::FireSettings {}, 91, -1, fire::LineRecipe(), warm);
		reach_ = warm ? 1.F : 0.F;
		// The seams are painted with the lines' fire, on its thread when that is worked out there: with the time and
		// the reach of the moment its frame was started.
		lines->SetAfterPaint(
		    [this](uint8_t *rgba, int pitch) {
			    intro::PaintSeams(rgba, pitch, seamsT_, seamsReach_, fire::FireSettings {}.red);
		    },
		    [this] {
			    seamsT_ = running_ ? t_ : static_cast<float>(SDL_GetTicks()) / 1000.F;
			    seamsReach_ = reach_;
		    });
		return lines;
	}

	void Step(float dt)
	{
		t_ += dt;
		fire::FireLayer *logo = MenuLogoFire();
		if (logo != nullptr) {
			fire::LiveFire &fire = logo->Fire();
			const float pictureWidth = static_cast<float>(fire.PictureWidth());
			const float across = pictureWidth / static_cast<float>(fire::LogoGridWidth);
			const float middleX = pictureWidth * 0.5F + static_cast<float>(fire.Pad());
			const float middleY = 120.F * static_cast<float>(fire.Height()) / static_cast<float>(fire::LogoGridHeight);
			if (!landed_ && t_ >= intro::Landing) {
				// It lands: every letter flares at once, a little apart, and the flames are blown outward.
				for (size_t i = 0; i < 6; i++)
					fire.FlareAgo(i, 0.25F - static_cast<float>(i) * 0.05F, static_cast<float>(i) * 0.05F);
				for (float x = 50.F - fire::LogoGridPad; x < fire::LogoGridWidth + fire::LogoGridPad; x += 55.F) {
					for (const float y : { 40.F, 150.F, 260.F, 370.F }) {
						const float dx = x - 550.F;
						const float dy = y - 120.F;
						const float l = std::max(std::hypot(dx, dy), 1.F);
						fire.Blow((x + fire::LogoGridPad) * across, y * across, dx / l * 1100.F * across, (dy / l * 700.F + 150.F) * across);
					}
				}
			}
			// Flying at the viewer, its flames are blown back toward them.
			if (t_ > intro::FlightStart - 0.3F && t_ < intro::Landing + 0.35F) {
				const float u = std::clamp((t_ - intro::FlightStart) / (intro::Landing - intro::FlightStart), 0.F, 1.F);
				const float strength = t_ < intro::Landing ? std::pow(1.F - u, 0.9F) : std::max(0.F, 1.F - (t_ - intro::Landing) / 0.35F) * 0.15F;
				fire.BlowOutward(middleX, middleY, strength, dt);
			}
		}
		if (!landed_ && t_ >= intro::Landing) {
			landed_ = true;
			embers_.Landing(random_);
		}
		if (!raked_ && t_ >= intro::Slash) {
			raked_ = true;
			embers_.Rake(random_);
		}
		embers_.Step(dt, t_, random_);
		if (lines_ != nullptr && Linesburning()) {
			const float reach = intro::LineReach(t_);
			if (reach != reach_) {
				reach_ = reach;
				intro::LineFeed(lines_->Fire().Feed(), reach);
			}
			fire::FireSettings settings;
			settings.amount = intro::LineAmount(t_);
			lines_->Fire().SetSettings(settings);
			intro::LineSparks(lines_->Fire(), dt, reach, random_);
		}
	}

	void LoadSounds()
	{
		sounds_.clear();
#ifndef NOSOUND
		if (!gbSndInited)
			return;
		// The claws' roar: one of Diablo's two attack sounds, picked at random each time.
		const bool second = (SDL_GetPerformanceCounter() & 1) != 0;
		for (const intro::SoundCue &cue : intro::SoundCues()) {
			IntroSound s;
			s.cue = &cue;
			const char *name = second && cue.alternative != nullptr ? cue.alternative : cue.file;
			const std::string path = IntroFile(name);
			if (FindAsset(path).ok()) {
				auto loaded = SoundFileLoadWithStatus(path.c_str(), std::string_view(name) == "intro_sfx.wav");
				if (loaded.has_value())
					s.sound = std::move(loaded).value();
				else
					LogWarn("Menu intro: {}", loaded.error());
			}
			sounds_.push_back(std::move(s));
		}
#endif
	}

	void PlaySounds()
	{
#ifndef NOSOUND
		const int user = *GetOptions().Audio.soundVolume;
		for (IntroSound &s : sounds_) {
			if (s.sound == nullptr || !gbSoundOn)
				continue;
			if (!s.started && real_ >= s.cue->at) {
				s.started = true;
				s.startedAt = real_;
				const float gain = s.cue->fadeIn > 0 ? 0.0001F : s.cue->volume;
				s.sound->DSB.PlayWithVolumeAndPan(LogVolume(gain), user, 0);
			} else if (s.started && s.cue->fadeIn > 0 && real_ - s.startedAt <= s.cue->fadeIn + 0.1F && s.sound->DSB.IsPlaying()) {
				const float gain = s.cue->volume * std::clamp((real_ - s.startedAt) / s.cue->fadeIn, 0.0001F, 1.F);
				s.sound->DSB.SetVolume(LogVolume(gain) + user * (ATTENUATION_MIN / VOLUME_MIN), ATTENUATION_MIN, 0);
			}
		}
#endif
	}

	void MakeTextures(SDL_Renderer *renderer)
	{
		if (texturesFor_ == renderer)
			return;
		if (texturesFor_ != nullptr)
			ForgetTextures();
		texturesFor_ = renderer;
		const auto streaming = [&](int w, int h, SDL_BlendMode mode) {
			SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, std::max(w, 1), std::max(h, 1));
			if (texture != nullptr) {
				SetLinear(texture);
				SDL_SetTextureBlendMode(texture, mode);
			}
			return texture;
		};
		words_ = streaming(subtitle_.PictureWidth(), subtitle_.PictureHeight(), SDL_BLENDMODE_BLEND);
		premultiplied_ = words_ != nullptr && SDL_SetTextureBlendMode(words_, PremultipliedBlend()) == 0;
		if (!premultiplied_ && words_ != nullptr)
			SDL_SetTextureBlendMode(words_, SDL_BLENDMODE_BLEND);
		room_ = streaming(RoomWidth, RoomHeight, SDL_BLENDMODE_ADD);
		vignette_ = streaming(VignetteWidth, VignetteHeight, SDL_BLENDMODE_MOD);
	}

	void DrawRoom(SDL_Renderer *renderer, float lw, float lh, float look)
	{
		MakeTextures(renderer);
		const float glow = intro::RoomGlow(t_) * look;
		if (room_ == nullptr || glow <= 0.002F)
			return;
		void *pixels = nullptr;
		int pitch = 0;
		if (SDL_LockTexture(room_, nullptr, &pixels, &pitch) != 0)
			return;
		intro::PaintRoom(static_cast<uint8_t *>(pixels), pitch, RoomWidth, RoomHeight, t_, glow);
		SDL_UnlockTexture(room_);
		const SDL_FRect all { 0, 0, lw, lh };
		SDL_RenderCopyF(renderer, room_, nullptr, &all);
	}

	/** @brief The logo's fire, the lines' fire behind its letters, its letters lit by both, and the words. */
	void DrawLogo(SDL_Renderer *renderer, fire::FireLayer &logo, const SDL_FRect &picture)
	{
		const SDL_FRect grid = logo.GridRect(picture);
		// Played, the fires are worked out off the game's thread, a frame each frame of the game: this frame shows
		// the one started with the last (a frame late, some 12 ms; the game's thread no longer works them out, and
		// they have until the next frame). For a test's still (Seek) they are worked out here and now.
		if (played_)
			logo.DrawAsync(renderer, grid, fireSeconds_, nullptr, LogoPace);
		else
			logo.Draw(renderer, grid, fireSeconds_, nullptr);
		if (lines_ != nullptr) {
			if (t_ > intro::Slash) {
				if (played_)
					lines_->DrawAsync(renderer, LineRect(picture), fireSeconds_, nullptr, LinePace);
				else
					lines_->Draw(renderer, LineRect(picture), fireSeconds_);
			}
		}
		if (SDL_Texture *art = MenuLogoArt(renderer); art != nullptr) {
			if (!logo.DrawLit(renderer, art, picture, SDL_FRect { 0, 0, 1, 1 }))
				SDL_RenderCopyF(renderer, art, nullptr, &picture);
		}
		const float sinceSlash = t_ - intro::Slash;
		if (intro::Subtitle::Shows(sinceSlash))
			DrawWords(renderer, picture, sinceSlash, t_);
	}

	/** @brief Where the lines' fire grid goes when the logo's picture is at `picture`. */
	static SDL_FRect LineRect(const SDL_FRect &picture)
	{
		const float x = picture.w / intro::BoxWidth;
		const float y = picture.h / intro::PictureHeight;
		const float top = intro::BoxHeight - static_cast<float>(intro::LineGridY + intro::LineGridHeight);
		return SDL_FRect { picture.x + static_cast<float>(intro::LineGridX) * x, picture.y + top * y, static_cast<float>(intro::LineGridWidth) * x, static_cast<float>(intro::LineGridHeight) * y };
	}

	void DrawWords(SDL_Renderer *renderer, const SDL_FRect &picture, float sinceSlash, float t)
	{
		MakeTextures(renderer);
		if (words_ == nullptr || !subtitle_.Loaded())
			return;
		// Worked out 30 times a second at most (its flicker and cooling are slow); drawn every frame. Once the intro is
		// over (on the menus, the Esc menu over the game too) off the game's thread: it only sends the words up.
#ifdef POE2_INTRO_THREADS
		if (!running_ && wordsPainted_) {
			if (wordsJob_.valid() && wordsJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				wordsJob_.get();
				UploadWords();
			}
			if (!wordsJob_.valid() && std::abs(t - wordsT_) >= 1.F / 30.F) {
				wordsT_ = t;
				wordsJob_ = std::async(std::launch::async, [this, t, sinceSlash] {
					PaintWordsInto(wordsPicture_.data(), subtitle_.PictureWidth() * 4, t, sinceSlash);
				});
			}
		} else
#endif
		    if (!wordsPainted_ || std::abs(t - wordsT_) >= 1.F / 30.F) {
#ifdef POE2_INTRO_THREADS
			if (wordsJob_.valid())
				wordsJob_.get();
#endif
			PaintWords(t, sinceSlash);
			wordsPainted_ = true;
			wordsT_ = t;
		}
		const float x = picture.w / intro::BoxWidth;
		const float y = picture.h / intro::PictureHeight;
		const float pop = intro::Subtitle::Pop(sinceSlash);
		const float w = intro::Subtitle::Width * x;
		const float h = intro::Subtitle::Height * y;
		const float cx = picture.x + (intro::Subtitle::Left + intro::Subtitle::Width / 2) * x;
		const float cy = picture.y + (intro::BoxHeight - intro::Subtitle::Bottom - intro::Subtitle::Height / 2) * y;
		const SDL_FRect dest { cx - w * pop / 2, cy - h * pop / 2, w * pop, h * pop };
		SDL_SetTextureColorMod(words_, 255, 255, 255);
		SDL_RenderCopyF(renderer, words_, nullptr, &dest);
	}

	void PaintWords(float t, float sinceSlash)
	{
		wordsPicture_.resize(static_cast<size_t>(subtitle_.PictureWidth()) * subtitle_.PictureHeight() * 4);
		PaintWordsInto(wordsPicture_.data(), subtitle_.PictureWidth() * 4, t, sinceSlash);
		UploadWords();
	}

	/** @brief Sends the words' picture (wordsPicture_) up to their texture. */
	void UploadWords()
	{
		void *pixels = nullptr;
		int pitch = 0;
		if (words_ == nullptr || SDL_LockTexture(words_, nullptr, &pixels, &pitch) != 0)
			return;
		const size_t row = static_cast<size_t>(subtitle_.PictureWidth()) * 4;
		for (int j = 0; j < subtitle_.PictureHeight(); j++)
			std::memcpy(static_cast<uint8_t *>(pixels) + static_cast<ptrdiff_t>(j) * pitch, wordsPicture_.data() + j * row, row);
		SDL_UnlockTexture(words_);
	}

	/** @brief Paints the words into `pixels` (any thread: it reads only the subtitle's pictures). */
	void PaintWordsInto(uint8_t *pixels, int pitch, float t, float sinceSlash) const
	{
		subtitle_.Paint(pixels, pitch, t, sinceSlash, fire::FireSettings {}.red);
		if (!premultiplied_) {
			// Without the blend for it: straight alpha (the glow round the words is lost).
			for (int j = 0; j < subtitle_.PictureHeight(); j++) {
				auto *row = static_cast<uint8_t *>(pixels) + static_cast<ptrdiff_t>(j) * pitch;
				for (int i = 0; i < subtitle_.PictureWidth(); i++) {
					uint8_t *p = row + static_cast<ptrdiff_t>(i) * 4;
					if (p[3] == 0)
						continue;
					for (int c = 0; c < 3; c++)
						p[c] = static_cast<uint8_t>(std::min(255, p[c] * 255 / p[3]));
				}
			}
		}
	}

	/** @brief Stage point (browser pixels, y up) to the screen (logical pixels), the logo landed at `anchor`. */
	static SDL_FPoint ToScreen(float x, float y, const SDL_FPoint &anchor, float k)
	{
		return SDL_FPoint { anchor.x + (x - intro::StageAnchorX) * k, anchor.y - (y - intro::StageAnchorY) * k };
	}

	void DrawEmbers(SDL_Renderer *renderer, const SDL_FPoint &anchor, float k, float look)
	{
		embers_.Streaks(streaks_, look);
		if (streaks_.empty())
			return;
		vertices_.clear();
		indices_.clear();
		for (const intro::Streak &s : streaks_) {
			float dx = s.headX - s.tailX;
			float dy = s.headY - s.tailY;
			const float len = std::max(std::hypot(dx, dy), 0.0001F);
			const float nx = -dy / len * s.halfWidth;
			const float ny = dx / len * s.halfWidth;
			const SDL_Color lit { Byte(s.r), Byte(s.g), Byte(s.b), Byte(s.a) };
			const SDL_Color dark { lit.r, lit.g, lit.b, 0 };
			const int base = static_cast<int>(vertices_.size());
			for (const auto &[x, y, c] : { std::tuple { s.tailX + nx, s.tailY + ny, dark }, std::tuple { s.tailX - nx, s.tailY - ny, dark }, std::tuple { s.headX + nx, s.headY + ny, lit }, std::tuple { s.headX - nx, s.headY - ny, lit } }) {
				SDL_Vertex v {};
				v.position = ToScreen(x, y, anchor, k);
				v.color = c;
				vertices_.push_back(v);
			}
			for (const int i : { 0, 1, 2, 1, 3, 2 })
				indices_.push_back(base + i);
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
		SDL_RenderGeometry(renderer, nullptr, vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(), static_cast<int>(indices_.size()));
	}

	/** @brief The twelve faint spots of light drifting across the room after the landing. */
	void DrawSpots(SDL_Renderer *renderer, float lw, float lh, float look)
	{
		const float since = t_ - intro::Landing;
		if (since <= 0 || look <= 0)
			return;
		const float fade = std::min(1.F, since) * look;
		constexpr int Rings = 8;
		constexpr int Segments = 20;
		vertices_.clear();
		indices_.clear();
		const float perPixel = lh / 900.F;
		for (const intro::GlowSpot &spot : intro::GlowSpots(t_)) {
			const float bright = spot.bright * fade;
			if (bright <= 0.001F)
				continue;
			const int base = static_cast<int>(vertices_.size());
			const float cx = spot.x * lw;
			const float cy = spot.y * lh;
			for (int r = 0; r <= Rings; r++) {
				const float d = static_cast<float>(r) / Rings;
				const float light = intro::GlowSpotProfile(d) * bright;
				const SDL_Color c { 255, Byte(0.38F), Byte(0.09F), Byte(light) };
				for (int s = 0; s < (r == 0 ? 1 : Segments); s++) {
					const float a = static_cast<float>(s) / Segments * 2.F * Pi;
					SDL_Vertex v {};
					v.position = SDL_FPoint { cx + std::cos(a) * d * spot.radius * perPixel, cy + std::sin(a) * d * spot.radius * perPixel };
					v.color = c;
					vertices_.push_back(v);
				}
			}
			for (int s = 0; s < Segments; s++) {
				const int s1 = (s + 1) % Segments;
				indices_.insert(indices_.end(), { base, base + 1 + s, base + 1 + s1 });
				for (int r = 1; r < Rings; r++) {
					const int a0 = base + 1 + (r - 1) * Segments + s;
					const int a1 = base + 1 + (r - 1) * Segments + s1;
					const int b0 = a0 + Segments;
					const int b1 = a1 + Segments;
					indices_.insert(indices_.end(), { a0, b0, a1, a1, b0, b1 });
				}
			}
		}
		if (indices_.empty())
			return;
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
		SDL_RenderGeometry(renderer, nullptr, vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(), static_cast<int>(indices_.size()));
	}

	void DrawClaws(SDL_Renderer *renderer, const SDL_FPoint &anchor, float k)
	{
		const float sinceSlash = t_ - intro::Slash;
		if (sinceSlash <= 0 || sinceSlash >= intro::Claws::Lasts || !claws_.Prepared())
			return;
		if (claws_texture_ == nullptr) {
			claws_texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, claws_.Width(), claws_.Height());
			SetLinear(claws_texture_);
			if (claws_texture_ != nullptr)
				SDL_SetTextureBlendMode(claws_texture_, SDL_BLENDMODE_ADD);
			clawsT_ = -100.F;
		}
		if (claws_texture_ != nullptr) {
			void *pixels = nullptr;
			int pitch = 0;
			// Every frame as they rake, then 30 times a second as they cool.
			const bool paint = sinceSlash < 0.35F || std::abs(t_ - clawsT_) >= 1.F / 30.F;
			if (!paint) {
				if (clawsLit_)
					DrawClawsTexture(renderer, anchor, k);
			} else if (SDL_LockTexture(claws_texture_, nullptr, &pixels, &pitch) == 0) {
				const bool any = claws_.Paint(static_cast<uint8_t *>(pixels), pitch, t_, sinceSlash, fire::FireSettings {}.red);
				SDL_UnlockTexture(claws_texture_);
				clawsT_ = t_;
				clawsLit_ = any;
				if (any)
					DrawClawsTexture(renderer, anchor, k);
			}
		}
		// A blink of warm light over everything as they land.
		const float blink = 0.22F * std::exp(-sinceSlash / 0.06F);
		if (blink > 0.004F) {
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
			SDL_SetRenderDrawColor(renderer, Byte(blink), Byte(0.6F * blink), Byte(0.3F * blink), 255);
			SDL_RenderFillRectF(renderer, nullptr);
		}
	}

	/** @brief The claws' picture, turned to their way, its middle along and across it from theirs. */
	void DrawClawsTexture(SDL_Renderer *renderer, const SDL_FPoint &anchor, float k)
	{
		const float midU = (intro::Claws::Left + intro::Claws::Right) / 2.F;
		const float midV = (intro::Claws::Top + intro::Claws::Bottom) / 2.F;
		const SDL_FPoint middle = ToScreen(intro::Claws::CentreX + midU * intro::ClawDirX + midV * 0.191F, intro::Claws::CentreY + midU * intro::ClawDirY + midV * -0.982F, anchor, k);
		const float w = (intro::Claws::Right - intro::Claws::Left) * k;
		const float h = (intro::Claws::Top - intro::Claws::Bottom) * k;
		const SDL_FRect dest { middle.x - w / 2, middle.y - h / 2, w, h };
		const double angle = std::atan2(-intro::ClawDirY, intro::ClawDirX) * 180.0 / Pi;
		SDL_RenderCopyExF(renderer, claws_texture_, nullptr, &dest, angle, nullptr, SDL_FLIP_NONE);
	}

	/** @brief Where the scene goes for a lens that shows scene point ctr + (o - ctr) m + shift at screen point o. */
	static SDL_FRect SceneRect(const SDL_FPoint &ctr, float m, float shiftX, float shiftY, float lw, float lh)
	{
		return SDL_FRect { ctr.x + (0.F - ctr.x - shiftX) / m, ctr.y + (0.F - ctr.y - shiftY) / m, lw / m, lh / m };
	}

	static void CopyScaled(SDL_Renderer *renderer, SDL_Texture *from, SDL_Texture *to)
	{
		SDL_SetRenderTarget(renderer, to);
		SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
		SDL_SetTextureColorMod(from, 255, 255, 255);
		SDL_SetTextureAlphaMod(from, 255);
		SDL_RenderCopy(renderer, from, nullptr, nullptr);
	}

	/** @brief What is brighter than 0.32 of the picture in `from`, into `to` (the bloom's and the rays' light). */
	static void BrightPass(SDL_Renderer *renderer, SDL_Texture *from, SDL_Texture *to)
	{
		CopyScaled(renderer, from, to);
		if (SDL_SetRenderDrawBlendMode(renderer, SubtractBlend()) == 0) {
			SDL_SetRenderDrawColor(renderer, 82, 82, 82, 255);
		} else {
			// No blend for it: dimmed instead.
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128);
		}
		SDL_RenderFillRect(renderer, nullptr);
	}

	static void AddCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &dest, float r, float g, float b)
	{
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
		SDL_SetTextureAlphaMod(texture, 255);
		SDL_SetTextureColorMod(texture, Byte(r), Byte(g), Byte(b));
		SDL_RenderCopyF(renderer, texture, nullptr, &dest);
	}

	/** @brief The lens (FS_POST): the punch zoom and the shake, the speed blur, the rays, the bloom, the flash, the dimming. */
	void DrawLens(SDL_Renderer *renderer, Targets &targets, float lw, float lh, const SDL_FPoint &anchor, float k, float look)
	{
		const intro::Lens lens = intro::LensAt(t_, random_.Next() - 0.5F, random_.Next() - 0.5F);
		const SDL_FPoint ctr { anchor.x, anchor.y - 10.F * k };
		const float shiftX = lens.shakeX * k;
		const float shiftY = -lens.shakeY * k;
		// Zoomed in enough that the shake never shows past the picture's edges.
		float m = 1.F - lens.punch;
		if (ctr.x > 0 && lw > ctr.x) {
			m = std::min(m, (ctr.x + std::min(shiftX, 0.F)) / ctr.x);
			m = std::min(m, (lw - ctr.x - std::max(shiftX, 0.F)) / (lw - ctr.x));
		}
		if (ctr.y > 0 && lh > ctr.y) {
			m = std::min(m, (ctr.y + std::min(shiftY, 0.F)) / ctr.y);
			m = std::min(m, (lh - ctr.y - std::max(shiftY, 0.F)) / (lh - ctr.y));
		}
		m = std::max(m, 0.5F);
		const SDL_FRect sceneRect = SceneRect(ctr, m, shiftX, shiftY, lw, lh);

		// The smaller pictures, the light left in two.
		CopyScaled(renderer, targets.scene, targets.half);
		CopyScaled(renderer, targets.half, targets.quarter);
		CopyScaled(renderer, targets.quarter, targets.eighth);
		CopyScaled(renderer, targets.eighth, targets.sixteenth);
		CopyScaled(renderer, targets.sixteenth, targets.small32);
		CopyScaled(renderer, targets.small32, targets.small64);
		CopyScaled(renderer, targets.small64, targets.small128);
		BrightPass(renderer, targets.eighth, targets.bright8);
		BrightPass(renderer, targets.small32, targets.bright32);

		// All the soft light at half size: the rays from the logo, the bloom and the streaks.
		SDL_SetRenderTarget(renderer, targets.glow);
		int gw = 0;
		int gh = 0;
		SDL_QueryTexture(targets.glow, nullptr, nullptr, &gw, &gh);
		SDL_RenderSetScale(renderer, static_cast<float>(gw) / lw, static_cast<float>(gh) / lh);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		if (look > 0.F) {
			constexpr int RayTaps = 6;
			for (int i = 0; i < RayTaps; i++) {
				const float f = (static_cast<float>(i) + 0.5F) / RayTaps;
				const float s = 1.F - f * 0.62F;
				const float w = (1.F - f) * 22.F / RayTaps / 9.F * lens.rays * look;
				AddCopy(renderer, targets.bright8, SceneRect(ctr, m * s, shiftX * s, shiftY * s, lw, lh), w, w, w);
			}
			AddCopy(renderer, targets.bright8, sceneRect, 0.35F * 0.8F * look, 0.35F * 0.8F * look, 0.35F * 0.8F * look);
			AddCopy(renderer, targets.bright32, sceneRect, 0.45F * 0.8F * look, 0.45F * 0.8F * look, 0.45F * 0.8F * look);
			AddCopy(renderer, targets.small128, sceneRect, 0.25F * 0.8F * look, 0.25F * 0.8F * look, 0.25F * 0.8F * look);
			// The streaks: the light smeared sideways a fifth of the screen each way.
			constexpr std::array<std::pair<float, float>, 5> Streaks { { { 0.F, 2.75F }, { 0.06F, 1.375F }, { -0.06F, 1.375F }, { 0.15F, 1.25F }, { -0.15F, 1.25F } } };
			for (const auto &[offset, weight] : Streaks) {
				const float w = 0.07F * weight * look;
				AddCopy(renderer, targets.bright32, SceneRect(ctr, m, shiftX + offset * lw * m, shiftY, lw, lh), w, w * 0.55F, w * 0.3F);
			}
		}
		// The speed blur: the scene drawn bigger and bigger toward the viewer, averaged.
		const bool zooming = lens.zoom > 0.01F;
		if (zooming) {
			SDL_SetRenderTarget(renderer, targets.zoom);
			SDL_RenderSetScale(renderer, static_cast<float>(gw) / lw, static_cast<float>(gh) / lh);
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			// More of them while it is strong (as it bursts out), so its copies smear rather than show one by one.
			const int ZoomTaps = lens.zoom > 0.5F ? 9 : 5;
			SDL_SetTextureBlendMode(targets.half, SDL_BLENDMODE_BLEND);
			SDL_SetTextureColorMod(targets.half, 255, 255, 255);
			for (int i = 0; i < ZoomTaps; i++) {
				const float f = (static_cast<float>(i) + 0.5F) / ZoomTaps;
				const float s = 1.F - f * 0.22F * lens.zoom;
				SDL_SetTextureAlphaMod(targets.half, static_cast<uint8_t>(255 / (i + 1)));
				const SDL_FRect dest = SceneRect(ctr, m * s, shiftX * s, shiftY * s, lw, lh);
				SDL_RenderCopyF(renderer, targets.half, nullptr, &dest);
			}
		}

		// Onto the screen: the scene through the lens.
		SDL_SetRenderTarget(renderer, nullptr);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		const float ca = lens.aberration;
		if (ca > 0.002F) {
			// The colours split a little on the hits.
			AddCopy(renderer, targets.scene, SceneRect(ctr, m * (1.F + ca), shiftX * (1.F + ca), shiftY * (1.F + ca), lw, lh), 1.F, 0.F, 0.F);
			AddCopy(renderer, targets.scene, sceneRect, 0.F, 1.F, 0.F);
			AddCopy(renderer, targets.scene, SceneRect(ctr, m * (1.F - ca), shiftX * (1.F - ca), shiftY * (1.F - ca), lw, lh), 0.F, 0.F, 1.F);
		} else {
			SDL_SetTextureBlendMode(targets.scene, SDL_BLENDMODE_NONE);
			SDL_SetTextureColorMod(targets.scene, 255, 255, 255);
			SDL_RenderCopyF(renderer, targets.scene, nullptr, &sceneRect);
		}
		// Out of focus as it bursts out of the screen: the scene small, stretched back over it.
		if (lens.defocus > 0.01F) {
			SDL_SetTextureBlendMode(targets.eighth, SDL_BLENDMODE_BLEND);
			SDL_SetTextureColorMod(targets.eighth, 255, 255, 255);
			SDL_SetTextureAlphaMod(targets.eighth, Byte(lens.defocus));
			SDL_RenderCopyF(renderer, targets.eighth, nullptr, &sceneRect);
		}
		if (zooming) {
			SDL_SetTextureBlendMode(targets.zoom, SDL_BLENDMODE_BLEND);
			SDL_SetTextureColorMod(targets.zoom, 255, 255, 255);
			SDL_SetTextureAlphaMod(targets.zoom, Byte(std::min(1.F, lens.zoom * 1.2F)));
			SDL_RenderCopy(renderer, targets.zoom, nullptr, nullptr);
		}
		SDL_SetTextureBlendMode(targets.glow, SDL_BLENDMODE_ADD);
		SDL_SetTextureColorMod(targets.glow, 255, 255, 255);
		SDL_SetTextureAlphaMod(targets.glow, 255);
		SDL_RenderCopy(renderer, targets.glow, nullptr, nullptr);
		// The flash on the claws and the stamp: brighter, and warm.
		if (lens.flash > 0.005F) {
			AddCopy(renderer, targets.scene, sceneRect, lens.flash * 1.3F, lens.flash * 1.3F, lens.flash * 1.3F);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
			SDL_SetRenderDrawColor(renderer, Byte(0.10F * lens.flash), Byte(0.055F * lens.flash), Byte(0.02F * lens.flash), 255);
			SDL_RenderFillRect(renderer, nullptr);
		}
		// Dark just before the claws.
		if (lens.dim < 0.999F) {
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, Byte(1.F - lens.dim));
			SDL_RenderFillRect(renderer, nullptr);
		}
		DrawVignette(renderer, look);
	}

	void DrawVignette(SDL_Renderer *renderer, float look)
	{
		if (vignette_ == nullptr || look <= 0.F)
			return;
		void *pixels = nullptr;
		int pitch = 0;
		if (SDL_LockTexture(vignette_, nullptr, &pixels, &pitch) != 0)
			return;
		for (int j = 0; j < VignetteHeight; j++) {
			auto *row = static_cast<uint8_t *>(pixels) + static_cast<ptrdiff_t>(j) * pitch;
			const float v = (static_cast<float>(j) + 0.5F) / VignetteHeight - 0.5F;
			for (int i = 0; i < VignetteWidth; i++) {
				const float u = (static_cast<float>(i) + 0.5F) / VignetteWidth - 0.5F;
				const float dark = 0.38F * (u * u + v * v) * 2.2F * look;
				const uint8_t b = Byte(1.F - dark);
				row[i * 4 + 0] = row[i * 4 + 1] = row[i * 4 + 2] = b;
				row[i * 4 + 3] = 255;
			}
		}
		SDL_UnlockTexture(vignette_);
		SDL_RenderCopy(renderer, vignette_, nullptr, nullptr);
	}

	/** @brief The room's glow is soft: worked out small and stretched over the screen. */
	static constexpr int RoomWidth = 96;
	static constexpr int RoomHeight = 54;
	static constexpr int VignetteWidth = 64;
	static constexpr int VignetteHeight = 36;

	/**
	 * @brief How its fires are paced while it plays: a frame each frame of the game, with more of their workers than
	 * on the menus (nothing else is going on); the logo's grid is the bigger.
	 */
	static constexpr fire::FirePace LogoPace { 0.F, 5 };
	static constexpr fire::FirePace LinePace { 0.F, 3 };

	float t_ = 0;
	float real_ = 0;
	float fireSeconds_ = 0;
	bool running_ = false;
	/** @brief Whether it is played frame by frame (Advance), not sought to a moment (Seek, the tests' stills). */
	bool played_ = false;
	/** @brief The seams' time and reach for the lines' frame being worked out (set on the game's thread as it starts). */
	float seamsT_ = 0;
	float seamsReach_ = 0;
	bool landed_ = false;
	bool raked_ = false;
	float reach_ = 0;
	uint64_t counter_ = 0;
	SDL_FRect rest_ {};
	bool restKnown_ = false;
	intro::Random random_ { 77 };
	intro::Subtitle subtitle_;
	intro::Claws claws_;
	intro::Embers embers_;
	std::unique_ptr<fire::FireLayer> lines_;
	std::vector<IntroSound> sounds_;
	std::vector<intro::Streak> streaks_;
	std::vector<SDL_Vertex> vertices_;
	std::vector<int> indices_;
	SDL_Renderer *texturesFor_ = nullptr;
	SDL_Texture *words_ = nullptr;
	bool wordsPainted_ = false;
	float wordsT_ = 0;
	bool premultiplied_ = false;
	SDL_Texture *claws_texture_ = nullptr;
	float clawsT_ = 0;
	bool clawsLit_ = false;
	SDL_Texture *room_ = nullptr;
	SDL_Texture *vignette_ = nullptr;
	/** @brief The words' picture as last painted, and the painting of the next off the game's thread (last: it ends first). */
	std::vector<uint8_t> wordsPicture_;
#ifdef POE2_INTRO_THREADS
	std::future<void> wordsJob_;
#endif
};

/** @brief The intro of this launch (kept after it ends, for the subtitle and the lines it leaves on the main menu). */
std::unique_ptr<Intro> Current;
bool Played = false;
Targets FrameTargets;
float FrameScaleX = 1;
float FrameScaleY = 1;
bool FrameOpen = false;

/** @brief The tests' own intro. */
std::unique_ptr<Intro> ForTest;
Targets TestTargets;

bool Playing()
{
	return Current != nullptr && Current->Running();
}

} // namespace

bool MenuIntroWanted()
{
	if (Played || renderer == nullptr || !*GetOptions().StartUp.menuIntro)
		return false;
	return ShowsHdLogo("ui_art/smlogo") && ShowsHdLogo("ui_art/logo") && IntroArtPresent();
}

void StartMenuIntro()
{
	if (!MenuIntroWanted())
		return;
	Played = true;
	auto intro = std::make_unique<Intro>();
	if (!intro->Load())
		return;
	Current = std::move(intro);
	Current->Start();
}

bool MenuIntroPlaying()
{
	return Playing();
}

bool MenuSubtitleShown()
{
	return Current != nullptr && !Current->Running() && Enabled();
}

bool MenuIntroTakesInput()
{
	return Playing() && Current->Time() < intro::MenuAt;
}

void SkipMenuIntro()
{
	if (Playing())
		Current->JumpToEnd(true);
}

void FinishMenuIntro()
{
	if (Playing())
		Current->JumpToEnd(false);
	FrameTargets.Release();
}

bool BeginMenuIntroFrame(SDL_Renderer *renderer)
{
	FrameOpen = false;
	if (!Playing() || renderer == nullptr || SDL_GetRenderTarget(renderer) != nullptr)
		return false;
	// The HD art's textures go with their renderer: a new one forgets them all, the intro's too, so not mid-frame.
	MenuLogoArt(renderer);
	SDL_RenderGetScale(renderer, &FrameScaleX, &FrameScaleY);
	// The pictures are as big as the part of the window the game draws to (as vfx::BeginScene's).
	SDL_Rect view { 0, 0, 0, 0 };
	SDL_RenderSetScale(renderer, 1, 1);
	SDL_RenderGetViewport(renderer, &view);
	SDL_RenderSetScale(renderer, FrameScaleX, FrameScaleY);
	if (!FrameTargets.Ensure(renderer, view.w, view.h) || SDL_SetRenderTarget(renderer, FrameTargets.ui) != 0) {
		// It can't be drawn here: straight to the menu.
		Current->JumpToEnd(true);
		return false;
	}
	SDL_RenderSetScale(renderer, FrameScaleX, FrameScaleY);
	FrameOpen = true;
	return true;
}

void EndMenuIntroFrame(SDL_Renderer *renderer)
{
	if (!FrameOpen)
		return;
	FrameOpen = false;
	SDL_SetRenderTarget(renderer, nullptr);
	if (!Playing())
		return;
	// The game didn't draw the main menu's logo for a while: nowhere to land.
	if (Current->framesWithoutLogo_ > 30) {
		Current->JumpToEnd(true);
		SDL_SetTextureBlendMode(FrameTargets.ui, SDL_BLENDMODE_NONE);
		SDL_SetTextureAlphaMod(FrameTargets.ui, 255);
		SDL_RenderCopy(renderer, FrameTargets.ui, nullptr, nullptr);
		return;
	}
	Current->framesWithoutLogo_++;
	Current->Advance(Current->FrameSeconds());
	Current->Present(renderer, FrameTargets, FrameScaleX, FrameScaleY, true);
	if (!Playing())
		FrameTargets.Release();
}

namespace intro_sdl {

bool HoldsMenuLogo(const SDL_FRect &picture)
{
	if (!Playing())
		return false;
	Current->LogoAt(picture);
	Current->framesWithoutLogo_ = 0;
	return true;
}

void DrawLines(SDL_Renderer *renderer, const SDL_FRect &picture, float seconds, const fire::FirePace &pace)
{
	if (Current == nullptr || Current->Running())
		return;
	Current->DrawLinesAfter(renderer, picture, seconds, pace);
}

void DrawSubtitle(SDL_Renderer *renderer, const SDL_FRect &picture)
{
	if (Current == nullptr || Current->Running())
		return;
	Current->DrawSubtitleAfter(renderer, picture);
}

void ReleaseLines()
{
	if (Current != nullptr && !Current->Running()) {
		Current->ReleaseLines();
		Current->ReleaseTextures();
	}
}

void ForgetTextures()
{
	if (Current != nullptr)
		Current->ForgetTextures();
	FrameTargets.Forget();
	if (ForTest != nullptr)
		ForTest->ForgetTextures();
	TestTargets.Forget();
}

bool RenderForTest(SDL_Renderer *renderer, SDL_Texture *ui, const SDL_FRect &picture, float t)
{
	if (renderer == nullptr)
		return false;
	MenuLogoArt(renderer);
	// A later moment of the same intro goes on from where it is (as a frame after frame would).
	if (ForTest == nullptr || ForTest->Time() > t) {
		ForTest = std::make_unique<Intro>();
		if (!ForTest->Load()) {
			ForTest = nullptr;
			return false;
		}
		ForTest->Start();
		ForTest->StopSounds();
	}
	ForTest->LogoAt(picture);
	ForTest->Seek(t);
	float scaleX = 1;
	float scaleY = 1;
	SDL_RenderGetScale(renderer, &scaleX, &scaleY);
	SDL_Rect view { 0, 0, 0, 0 };
	SDL_RenderSetScale(renderer, 1, 1);
	SDL_RenderGetViewport(renderer, &view);
	SDL_RenderSetScale(renderer, scaleX, scaleY);
	if (!TestTargets.Ensure(renderer, view.w, view.h))
		return false;
	if (ui != nullptr) {
		SDL_SetRenderTarget(renderer, TestTargets.ui);
		SDL_SetTextureBlendMode(ui, SDL_BLENDMODE_NONE);
		SDL_RenderCopy(renderer, ui, nullptr, nullptr);
		SDL_SetRenderTarget(renderer, nullptr);
	}
	ForTest->Present(renderer, TestTargets, scaleX, scaleY, ui != nullptr);
	return true;
}

void EndForTest()
{
	if (ForTest != nullptr)
		ForTest->ReleaseTextures();
	ForTest = nullptr;
	TestTargets.Release();
	ForgetMenuFire();
}

} // namespace intro_sdl

} // namespace hd

} // namespace devilution

#else

namespace devilution {
namespace hd {
bool MenuIntroWanted() { return false; }
void StartMenuIntro() { }
bool MenuIntroPlaying() { return false; }
bool MenuIntroTakesInput() { return false; }
void SkipMenuIntro() { }
void FinishMenuIntro() { }
bool BeginMenuIntroFrame(SDL_Renderer *) { return false; }
void EndMenuIntroFrame(SDL_Renderer *) { }
bool MenuSubtitleShown() { return false; }
} // namespace hd
} // namespace devilution

#endif
