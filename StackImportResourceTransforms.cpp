#include "StackImportResourceTransforms.h"

#include <cstring>

namespace stackimport {

namespace {

void swap_bgra_to_rgba(uint8_t* data, size_t pixel_count)
{
	for(size_t i = 0; i < pixel_count; ++i)
	{
		uint8_t tmp = data[i * 4];
		data[i * 4] = data[i * 4 + 2];
		data[i * 4 + 2] = tmp;
	}
}

auto resource_type_is(const rsrcd::ResRef& resource, const char* type) -> bool
{
	return resource.type.size == 4 && resource.type.data != nullptr &&
		std::memcmp(resource.type.data, type, 4) == 0;
}

} // namespace

auto emit_builtin_resource_transforms(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	if(resource_type_is(resource, "ICON") && resource.data.size == 128)
	{
		ResourcePayload descriptor = make_converted_resource_payload(
			ref,
			ResourcePayloadFormat::Rgba32,
			rsrcd::Bytes{nullptr, 32u * 32u * 4u},
			"image/x-rgba32",
			"decoded 32x32 ICON pixels");
		descriptor.width = 32;
		descriptor.height = 32;
		descriptor.row_bytes = 32 * 4;
		if(!output.wants_resource_payload(descriptor))
			return true;

		uint8_t rgba[32 * 32 * 4];
		rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
		if(!rsrcd::img::decode_icon_bw(resource.data, dst))
			return true;
		swap_bgra_to_rgba(rgba, 32u * 32u);
		descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
		return output.on_resource_payload(descriptor);
	}

	if(resource_type_is(resource, "CURS") && resource.data.size >= 68)
	{
		ResourcePayload descriptor = make_converted_resource_payload(
			ref,
			ResourcePayloadFormat::Rgba32,
			rsrcd::Bytes{nullptr, 16u * 16u * 4u},
			"image/x-rgba32",
			"decoded 16x16 CURS pixels");
		descriptor.width = 16;
		descriptor.height = 16;
		descriptor.row_bytes = 16 * 4;
		if(!output.wants_resource_payload(descriptor))
			return true;

		uint8_t rgba[16 * 16 * 4];
		rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
		int16_t hot_x = 0;
		int16_t hot_y = 0;
		if(!rsrcd::img::decode_curs(resource.data, dst, hot_x, hot_y))
			return true;
		swap_bgra_to_rgba(rgba, 16u * 16u);
		descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
		descriptor.hotspot_x = hot_x;
		descriptor.hotspot_y = hot_y;
		return output.on_resource_payload(descriptor);
	}

	if(resource_type_is(resource, "PAT#"))
	{
		const size_t pat_count = rsrcd::patlist::count(resource.data);
		for(size_t pi = 0; pi < pat_count; ++pi)
		{
			rsrcd::Bytes pat = rsrcd::patlist::pattern_at(resource.data, pi);
			if(pat.size != 8)
				continue;

			ResourcePayload descriptor = make_converted_resource_payload(
				ref,
				ResourcePayloadFormat::Rgba32,
				rsrcd::Bytes{nullptr, 8u * 8u * 4u},
				"image/x-rgba32",
				"decoded 8x8 PAT# pattern pixels");
			descriptor.variant_index = static_cast<uint32_t>(pi);
			descriptor.width = 8;
			descriptor.height = 8;
			descriptor.row_bytes = 8 * 4;
			if(!output.wants_resource_payload(descriptor))
				continue;

			uint8_t rgba[8 * 8 * 4];
			rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
			if(!rsrcd::img::decode_pat(pat, dst))
				continue;
			swap_bgra_to_rgba(rgba, 8u * 8u);
			descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
			if(!output.on_resource_payload(descriptor))
				return false;
		}
	}

	return true;
}

} // namespace stackimport
