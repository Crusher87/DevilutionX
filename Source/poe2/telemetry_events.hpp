/**
 * @file poe2/telemetry_events.hpp
 *
 * POE2 Mashup play log, the data half: every event also goes to session_*.events beside the readable log,
 * one JSON object per line with a kind, a time and named fields, so reports read numbers rather than
 * sentences. Each channel has a loudness level (switched with log() in the console), every kind of
 * event is rate limited, and lines are kept in memory and written about once a second.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace devilution {

/** @brief Version of the event file's layout, in its first line. */
constexpr int TelemetryEventsVersion = 1;
/** @brief Lines of one kind written per second; the rest are counted in a "dropped" event. */
constexpr int TelemetryEventsPerSecond = 40;
constexpr int TelemetryDeepEventsPerSecond = 60;
/** @brief Buffered event text is written out once this large, even before the second is up. */
constexpr size_t TelemetryEventBufferBytes = 64 * 1024;

/** @brief What an event is about; each channel has its own loudness. */
enum class LogChannel : uint8_t {
	/** Game start and end, summaries, notes, levels, deaths, crashes, settings. */
	Session,
	/** Fight records: one per encounter. */
	Fights,
	/** Kills and hits taken. */
	Combat,
	/** Damage dealt per hit, and the breakdown of every hit both ways. */
	Damage,
	/** Skills used and failed, gems. */
	Skills,
	/** Drops, pickups, crafting, gold. */
	Loot,
	/** Areas entered, level-ups, build snapshots. */
	Progress,
	/** Problems the game survived: missing pictures, bad values on load, engine warnings. */
	Warnings,
	/** Stalls, frame rate and the log's own cost. */
	Perf,
	/** Visual effects going off. */
	Effects,
	/** Passive tree: every node taken or refunded, and the whole tree at game start, each level-up and game end. */
	Tree,
	/** Dungeon floors room by room: what each floor holds, and how the time on it was spent. */
	Rooms,
	Count,
};

/** @brief How much a channel records: Always events, Normal detail, or the Deep breakdowns. */
enum class LogLevel : uint8_t {
	Off,
	Always,
	Normal,
	Deep,
};

std::string_view LogChannelName(LogChannel channel);
std::string_view LogLevelName(LogLevel level);
std::optional<LogChannel> ParseLogChannel(std::string_view name);
std::optional<LogLevel> ParseLogLevel(std::string_view name);
LogLevel GetLogLevel(LogChannel channel);
void SetLogLevel(LogChannel channel, LogLevel level);
/** @brief Whether events of this level on this channel are written. */
bool LogLevelOn(LogChannel channel, LogLevel level);

/** @brief Opens the event file and writes its header line. */
bool OpenTelemetryEvents(const std::string &path, std::string_view header);
bool TelemetryEventsOpen();
/** @brief Writes the buffered events to the file (and the counts of any dropped by the rate limits). */
void FlushTelemetryEvents();
/** @brief Bytes written to the event file this session, and milliseconds spent writing both files. */
uint64_t TelemetryEventBytes();
uint32_t TelemetryWriteMs();
void AddTelemetryWriteMs(uint32_t ms);

/** @brief The time and area stamped on every event; set by the play log. */
void SetTelemetryEventClock(uint32_t sessionMs, std::string_view area);

/** @brief Quotes text for JSON. */
void AppendJsonString(std::string &out, std::string_view text);

/**
 * @brief One event: built field by field and written when it goes out of scope.
 * Whether it's written is decided when it's made (the channel's level and the kind's rate limit), so a
 * switched off event costs one check: test it with if (event) before working out costly fields.
 */
class TelemetryEvent {
public:
	TelemetryEvent(LogChannel channel, LogLevel level, std::string_view kind);
	~TelemetryEvent();
	TelemetryEvent(const TelemetryEvent &) = delete;
	TelemetryEvent &operator=(const TelemetryEvent &) = delete;

	explicit operator bool() const
	{
		return enabled_;
	}

	TelemetryEvent &add(std::string_view key, std::string_view value);
	TelemetryEvent &add(std::string_view key, const char *value)
	{
		return add(key, std::string_view(value));
	}
	TelemetryEvent &add(std::string_view key, const std::string &value)
	{
		return add(key, std::string_view(value));
	}
	TelemetryEvent &add(std::string_view key, bool value);
	TelemetryEvent &add(std::string_view key, double value);
	template <typename T>
	    requires std::is_integral_v<T>
	TelemetryEvent &add(std::string_view key, T value)
	{
		if constexpr (std::is_signed_v<T>)
			return addInt(key, static_cast<int64_t>(value));
		else
			return addInt(key, static_cast<int64_t>(static_cast<uint64_t>(value)));
	}
	/** @brief Adds a field whose value is already JSON (an array or object). */
	TelemetryEvent &addRaw(std::string_view key, std::string_view json);

private:
	TelemetryEvent &addInt(std::string_view key, int64_t value);
	void key(std::string_view key);

	std::string line_;
	bool enabled_;
};

/** @brief Builds a JSON array of strings or objects, for event fields like gear and skills. */
class JsonArray {
public:
	JsonArray &add(std::string_view text);
	/** @brief Adds an element that is already JSON. */
	JsonArray &addRaw(std::string_view json);
	std::string str() const
	{
		return text_ + ']';
	}

private:
	std::string text_ = "[";
};

} // namespace devilution
