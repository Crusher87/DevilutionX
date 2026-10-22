#include "poe2/telemetry_events.hpp"

#include <array>
#include <cmath>
#include <cstdio>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <ankerl/unordered_dense.h>

#include "utils/file_util.h"
#include "utils/format.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr size_t NumChannels = static_cast<size_t>(LogChannel::Count);
constexpr std::array<std::string_view, NumChannels> ChannelNames { "session", "fights", "combat", "damage", "skills", "loot", "progress", "warnings", "perf", "effects", "tree", "rooms" };
constexpr std::array<std::string_view, 4> LevelNames { "off", "always", "normal", "deep" };

/** Every channel starts at Normal: Deep is only switched on from the console, for one session. */
std::array<LogLevel, NumChannels> Levels = [] {
	std::array<LogLevel, NumChannels> levels {};
	levels.fill(LogLevel::Normal);
	return levels;
}();

FILE *EventsFile = nullptr;
std::string Buffer;
uint64_t BytesWritten = 0;
uint32_t WriteMs = 0;
uint32_t ClockMs = 0;
std::string Area;

/** @brief One kind of event's lines in the current second. */
struct KindRate {
	uint32_t windowStart = 0;
	int written = 0;
	int dropped = 0;
};
ankerl::unordered_dense::map<std::string, KindRate> Rates;

uint32_t RealMs()
{
	return static_cast<uint32_t>(SDL_GetTicks());
}

/** @brief Counts the event against its kind's limit for this second; false when it's over. */
bool WithinRate(std::string_view kind, LogLevel level)
{
	const uint32_t now = RealMs();
	KindRate &rate = Rates[std::string(kind)];
	if (now - rate.windowStart >= 1000) {
		rate.windowStart = now;
		rate.written = 0;
	}
	const int limit = level == LogLevel::Deep ? TelemetryDeepEventsPerSecond : TelemetryEventsPerSecond;
	if (rate.written >= limit) {
		rate.dropped++;
		return false;
	}
	rate.written++;
	return true;
}

void WriteBuffer()
{
	if (EventsFile == nullptr || Buffer.empty())
		return;
	const uint32_t start = RealMs();
	std::fwrite(Buffer.data(), 1, Buffer.size(), EventsFile);
	std::fflush(EventsFile);
	BytesWritten += Buffer.size();
	Buffer.clear();
	WriteMs += RealMs() - start;
}

} // namespace

std::string_view LogChannelName(LogChannel channel)
{
	return ChannelNames[static_cast<size_t>(channel)];
}

std::string_view LogLevelName(LogLevel level)
{
	return LevelNames[static_cast<size_t>(level)];
}

std::optional<LogChannel> ParseLogChannel(std::string_view name)
{
	for (size_t i = 0; i < NumChannels; i++) {
		if (ChannelNames[i] == name)
			return static_cast<LogChannel>(i);
	}
	return std::nullopt;
}

std::optional<LogLevel> ParseLogLevel(std::string_view name)
{
	for (size_t i = 0; i < LevelNames.size(); i++) {
		if (LevelNames[i] == name)
			return static_cast<LogLevel>(i);
	}
	return std::nullopt;
}

LogLevel GetLogLevel(LogChannel channel)
{
	return Levels[static_cast<size_t>(channel)];
}

void SetLogLevel(LogChannel channel, LogLevel level)
{
	// Session events are what every report needs, so they can't be switched off.
	if (channel == LogChannel::Session && level < LogLevel::Always)
		level = LogLevel::Always;
	Levels[static_cast<size_t>(channel)] = level;
}

bool LogLevelOn(LogChannel channel, LogLevel level)
{
	return EventsFile != nullptr && level <= Levels[static_cast<size_t>(channel)];
}

bool OpenTelemetryEvents(const std::string &path, std::string_view header)
{
	EventsFile = OpenFile(path.c_str(), "wb");
	if (EventsFile == nullptr)
		return false;
	Buffer.reserve(TelemetryEventBufferBytes);
	Buffer.append(header);
	Buffer += '\n';
	WriteBuffer();
	return true;
}

bool TelemetryEventsOpen()
{
	return EventsFile != nullptr;
}

void FlushTelemetryEvents()
{
	if (EventsFile == nullptr)
		return;
	// Kinds that went over their limit say how many lines were skipped, so nothing disappears silently.
	const uint32_t now = RealMs();
	for (auto &[kind, rate] : Rates) {
		if (rate.dropped == 0 || now - rate.windowStart < 1000)
			continue;
		StrAppend(Buffer, std::format("{{\"t\":{:d},\"k\":\"dropped\",\"of\":", ClockMs));
		AppendJsonString(Buffer, kind);
		StrAppend(Buffer, std::format(",\"n\":{:d}}}\n", rate.dropped));
		rate.dropped = 0;
	}
	WriteBuffer();
}

uint64_t TelemetryEventBytes()
{
	return BytesWritten + Buffer.size();
}

uint32_t TelemetryWriteMs()
{
	return WriteMs;
}

void AddTelemetryWriteMs(uint32_t ms)
{
	WriteMs += ms;
}

void SetTelemetryEventClock(uint32_t sessionMs, std::string_view area)
{
	ClockMs = sessionMs;
	if (Area != area)
		Area = area;
}

void AppendJsonString(std::string &out, std::string_view text)
{
	out += '"';
	for (const char c : text) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20)
				StrAppend(out, std::format("\\u{:04x}", static_cast<int>(c)));
			else
				out += c;
		}
	}
	out += '"';
}

TelemetryEvent::TelemetryEvent(LogChannel channel, LogLevel level, std::string_view kind)
    : enabled_(LogLevelOn(channel, level) && WithinRate(kind, level))
{
	if (!enabled_)
		return;
	// Every event: when (ms since the session started), what, where, and which hero (0 is your own).
	line_ = std::format("{{\"t\":{:d},\"k\":", ClockMs);
	AppendJsonString(line_, kind);
	line_ += ",\"area\":";
	AppendJsonString(line_, Area);
}

TelemetryEvent::~TelemetryEvent()
{
	if (!enabled_)
		return;
	Buffer += line_;
	Buffer += "}\n";
	if (Buffer.size() >= TelemetryEventBufferBytes)
		WriteBuffer();
}

void TelemetryEvent::key(std::string_view key)
{
	line_ += ',';
	AppendJsonString(line_, key);
	line_ += ':';
}

TelemetryEvent &TelemetryEvent::add(std::string_view key, std::string_view value)
{
	if (!enabled_)
		return *this;
	this->key(key);
	AppendJsonString(line_, value);
	return *this;
}

TelemetryEvent &TelemetryEvent::add(std::string_view key, bool value)
{
	if (!enabled_)
		return *this;
	this->key(key);
	line_ += value ? "true" : "false";
	return *this;
}

TelemetryEvent &TelemetryEvent::add(std::string_view key, double value)
{
	if (!enabled_)
		return *this;
	this->key(key);
	// JSON has no infinity or NaN.
	if (!std::isfinite(value))
		value = 0;
	StrAppend(line_, std::format("{:.2f}", value));
	return *this;
}

TelemetryEvent &TelemetryEvent::addInt(std::string_view key, int64_t value)
{
	if (!enabled_)
		return *this;
	this->key(key);
	StrAppend(line_, std::format("{:d}", value));
	return *this;
}

TelemetryEvent &TelemetryEvent::addRaw(std::string_view key, std::string_view json)
{
	if (!enabled_)
		return *this;
	this->key(key);
	line_.append(json);
	return *this;
}

JsonArray &JsonArray::add(std::string_view text)
{
	if (text_.size() > 1)
		text_ += ',';
	AppendJsonString(text_, text);
	return *this;
}

JsonArray &JsonArray::addRaw(std::string_view json)
{
	if (text_.size() > 1)
		text_ += ',';
	text_.append(json);
	return *this;
}

} // namespace devilution
