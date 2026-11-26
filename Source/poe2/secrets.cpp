#include "poe2/secrets.hpp"

#include <cstdio>
#include <string>
#include <string_view>

#include "utils/file_util.h"
#include "utils/log.hpp"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace secrets {

namespace {

/** @brief The name each step goes by in secrets.ini (one a line: "name=1"). */
constexpr std::string_view Names[] = { "pestered_cow", "heard_rumour", "lit_name" };

uint32_t Bits = 0;
bool Loaded = false;

std::string FilePath()
{
	return StrCat(paths::ConfigPath(), "secrets.ini");
}

void Load()
{
	if (Loaded)
		return;
	Loaded = true;
	FILE *file = OpenFile(FilePath().c_str(), "rb");
	if (file == nullptr)
		return;
	char line[128];
	while (std::fgets(line, sizeof(line), file) != nullptr) {
		std::string_view text(line);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
			text.remove_suffix(1);
		const size_t eq = text.find('=');
		if (eq == std::string_view::npos || text.substr(eq + 1) != "1")
			continue;
		const std::string_view key = text.substr(0, eq);
		for (size_t i = 0; i < std::size(Names); i++) {
			if (key == Names[i])
				Bits |= 1U << i;
		}
	}
	std::fclose(file);
}

void Save()
{
	const std::string path = FilePath();
	FILE *file = OpenFile(path.c_str(), "wb");
	if (file == nullptr) {
		LogError("Couldn't write {}", path);
		return;
	}
	std::fputs("; What this PC has found on the hidden trail. Delete this file to start it again.\n", file);
	for (size_t i = 0; i < std::size(Names); i++) {
		if ((Bits & (1U << i)) != 0)
			std::fputs(StrCat(Names[i], "=1\n").c_str(), file);
	}
	std::fclose(file);
}

} // namespace

bool Has(Found step)
{
	Load();
	return (Bits & (1U << static_cast<unsigned>(step))) != 0;
}

void Set(Found step)
{
	if (Has(step))
		return;
	Bits |= 1U << static_cast<unsigned>(step);
	Save();
}

void Reset()
{
	Load();
	Bits = 0;
	Save();
}

} // namespace secrets

} // namespace devilution
