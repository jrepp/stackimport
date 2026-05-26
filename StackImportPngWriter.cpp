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

void write_png_chunk(void* context, void* data, int size)
{
	auto* writer = static_cast<PlatformPngWriter*>(context);
	if(!writer || !writer->ok || size < 0)
		return;
	const size_t bytes = static_cast<size_t>(size);
	writer->ok = stackimport_internal_write_file(writer->file, data, bytes) == bytes;
}

} // namespace

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
