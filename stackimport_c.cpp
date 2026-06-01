#include "stackimport_c.h"

#include "CStackFile.h"
#include "StackImportResourceTransforms.h"
#include "StackImportResourceTypes.h"
#include "StackImportSoundConverter.h"
#include "stackimport_logging.h"
#include "stackimport_platform_internal.h"

#include <new>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <malloc.h>
#endif

extern unsigned char sMacRomanToUTF8Table[128][5];

struct stackimport_context {
	uint32_t abi_signature;
	CStackFile stack;
	stackimport_platform platform;
	stackimport_internal_platform internal_platform;
	bool allocator_owns_context;
};

namespace {

constexpr uint32_t fnv1a_byte(uint32_t hash, uint8_t value)
{
	return (hash ^ value) * 16777619u;
}

constexpr uint32_t fnv1a_word(uint32_t hash, uint64_t value)
{
	for(int shift = 0; shift < 64; shift += 8)
		hash = fnv1a_byte(hash, static_cast<uint8_t>((value >> shift) & 0xFFu));
	return hash;
}

constexpr uint32_t context_abi_signature()
{
	uint32_t hash = 2166136261u;
	hash = fnv1a_word(hash, STACKIMPORT_API_VERSION);
	hash = fnv1a_word(hash, STACKIMPORT_VERSION_PACKED);
	hash = fnv1a_word(hash, sizeof(stackimport_context));
	hash = fnv1a_word(hash, alignof(stackimport_context));
	hash = fnv1a_word(hash, sizeof(CStackFile));
	hash = fnv1a_word(hash, sizeof(stackimport_platform));
	hash = fnv1a_word(hash, sizeof(stackimport_internal_platform));
	hash = fnv1a_word(hash, sizeof(bool));
	return hash;
}

constexpr uint32_t kContextAbiSignature = context_abi_signature();

constexpr uint32_t kKnownImportFlags =
	STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS |
	STACKIMPORT_IMPORT_NO_STATUS |
	STACKIMPORT_IMPORT_NO_PROGRESS |
	STACKIMPORT_IMPORT_RAW_GRAPHICS;

bool valid_allocator(const stackimport_allocator* allocator)
{
	return allocator &&
		allocator->struct_size >= sizeof(stackimport_allocator) &&
		allocator->allocate &&
		allocator->deallocate;
}

bool valid_log_handler(const stackimport_log_handler* handler)
{
	return handler &&
		handler->struct_size >= sizeof(stackimport_log_handler) &&
		(handler->log || handler->message);
}

bool valid_context(const stackimport_context* context)
{
	return context && context->abi_signature == kContextAbiSignature;
}

void* STACKIMPORT_CALL libc_allocate(size_t size, size_t alignment, void*)
{
	void* ptr = nullptr;
	if(alignment < sizeof(void*))
		alignment = sizeof(void*);
#if defined(_WIN32)
	ptr = _aligned_malloc(size, alignment);
	return ptr;
#else
	if(posix_memalign(&ptr, alignment, size) != 0)
		return nullptr;
	return ptr;
#endif
}

void STACKIMPORT_CALL libc_deallocate(void* ptr, void*)
{
#if defined(_WIN32)
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

void STACKIMPORT_CALL libc_message(uint32_t severity, const char* message, void*)
{
	stackimport_quill_log_message(severity, message);
}

stackimport_file_handle STACKIMPORT_CALL libc_open_file(const char* path, const char* mode, void*)
{
	return fopen(path, mode);
}

size_t STACKIMPORT_CALL libc_read_file(stackimport_file_handle file, void* data, size_t size, void*)
{
	return fread(data, 1, size, static_cast<FILE*>(file));
}

size_t STACKIMPORT_CALL libc_write_file(stackimport_file_handle file, const void* data, size_t size, void*)
{
	return fwrite(data, 1, size, static_cast<FILE*>(file));
}

int STACKIMPORT_CALL libc_close_file(stackimport_file_handle file, void*)
{
	return fclose(static_cast<FILE*>(file));
}

int STACKIMPORT_CALL libc_make_directory(const char* path, void*)
{
#if defined(_WIN32)
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}

bool valid_platform(const stackimport_platform* platform)
{
	return platform &&
		platform->struct_size >= sizeof(stackimport_platform) &&
		platform->allocate &&
		platform->deallocate &&
		platform->open_file &&
		platform->read_file &&
		platform->write_file &&
		platform->close_file &&
		platform->make_directory;
}

stackimport_context make_context(
	const stackimport_platform& platform,
	bool allocator_owns_context,
	const stackimport_log_handler* handler = nullptr)
{
	stackimport_internal_platform internal_platform = stackimport_internal_platform_from_api(&platform);
	if(handler)
	{
		internal_platform.log = handler->log;
		if(handler->message)
			internal_platform.message = handler->message;
		internal_platform.log_user_data = handler->user_data;
		if(!handler->log)
			internal_platform.user_data = handler->user_data;
	}
	stackimport_platform_scope scope(internal_platform);
	return stackimport_context{kContextAbiSignature, CStackFile(), platform, internal_platform, allocator_owns_context};
}

uint32_t api_resource_payload_format(stackimport::ResourcePayloadFormat format)
{
	switch(format)
	{
		case stackimport::ResourcePayloadFormat::Native:
			return static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_NATIVE);
		case stackimport::ResourcePayloadFormat::Rgba32:
			return static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_RGBA32);
		case stackimport::ResourcePayloadFormat::JsonUtf8:
			return static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_JSON_UTF8);
		case stackimport::ResourcePayloadFormat::TextUtf8:
			return static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_TEXT_UTF8);
		case stackimport::ResourcePayloadFormat::Binary:
			return static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_BINARY);
	}
	return static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_NATIVE);
}

stackimport_resource_payload api_resource_payload(const stackimport::ResourcePayload& payload)
{
	stackimport_resource_payload apiPayload = {};
	apiPayload.struct_size = sizeof(stackimport_resource_payload);
	std::memcpy(apiPayload.type, payload.resource.type.v, sizeof(apiPayload.type));
	apiPayload.id = payload.resource.id;
	apiPayload.resource_flags = payload.resource.flags;
	apiPayload.order = payload.resource.order;
	apiPayload.name = payload.resource.name.data;
	apiPayload.name_size = payload.resource.name.size;
	apiPayload.native_size = payload.resource.native_size;
	apiPayload.format = api_resource_payload_format(payload.format);
	apiPayload.variant_index = payload.variant_index;
	apiPayload.width = payload.width;
	apiPayload.height = payload.height;
	apiPayload.row_bytes = payload.row_bytes;
	apiPayload.hotspot_x = payload.hotspot_x;
	apiPayload.hotspot_y = payload.hotspot_y;
	apiPayload.media_type = payload.media_type;
	apiPayload.description = payload.description;
	apiPayload.payload_size = payload.data.size;
	return apiPayload;
}

class CApiResourceOutput final : public stackimport::IResourceOutput {
public:
	explicit CApiResourceOutput(const stackimport_import_options& options)
		: resource_payload_flags_(options.resource_payload_flags)
		, resource_wants_(options.resource_wants)
		, resource_payload_(options.resource_payload)
		, resource_user_data_(options.resource_user_data)
	{}

	explicit CApiResourceOutput(const stackimport_resource_conversion_options& options)
		: resource_payload_flags_(options.resource_payload_flags)
		, resource_wants_(options.resource_wants)
		, resource_payload_(options.resource_payload)
		, resource_user_data_(options.resource_user_data)
	{}

	auto wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		if(!resource_payload_)
			return false;
		const bool converted = payload.format != stackimport::ResourcePayloadFormat::Native;
		const uint32_t wantedFlag = converted ? STACKIMPORT_RESOURCE_PAYLOADS_CONVERTED : STACKIMPORT_RESOURCE_PAYLOADS_NATIVE;
		if((resource_payload_flags_ & wantedFlag) == 0)
			return false;
		if(!resource_wants_)
			return true;
		const stackimport_resource_payload apiPayload = api_resource_payload(payload);
		return resource_wants_(&apiPayload, resource_user_data_) != 0;
	}

	auto on_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		if(!resource_payload_)
			return true;
		const stackimport_resource_payload apiPayload = api_resource_payload(payload);
		return resource_payload_(&apiPayload, payload.data.data, payload.data.size, resource_user_data_) != 0;
	}

private:
	uint32_t resource_payload_flags_;
	stackimport_resource_wants_fn resource_wants_;
	stackimport_resource_payload_fn resource_payload_;
	void* resource_user_data_;
};

std::string mac_roman_to_utf8_string(const uint8_t* data, size_t size)
{
	std::string result;
	result.reserve(size);
	for(size_t i = 0; i < size; i++)
	{
		const unsigned char ch = data[i];
		if(ch >= 128)
			result.append(reinterpret_cast<const char*>(sMacRomanToUTF8Table[ch - 128]));
		else if(ch == 0x11)
		{
			static const unsigned char command_key[4] = {0xe2, 0x8c, 0x98, 0};
			result.append(reinterpret_cast<const char*>(command_key));
		}
		else
			result.push_back(static_cast<char>(ch));
	}
	return result;
}

}

STACKIMPORT_API uint32_t STACKIMPORT_CALL stackimport_api_version(void)
{
	return STACKIMPORT_API_VERSION;
}

STACKIMPORT_API const char* STACKIMPORT_CALL stackimport_version_string(void)
{
	return STACKIMPORT_VERSION_STRING;
}

STACKIMPORT_API uint32_t STACKIMPORT_CALL stackimport_version_packed(void)
{
	return STACKIMPORT_VERSION_PACKED;
}

STACKIMPORT_API const char* STACKIMPORT_CALL stackimport_status_string(stackimport_status status)
{
	switch(status)
	{
		case STACKIMPORT_STATUS_OK:
			return "ok";
		case STACKIMPORT_STATUS_INVALID_ARGUMENT:
			return "invalid argument";
		case STACKIMPORT_STATUS_ALLOCATION_FAILED:
			return "allocation failed";
		case STACKIMPORT_STATUS_IMPORT_FAILED:
			return "import failed";
		case STACKIMPORT_STATUS_UNSUPPORTED_OPTION:
			return "unsupported option";
		case STACKIMPORT_STATUS_ABI_MISMATCH:
			return "ABI mismatch";
	}
	return "unknown status";
}

STACKIMPORT_API void STACKIMPORT_CALL stackimport_allocator_init(stackimport_allocator* allocator)
{
	if(!allocator)
		return;
	*allocator = {};
	allocator->struct_size = sizeof(stackimport_allocator);
}

STACKIMPORT_API void STACKIMPORT_CALL stackimport_log_handler_init(stackimport_log_handler* handler)
{
	if(!handler)
		return;
	*handler = {};
	handler->struct_size = sizeof(stackimport_log_handler);
}

STACKIMPORT_API void STACKIMPORT_CALL stackimport_platform_init(stackimport_platform* platform)
{
	if(!platform)
		return;
	*platform = {};
	platform->struct_size = sizeof(stackimport_platform);
	platform->allocate = libc_allocate;
	platform->deallocate = libc_deallocate;
	platform->message = libc_message;
	platform->open_file = libc_open_file;
	platform->read_file = libc_read_file;
	platform->write_file = libc_write_file;
	platform->close_file = libc_close_file;
	platform->make_directory = libc_make_directory;
}

STACKIMPORT_API void STACKIMPORT_CALL stackimport_import_options_init(stackimport_import_options* options)
{
	if(!options)
		return;
	*options = {};
	options->struct_size = sizeof(stackimport_import_options);
	options->resource_payload_flags = STACKIMPORT_RESOURCE_PAYLOADS_ALL;
}

STACKIMPORT_API void STACKIMPORT_CALL stackimport_resource_conversion_options_init(stackimport_resource_conversion_options* options)
{
	if(!options)
		return;
	*options = {};
	options->struct_size = sizeof(stackimport_resource_conversion_options);
	options->resource_payload_flags = STACKIMPORT_RESOURCE_PAYLOADS_ALL;
	options->id = 0;
}

STACKIMPORT_API size_t STACKIMPORT_CALL stackimport_context_size(void)
{
	return sizeof(stackimport_context);
}

STACKIMPORT_API size_t STACKIMPORT_CALL stackimport_context_alignment(void)
{
	return alignof(stackimport_context);
}

STACKIMPORT_API uint32_t STACKIMPORT_CALL stackimport_context_abi_signature(void)
{
	return kContextAbiSignature;
}

STACKIMPORT_API stackimport_status STACKIMPORT_CALL stackimport_context_init(
	void* storage,
	size_t storage_size,
	stackimport_context** out_context)
{
	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	return stackimport_context_init_with_platform(storage, storage_size, &platform, out_context);
}

STACKIMPORT_API stackimport_status STACKIMPORT_CALL stackimport_context_init_with_platform(
	void* storage,
	size_t storage_size,
	const stackimport_platform* platform,
	stackimport_context** out_context)
{
	if(!storage || !out_context || storage_size < sizeof(stackimport_context))
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	if(!valid_platform(platform))
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	const auto address = reinterpret_cast<uintptr_t>(storage);
	if((address % alignof(stackimport_context)) != 0)
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	*out_context = new (storage) stackimport_context(make_context(*platform, false));
	return STACKIMPORT_STATUS_OK;
}

STACKIMPORT_API stackimport_status STACKIMPORT_CALL stackimport_context_init_with_log_handler(
	void* storage,
	size_t storage_size,
	const stackimport_log_handler* handler,
	stackimport_context** out_context)
{
	if(!valid_log_handler(handler))
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	if(!storage || !out_context || storage_size < sizeof(stackimport_context))
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	const auto address = reinterpret_cast<uintptr_t>(storage);
	if((address % alignof(stackimport_context)) != 0)
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	*out_context = new (storage) stackimport_context(make_context(platform, false, handler));
	return STACKIMPORT_STATUS_OK;
}

STACKIMPORT_API void STACKIMPORT_CALL stackimport_context_deinit(stackimport_context* context)
{
	if(!context)
		return;
	if(!valid_context(context))
	{
		stackimport_internal_message(STACKIMPORT_MESSAGE_ERROR, "Error: stackimport context ABI signature mismatch");
		return;
	}
	stackimport_platform_scope scope(context->internal_platform);
	context->~stackimport_context();
}

STACKIMPORT_API stackimport_status STACKIMPORT_CALL stackimport_context_create(
	const stackimport_allocator* allocator,
	stackimport_context** out_context)
{
	if(!valid_allocator(allocator) || !out_context)
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	platform.allocate = allocator->allocate;
	platform.deallocate = allocator->deallocate;
	platform.user_data = allocator->user_data;
	return stackimport_context_create_with_platform(&platform, out_context);
}

STACKIMPORT_API stackimport_status STACKIMPORT_CALL stackimport_context_create_with_platform(
	const stackimport_platform* platform,
	stackimport_context** out_context)
{
	if(!valid_platform(platform) || !out_context)
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	void* storage = platform->allocate(sizeof(stackimport_context), alignof(stackimport_context), platform->user_data);
	if(!storage)
		return STACKIMPORT_STATUS_ALLOCATION_FAILED;
	auto* context = new (storage) stackimport_context(make_context(*platform, true));
	*out_context = context;
	return STACKIMPORT_STATUS_OK;
}

STACKIMPORT_API stackimport_status STACKIMPORT_CALL stackimport_context_create_with_log_handler(
	const stackimport_log_handler* handler,
	stackimport_context** out_context)
{
	if(!valid_log_handler(handler))
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	if(!out_context)
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	void* storage = platform.allocate(sizeof(stackimport_context), alignof(stackimport_context), platform.user_data);
	if(!storage)
		return STACKIMPORT_STATUS_ALLOCATION_FAILED;
	auto* context = new (storage) stackimport_context(make_context(platform, true, handler));
	*out_context = context;
	return STACKIMPORT_STATUS_OK;
}

STACKIMPORT_API void STACKIMPORT_CALL stackimport_context_destroy(stackimport_context* context)
{
	if(!context)
		return;
	if(!valid_context(context))
	{
		stackimport_internal_message(STACKIMPORT_MESSAGE_ERROR, "Error: stackimport context ABI signature mismatch");
		return;
	}
	const stackimport_platform platform = context->platform;
	const bool allocator_owns_context = context->allocator_owns_context;
	stackimport_platform_scope scope(context->internal_platform);
	context->~stackimport_context();
	if(allocator_owns_context)
		platform.deallocate(context, platform.user_data);
}

STACKIMPORT_API stackimport_status STACKIMPORT_CALL stackimport_import(
	stackimport_context* context,
	const stackimport_import_options* options)
{
	if(!context ||
		!options ||
		options->struct_size < offsetof(stackimport_import_options, resource_payload_flags) ||
		!options->input_path ||
		!options->output_package_path)
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	if(!valid_context(context))
		return STACKIMPORT_STATUS_ABI_MISMATCH;
	if((options->flags & ~kKnownImportFlags) != 0)
		return STACKIMPORT_STATUS_UNSUPPORTED_OPTION;

	stackimport_platform_scope scope(context->internal_platform);
	stackimport_internal_reset_allocation_failure();
	context->stack.~CStackFile();
	new (&context->stack) CStackFile();
	context->stack.SetDumpRawBlockData((options->flags & STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS) != 0);
	context->stack.SetStatusMessages((options->flags & STACKIMPORT_IMPORT_NO_STATUS) == 0);
	context->stack.SetProgressMessages((options->flags & STACKIMPORT_IMPORT_NO_PROGRESS) == 0);
	context->stack.SetDecodeGraphics((options->flags & STACKIMPORT_IMPORT_RAW_GRAPHICS) == 0);
	stackimport_import_options resourceOptions = {};
	if(options->struct_size >= sizeof(stackimport_import_options))
		resourceOptions = *options;
	CApiResourceOutput resourceOutput(resourceOptions);
	if(options->struct_size >= sizeof(stackimport_import_options) && options->resource_payload)
		context->stack.SetResourceOutput(&resourceOutput);

	if(!context->stack.LoadFile(options->input_path, options->output_package_path))
	{
		if(stackimport_internal_had_allocation_failure())
			return STACKIMPORT_STATUS_ALLOCATION_FAILED;
		return STACKIMPORT_STATUS_IMPORT_FAILED;
	}
	if(stackimport_internal_had_allocation_failure())
		return STACKIMPORT_STATUS_ALLOCATION_FAILED;
	return STACKIMPORT_STATUS_OK;
}

STACKIMPORT_API stackimport_status STACKIMPORT_CALL stackimport_convert_resource(
	const stackimport_resource_conversion_options* options)
{
	if(!options ||
		options->struct_size < sizeof(stackimport_resource_conversion_options) ||
		!options->resource_payload ||
		(!options->data && options->data_size != 0) ||
		(!options->name && options->name_size != 0))
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	if((options->resource_payload_flags & ~static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOADS_ALL)) != 0)
		return STACKIMPORT_STATUS_UNSUPPORTED_OPTION;
	if(options->type[0] == '\0' && options->type[1] == '\0' && options->type[2] == '\0' && options->type[3] == '\0')
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;

	stackimport_internal_reset_allocation_failure();
	CApiResourceOutput output(*options);
	const auto* data = static_cast<const uint8_t*>(options->data);
	const auto* name = static_cast<const uint8_t*>(options->name);
	rsrcd::ResRef resource{};
	resource.type = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(options->type), 4};
	resource.id = options->id;
	resource.data = rsrcd::Bytes{data, options->data_size};
	resource.name = rsrcd::Bytes{name, options->name_size};
	resource.flags = static_cast<uint8_t>(options->resource_flags & 0xFFu);
	resource.order = options->order;
	const stackimport::ResourceRef ref = stackimport::resource_ref_from_resref(resource);

	if((options->resource_payload_flags & STACKIMPORT_RESOURCE_PAYLOADS_NATIVE) != 0)
	{
		if(!stackimport::emit_resource_payload(output, stackimport::make_native_resource_payload(ref, resource.data)))
			return STACKIMPORT_STATUS_IMPORT_FAILED;
	}

	if((options->resource_payload_flags & STACKIMPORT_RESOURCE_PAYLOADS_CONVERTED) != 0)
	{
		if(!stackimport::emit_builtin_resource_transforms(resource, ref, output))
			return STACKIMPORT_STATUS_IMPORT_FAILED;
	}

	if(stackimport_internal_had_allocation_failure())
		return STACKIMPORT_STATUS_ALLOCATION_FAILED;
	return STACKIMPORT_STATUS_OK;
}

STACKIMPORT_API size_t STACKIMPORT_CALL stackimport_mac_roman_to_utf8(
	const void* mac_roman_data,
	size_t mac_roman_size,
	void* utf8_buffer,
	size_t utf8_capacity,
	const char** out_error)
{
	if(out_error)
		*out_error = nullptr;
	if(!out_error || (!mac_roman_data && mac_roman_size != 0))
	{
		if(out_error)
			*out_error = "invalid argument";
		return 0;
	}
	const bool queryOnly = utf8_buffer == nullptr && utf8_capacity == 0;
	if(!queryOnly && !utf8_buffer)
	{
		*out_error = "invalid output buffer";
		return 0;
	}

	thread_local std::string utf8;
	thread_local std::string error;
	error.clear();
	utf8.clear();
	utf8 = mac_roman_to_utf8_string(static_cast<const uint8_t*>(mac_roman_data), mac_roman_size);

	if(queryOnly)
		return utf8.size();
	if(utf8.size() > utf8_capacity)
	{
		*out_error = "output buffer too small";
		return 0;
	}
	if(!utf8.empty())
		std::memcpy(utf8_buffer, utf8.data(), utf8.size());
	return utf8.size();
}

STACKIMPORT_API size_t STACKIMPORT_CALL stackimport_snd_to_wav(
	const void* snd_data,
	size_t snd_size,
	void* wav_buffer,
	size_t wav_capacity,
	const char** out_error)
{
	if(out_error)
		*out_error = nullptr;
	if(!snd_data || !out_error)
	{
		if(out_error)
			*out_error = "invalid argument";
		return 0;
	}
	const bool queryOnly = wav_buffer == nullptr && wav_capacity == 0;
	if(!queryOnly && !wav_buffer)
	{
		*out_error = "invalid output buffer";
		return 0;
	}

	thread_local std::string error;
	error.clear();
	stackimport::PlatformByteVector wav;
	const rsrcd::Bytes snd{static_cast<const uint8_t*>(snd_data), snd_size};
	if(!stackimport::ConvertSndResourceToWav(snd, wav, error))
	{
		*out_error = error.empty() ? "conversion failed" : error.c_str();
		return 0;
	}

	if(queryOnly)
		return wav.size();

	if(wav.size() > wav_capacity)
	{
		*out_error = "output buffer too small";
		return 0;
	}

	std::memcpy(wav_buffer, wav.data(), wav.size());
	*out_error = nullptr;
	return wav.size();
}
