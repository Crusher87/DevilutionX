/**
 * @file animationinfo.h
 *
 * Contains the core animation information and related logic
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "engine/clx_sprite.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

/**
 * @brief Specifies what special logics are applied for a Animation
 */
enum AnimationDistributionFlags : uint8_t {
	None = 0,
	/**
	 * @brief processAnimation will be called after setNewAnimation (in same game tick as NewPlrAnim)
	 */
	ProcessAnimationPending = 1 << 0,
	/**
	 * @brief Delay of last Frame is ignored (for example, because only Frame and not delay is checked in game_logic)
	 */
	SkipsDelayOfLastFrame = 1 << 1,
	/**
	 * @brief Repeated Animation (for example same player melee attack, that can be repeated directly after hit frame and doesn't need to show all animation frames)
	 */
	RepeatedAction = 1 << 2,
};

/**
 * @brief Contains the core animation information and related logic
 */
class AnimationInfo {
public:
	/**
	 * @brief Animation sprite
	 */
	OptionalClxSpriteList sprites;
	/**
	 * @brief How many game ticks are needed to advance one Animation Frame
	 */
	int8_t ticksPerFrame;
	/**
	 * @brief Increases by one each game tick, counting how close we are to ticksPerFrame
	 */
	int8_t tickCounterOfCurrentFrame;
	/**
	 * @brief Number of frames in current animation
	 */
	int8_t numberOfFrames;
	/**
	 * @brief Current frame of animation
	 */
	int8_t currentFrame;
	/**
	 * @brief Is the animation currently petrified and shouldn't advance with gfProgressToNextGameTick
	 */
	bool isPetrified;

	[[nodiscard]] ClxSprite currentSprite() const
	{
		// A timed animation loaded from a save has more logic frames than sprites (see setTimedAnimation): it holds the last.
		const auto frame = static_cast<uint32_t>(std::max<int8_t>(getFrameToUseForRendering(), 0));
		return (*sprites)[std::min(frame, sprites->numSprites() - 1)];
	}

	[[nodiscard]] bool isLastFrame() const
	{
		return currentFrame >= (numberOfFrames - 1);
	}

	/**
	 * @brief Calculates the Frame to use for the Animation rendering
	 * @return The Frame to use for rendering
	 */
	[[nodiscard]] int8_t getFrameToUseForRendering() const;

	/**
	 * @brief Calculates the progress of the current animation as a fraction (see baseValueFraction)
	 */
	[[nodiscard]] uint8_t getAnimationProgress() const;

	/**
	 * @brief How far the shown frame is towards the next one, from 0 to just under 1 (POE2 HD in-between frames).
	 */
	[[nodiscard]] float getFrameFraction() const;

	/**
	 * @brief Sets the new Animation with all relevant information for rendering
	 * @param sprites Animation sprites
	 * @param numberOfFrames Number of Frames in Animation
	 * @param ticksPerFrame How many game ticks are needed to advance one Animation Frame
	 * @param flags Specifies what special logics are applied to this Animation
	 * @param numSkippedFrames Number of Frames that will be skipped (for example with modifier "faster attack")
	 * @param distributeFramesBeforeFrame Distribute the numSkippedFrames only before this frame
	 * @param previewShownGameTickFragments Defines how long (in game ticks fraction) the preview animation was shown
	 */
	void setNewAnimation(OptionalClxSpriteList sprites, int8_t numberOfFrames, int8_t ticksPerFrame, AnimationDistributionFlags flags = AnimationDistributionFlags::None, int8_t numSkippedFrames = 0, int8_t distributeFramesBeforeFrame = 0, uint8_t previewShownGameTickFragments = 0);

	/**
	 * @brief POE2 Mashup: sets an animation that takes a set number of game ticks, however many frames it has. The game
	 * logic still moves one frame a game tick, so every frame it checks comes exactly once: the animation starts
	 * ticksToAction frames before the action frame (before the first frame if the wind-up is longer, as a negative
	 * numSkippedFrames does) and ends ticksAfterAction frames after it, which numberOfFrames counts. Rendering spreads
	 * the sprites' frames before the action frame over the wind-up, as setNewAnimation does, and the ones after it over
	 * the follow-through.
	 * @param sprites Animation sprites
	 * @param spriteFrames Number of frames the sprites have
	 * @param actionFrame The frame the action (a hit or a cast) happens on
	 * @param ticksToAction Game ticks from the start to the action frame, at least 2
	 * @param ticksAfterAction Game ticks from the action frame to the last frame, at least 1
	 * @param flags Specifies what special logics are applied to this Animation
	 * @param previewShownGameTickFragments Defines how long (in game ticks fraction) the preview animation was shown
	 */
	void setTimedAnimation(OptionalClxSpriteList sprites, int8_t spriteFrames, int8_t actionFrame, int8_t ticksToAction, int8_t ticksAfterAction, AnimationDistributionFlags flags, uint8_t previewShownGameTickFragments = 0);

	/**
	 * @brief Changes the Animation Data on-the-fly. This is needed if a animation is currently in progress and the player changes his gear.
	 * @param sprites Animation sprites
	 * @param numberOfFrames Number of Frames in Animation
	 * @param ticksPerFrame How many game ticks are needed to advance one Animation Frame
	 */
	void changeAnimationData(OptionalClxSpriteList sprites, int8_t numberOfFrames, int8_t ticksPerFrame);

	/**
	 * @brief Process the Animation for a game tick (for example advances the frame)
	 * @param reverseAnimation Play the animation backwards (for example is used for "unseen" monster fading)
	 */
	void processAnimation(bool reverseAnimation = false);

	/**
	 * @brief The scale of the raw fixed point fractions still used at some boundaries (see
	 * getAnimationProgress() and previewShownGameTickFragments); baseValueFraction/128
	 * corresponds to 1/100%. Internally, AnimationInfo uses FixedPoint<..., 7> instead, which is
	 * the same scale expressed as a type rather than a raw value.
	 */
	constexpr static uint8_t baseValueFraction = 128;

private:
	/**
	 * @brief A fixed point count of game ticks, signed because ProcessAnimationPending
	 * distribution deliberately starts it out negative.
	 */
	using TicksFixed = FixedPoint<int16_t, 7>;
	/**
	 * @brief A fixed point rate of animation-fractions advanced per game tick.
	 */
	using RateFixed = FixedPoint<uint16_t, 7>;

	/**
	 * @brief returns the progress as a fraction in time to the next game tick or no progress if the animation is frozen
	 */
	[[nodiscard]] FixedPoint<uint8_t, 7> getProgressToNextGameTick() const;

	/**
	 * @brief The sprite frame shown in a timed animation's follow-through (see setTimedAnimation), in 1/baseValueFraction frames.
	 */
	[[nodiscard]] int getFollowThroughSpriteFraction() const;

	/**
	 * @brief Animation Frames that will be adjusted for the skipped Frames/game ticks
	 */
	int8_t relevantFramesForDistributing_;
	/**
	 * @brief Animation Frames that wasn't shown from previous Animation
	 */
	int8_t skippedFramesFromPreviousAnimation_;
	/**
	 * @brief Specifies how many animation-fractions are displayed between two game ticks. this can be more than one frame, if animations are skipped or less than one frame if the same animation is shown in multiple times (delay specified).
	 */
	RateFixed tickModifier_;
	/**
	 * @brief Number of game ticks after the current animation sequence started
	 */
	TicksFixed ticksSinceSequenceStarted_;
	/**
	 * @brief A timed animation's sprite frames, when they differ from numberOfFrames (see setTimedAnimation); 0 otherwise.
	 */
	int8_t spriteFrames_ = 0;
};

} // namespace devilution
