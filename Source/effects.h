/**
 * @file effects.h
 *
 * Interface of functions for loading and playing sounds.
 */
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "engine/sound.h"
#include "parse_sfx_id.hpp"
#include "sound_effect_enums.h"

namespace devilution {

struct TSFX {
	uint8_t bFlags;
	std::string pszName;
	std::unique_ptr<TSnd> pSnd;
};

extern int sfxdelay;
extern SfxID sfxdnum;

bool effect_is_playing(SfxID nSFX);
void stream_stop();
void PlaySFX(SfxID psfx);
void PlaySfxLoc(SfxID psfx, Point position, bool randomizeByCategory = true);
/** @brief POE2 Mashup: plays a mod's own sound file at a tile, loading it the first time. */
void PlayModSfxLoc(std::string_view path, Point position);
/**
 * @brief POE2 Mashup: plays a sound effect slower and lower (ratePercent of its speed, under 100 for deeper), by
 * reading its wave file as if it were recorded at a lower rate. A sound that isn't a plain wave file plays as it is.
 */
void PlayPitchedSfxLoc(SfxID psfx, Point position, int ratePercent);
void sound_stop();
void sound_update();
void effects_cleanup_sfx(bool fullUnload = true);
void sound_init();
void ui_sound_init();
void effects_play_sound(SfxID);
int GetSFXLength(SfxID nSFX);

std::expected<HeroSpeech, std::string> ParseHeroSpeech(std::string_view value);

} // namespace devilution
