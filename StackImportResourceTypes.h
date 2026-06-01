#pragma once

#include <cstddef>
#include <cstdint>

#include "rsrcd.hpp"

namespace stackimport {

enum class ResourcePayloadFormat : uint8_t {
	Native = 0,
	Rgba32 = 1,
	JsonUtf8 = 2,
	TextUtf8 = 3,
	Binary = 4,
};

struct ResourceRef {
	rsrcd::FourCC type;
	int32_t id;
	rsrcd::Bytes name;
	uint8_t flags;
	uint32_t order;
	size_t native_size;
};

struct ResourcePayload {
	ResourceRef resource;
	ResourcePayloadFormat format;
	rsrcd::Bytes data;
	uint32_t variant_index;
	uint32_t width;
	uint32_t height;
	uint32_t row_bytes;
	int32_t hotspot_x;
	int32_t hotspot_y;
	const char* media_type;
	const char* description;
};

class IResourceOutput {
public:
	virtual ~IResourceOutput() = default;

	virtual auto wants_resource_payload(const ResourcePayload& payload) -> bool
	{
		(void)payload;
		return true;
	}
	virtual auto on_resource_payload(const ResourcePayload& payload) -> bool = 0;
	virtual auto on_resource_error(const ResourceRef& resource, const char* msg) -> bool
	{
		(void)resource;
		(void)msg;
		return true;
	}
};

struct ResourceWalkOptions {
	bool emit_native;
	bool emit_converted;

	constexpr ResourceWalkOptions() : emit_native(true), emit_converted(true) {}
};

inline auto resource_ref_from_resref(const rsrcd::ResRef& res) -> ResourceRef
{
	ResourceRef ref{};
	if(res.type.size == 4 && res.type.data != nullptr)
		ref.type = rsrcd::FourCC::from_bytes(res.type.data);
	ref.id = res.id;
	ref.name = res.name;
	ref.flags = res.flags;
	ref.order = res.order;
	ref.native_size = res.data.size;
	return ref;
}

inline auto make_native_resource_payload(const ResourceRef& ref, rsrcd::Bytes data) -> ResourcePayload
{
	ResourcePayload payload{};
	payload.resource = ref;
	payload.format = ResourcePayloadFormat::Native;
	payload.data = data;
	payload.media_type = "application/octet-stream";
	payload.description = "native resource data";
	return payload;
}

inline auto make_converted_resource_payload(
	const ResourceRef& ref,
	ResourcePayloadFormat format,
	rsrcd::Bytes data,
	const char* media_type,
	const char* description) -> ResourcePayload
{
	ResourcePayload payload{};
	payload.resource = ref;
	payload.format = format;
	payload.data = data;
	payload.media_type = media_type;
	payload.description = description;
	return payload;
}

inline auto emit_resource_payload(IResourceOutput& output, const ResourcePayload& payload) -> bool
{
	ResourcePayload descriptor = payload;
	descriptor.data.data = nullptr;
	if(!output.wants_resource_payload(descriptor))
		return true;
	return output.on_resource_payload(payload);
}

inline auto emit_resource_payload(IResourceOutput* output, const ResourcePayload& payload) -> bool
{
	if(output == nullptr)
		return true;
	return emit_resource_payload(*output, payload);
}

} // namespace stackimport
