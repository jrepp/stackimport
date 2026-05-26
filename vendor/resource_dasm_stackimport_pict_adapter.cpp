#include "../StackImportResourceDasmPictAdapter.h"

#include <exception>
#include <limits>

#include "resource_file/ResourceFile.hh"

namespace stackimport {

bool DecodePictWithResourceDasmToPng(
	const uint8_t* data,
	size_t size,
	std::vector<uint8_t>& png,
	uint32_t& width,
	uint32_t& height,
	std::string& error)
{
	png.clear();
	width = 0;
	height = 0;
	error.clear();

	try
	{
		auto decoded = ResourceDASM::ResourceFile::decode_PICT_only(data, size, false);
		if(!decoded.embedded_image_data.empty())
		{
			if(decoded.embedded_image_format != "png")
			{
				error = "PICT embedded image is not PNG";
				return false;
			}
			png.assign(decoded.embedded_image_data.begin(), decoded.embedded_image_data.end());
			return true;
		}

		if(decoded.image.empty())
		{
			error = "PICT decoded to an empty image";
			return false;
		}
		if(decoded.image.get_width() > std::numeric_limits<uint32_t>::max() ||
			decoded.image.get_height() > std::numeric_limits<uint32_t>::max())
		{
			error = "PICT dimensions exceed 32-bit payload metadata";
			return false;
		}

		const std::string png_data = decoded.image.serialize(phosg::ImageFormat::PNG);
		png.assign(png_data.begin(), png_data.end());
		width = static_cast<uint32_t>(decoded.image.get_width());
		height = static_cast<uint32_t>(decoded.image.get_height());
		return true;
	}
	catch(const std::exception& e)
	{
		error = e.what();
		return false;
	}
}

} // namespace stackimport
