#include "poe2/hd.hpp"

namespace devilution {

namespace hd {

bool Recording = false;
RecordTileFn RecordTileHook = nullptr;
RecordSpriteFn RecordSpriteHook = nullptr;

} // namespace hd

} // namespace devilution
