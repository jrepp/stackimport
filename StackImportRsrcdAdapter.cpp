#include "StackImportRsrcdAdapter.h"

#include "vendor/rsrcd/include/rsrcd.hpp"

namespace stackimport {

namespace {

void swap_bgra_to_rgba(uint8_t* data, size_t pixel_count)
{
	for(size_t i = 0; i < pixel_count; i++)
	{
		uint8_t tmp = data[i * 4];
		data[i * 4] = data[i * 4 + 2];
		data[i * 4 + 2] = tmp;
	}
}

} // namespace

bool DecodeIconBwWithRsrcd(const uint8_t* data, size_t size, uint8_t* rgba, size_t rgbaSize)
{
	if(!data || !rgba || rgbaSize != 32u * 32u * 4u)
		return false;
	rsrcd::MutableBytes dst{rgba, rgbaSize};
	if(!rsrcd::img::decode_icon_bw(rsrcd::Bytes{data, size}, dst))
		return false;
	swap_bgra_to_rgba(rgba, 32u * 32u);
	return true;
}

bool DecodeCursorWithRsrcd(const uint8_t* data, size_t size, uint8_t* rgba, size_t rgbaSize, int16_t& hotX, int16_t& hotY)
{
	if(!data || !rgba || rgbaSize != 16u * 16u * 4u)
		return false;
	rsrcd::MutableBytes dst{rgba, rgbaSize};
	if(!rsrcd::img::decode_curs(rsrcd::Bytes{data, size}, dst, hotX, hotY))
		return false;
	swap_bgra_to_rgba(rgba, 16u * 16u);
	return true;
}

size_t PatternListCountWithRsrcd(const uint8_t* data, size_t size)
{
	return rsrcd::patlist::count(rsrcd::Bytes{data, size});
}

bool DecodePatternAtWithRsrcd(const uint8_t* data, size_t size, size_t index, uint8_t* rgba, size_t rgbaSize)
{
	if(!data || !rgba || rgbaSize != 8u * 8u * 4u)
		return false;
	const rsrcd::Bytes pattern = rsrcd::patlist::pattern_at(rsrcd::Bytes{data, size}, index);
	if(pattern.size != 8)
		return false;
	rsrcd::MutableBytes dst{rgba, rgbaSize};
	rsrcd::img::decode_pat(pattern, dst);
	swap_bgra_to_rgba(rgba, 8u * 8u);
	return true;
}

bool ParsePlteWithRsrcd(const uint8_t* data, size_t size, PltePalette& palette)
{
	rsrcd::plte::Palette<64> parsed;
	const auto result = rsrcd::plte::parse(rsrcd::Bytes{data, size}, parsed);
	if(!result)
		return false;

	palette = {};
	palette.wdef = parsed.wdef;
	palette.showName = parsed.show_name;
	palette.selection = parsed.selection;
	palette.frame = parsed.frame;
	palette.pictRef = parsed.pict_ref;
	palette.top = parsed.top;
	palette.left = parsed.left;
	palette.buttonCount = parsed.button_count;
	for(size_t i = 0; i < parsed.button_count && i < 64; i++)
	{
		const auto& src = parsed.buttons[i];
		PlteButton& dst = palette.buttons[i];
		dst.top = src.top;
		dst.left = src.left;
		dst.bottom = src.bottom;
		dst.right = src.right;
		dst.message = src.message.data;
		dst.messageSize = src.message.size;
	}
	return true;
}

} // namespace stackimport
