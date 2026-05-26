#include "StackImportPngWriter.h"

#include "stackimport_c.h"
#include "stackimport_platform_internal.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace stackimport {
namespace {

struct PlatformPngWriter {
	stackimport_file_handle file;
	bool ok;
};

struct MemoryPngWriter {
	std::vector<uint8_t>* data;
	bool ok;
};

void write_png_chunk(void* context, void* data, int size)
{
	auto* writer = static_cast<PlatformPngWriter*>(context);
	if(!writer || !writer->ok || size < 0)
		return;
	const size_t bytes = static_cast<size_t>(size);
	writer->ok = stackimport_internal_write_file(writer->file, data, bytes) == bytes;
}

void write_png_memory_chunk(void* context, void* data, int size)
{
	auto* writer = static_cast<MemoryPngWriter*>(context);
	if(!writer || !writer->ok || !writer->data || !data || size < 0)
		return;
	const auto* bytes = static_cast<const uint8_t*>(data);
	writer->data->insert(writer->data->end(), bytes, bytes + static_cast<size_t>(size));
}

} // namespace

bool WritePngToMemory(
	std::vector<uint8_t>& png,
	int width,
	int height,
	int components,
	const void* data,
	int strideBytes)
{
	png.clear();
	MemoryPngWriter writer{&png, true};
	const int encoded = stbi_write_png_to_func(write_png_memory_chunk, &writer, width, height, components, data, strideBytes);
	if(encoded == 0 || !writer.ok)
	{
		png.clear();
		return false;
	}
	return true;
}

bool WritePngFile(
	const std::string& path,
	int width,
	int height,
	int components,
	const void* data,
	int strideBytes)
{
	stackimport_file_handle file = stackimport_internal_open_file(path.c_str(), "wb");
	if(!file)
		return false;

	PlatformPngWriter writer{file, true};
	const int encoded = stbi_write_png_to_func(write_png_chunk, &writer, width, height, components, data, strideBytes);
	const int closeStatus = stackimport_internal_close_file(file);
	return encoded != 0 && writer.ok && closeStatus == 0;
}

} // namespace stackimport
