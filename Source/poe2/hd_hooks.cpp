#include "poe2/hd.hpp"

namespace devilution {

namespace hd {

bool Recording = false;
RecordTileFn RecordTileHook = nullptr;
RecordSpriteFn RecordSpriteHook = nullptr;
RegisterSpritesFn RegisterSpritesHook = nullptr;
RecordUiSpriteFn RecordUiSpriteHook = nullptr;
RecordUiOutlineFn RecordUiOutlineHook = nullptr;
RecordUiPictureFn RecordUiPictureHook = nullptr;
RegisterUiPictureFn RegisterUiPictureHook = nullptr;
ForgetUiPictureFn ForgetUiPictureHook = nullptr;
RecordUiBlitFn RecordUiBlitHook = nullptr;
RegisterUiLettersFn RegisterUiLettersHook = nullptr;
RecordUiSpellIconFn RecordUiSpellIconHook = nullptr;
CoverUiSpellIconFn CoverUiSpellIconHook = nullptr;

} // namespace hd

} // namespace devilution
