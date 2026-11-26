#pragma once

#include <string>
#include <string_view>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

/**
 * @brief Where a table of the shared look and comfort features (the designed rooms, map icons, atmosphere, quest tracker,
 * skill looks) is read from. POE2 Mashup keeps them in txtdata\poe2\. Sanctuary Reforged keeps its own copies in
 * txtdata\reforged\; a reforged.mpq from before they moved has them where POE2 Mashup does, so that is tried next.
 * @param file The file name, such as "map_icons.tsv".
 */
inline std::string ModeTablePath(std::string_view file)
{
	if (gbIsReforged) {
		std::string path = StrCat("txtdata\\reforged\\", file);
		if (FindAsset(path).ok())
			return path;
	}
	return StrCat("txtdata\\poe2\\", file);
}

} // namespace devilution
