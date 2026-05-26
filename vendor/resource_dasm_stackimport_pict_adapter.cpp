#include "../StackImportResourceDasmPictAdapter.h"
#include "../StackImportPngWriter.h"

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
		const size_t imageWidth = decoded.image.get_width();
		const size_t imageHeight = decoded.image.get_height();
		if(imageWidth > std::numeric_limits<uint32_t>::max() ||
			imageHeight > std::numeric_limits<uint32_t>::max())
		{
			error = "PICT dimensions exceed 32-bit payload metadata";
			return false;
		}
		if(imageWidth > static_cast<size_t>(std::numeric_limits<int>::max()) ||
			imageHeight > static_cast<size_t>(std::numeric_limits<int>::max()))
		{
			error = "PICT dimensions exceed PNG encoder limits";
			return false;
		}
		if(imageHeight != 0 && imageWidth > std::numeric_limits<size_t>::max() / imageHeight / 4)
		{
			error = "PICT dimensions exceed PNG staging buffer limits";
			return false;
		}

		const auto imageWidthInt = static_cast<int>(imageWidth);
		const auto imageHeightInt = static_cast<int>(imageHeight);
		std::vector<uint8_t> rgba(imageWidth * imageHeight * 4);
		for(size_t y = 0; y < imageHeight; ++y)
		{
			for(size_t x = 0; x < imageWidth; ++x)
			{
				const uint32_t color = decoded.image.read(x, y);
				const size_t offset = ((y * imageWidth) + x) * 4;
				rgba[offset] = static_cast<uint8_t>((color >> 24) & 0xFF);
				rgba[offset + 1] = static_cast<uint8_t>((color >> 16) & 0xFF);
				rgba[offset + 2] = static_cast<uint8_t>((color >> 8) & 0xFF);
				rgba[offset + 3] = static_cast<uint8_t>(color & 0xFF);
			}
		}
		if(!WritePngToMemory(png, imageWidthInt, imageHeightInt, 4, rgba.data(), imageWidthInt * 4))
		{
			error = "PICT PNG encoding failed";
			return false;
		}
		width = static_cast<uint32_t>(imageWidth);
		height = static_cast<uint32_t>(imageHeight);
		return true;
	}
	catch(const std::exception& e)
	{
		error = e.what();
		return false;
	}
}

} // namespace stackimport
