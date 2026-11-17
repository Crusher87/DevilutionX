#include "panels/info_box.hpp"

#include "engine/load_cel.hpp"
#include "poe2/hd.hpp"

namespace devilution {

OptionalOwnedClxSpriteList pSTextBoxCels;
OptionalOwnedClxSpriteList pSTextSlidCels;

void InitInfoBoxGfx()
{
	pSTextSlidCels = LoadCel("data\\textslid", 12);
	pSTextBoxCels = LoadCel("data\\textbox2", 271);
	// POE2 Mashup: the shops' box and its scroll bar show the HD pack's art of them.
	hd::RegisterUiPicture("data\\textslid", *pSTextSlidCels);
	hd::RegisterUiPicture("data\\textbox2", *pSTextBoxCels);
}

void FreeInfoBoxGfx()
{
	hd::ForgetUiPicture(pSTextBoxCels);
	hd::ForgetUiPicture(pSTextSlidCels);
	pSTextBoxCels = std::nullopt;
	pSTextSlidCels = std::nullopt;
}

} // namespace devilution
