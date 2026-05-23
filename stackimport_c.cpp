#include "stackimport_c.h"

#include "CStackFile.h"
#include "stackimport_platform_internal.h"

#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

struct stackimport_context {
	CStackFile stack;
	stackimport_platform platform;
	stackimport_internal_platform internal_platform;
	bool allocator_owns_context;
};

namespace {

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

void* libc_allocate(size_t size, size_t alignment, void*)
{
	void* ptr = nullptr;
	if(alignment < sizeof(void*))
		alignment = sizeof(void*);
	if(posix_memalign(&ptr, alignment, size) != 0)
		return nullptr;
	return ptr;
}

void libc_deallocate(void* ptr, void*)
{
	free(ptr);
}

void libc_message(uint32_t severity, const char* message, void*)
{
	FILE* stream = severity >= STACKIMPORT_MESSAGE_WARNING ? stderr : stdout;
	fprintf(stream, "%s\n", message ? message : "");
}

stackimport_file_handle libc_open_file(const char* path, const char* mode, void*)
{
	return fopen(path, mode);
}

size_t libc_write_file(stackimport_file_handle file, const void* data, size_t size, void*)
{
	return fwrite(data, 1, size, static_cast<FILE*>(file));
}

int libc_close_file(stackimport_file_handle file, void*)
{
	return fclose(static_cast<FILE*>(file));
}

int libc_make_directory(const char* path, void*)
{
	return mkdir(path, 0777);
}

bool valid_platform(const stackimport_platform* platform)
{
	return platform &&
		platform->struct_size >= sizeof(stackimport_platform) &&
		platform->allocate &&
		platform->deallocate &&
		platform->open_file &&
		platform->write_file &&
		platform->close_file &&
		platform->make_directory;
}

stackimport_context make_context(const stackimport_platform& platform, bool allocator_owns_context)
{
	stackimport_internal_platform internal_platform = stackimport_internal_platform_from_api(&platform);
	stackimport_platform_scope scope(internal_platform);
	return stackimport_context{CStackFile(), platform, internal_platform, allocator_owns_context};
}

}

uint32_t stackimport_api_version(void)
{
	return STACKIMPORT_API_VERSION;
}

const char* stackimport_status_string(stackimport_status status)
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
	}
	return "unknown status";
}

void stackimport_allocator_init(stackimport_allocator* allocator)
{
	if(!allocator)
		return;
	*allocator = {};
	allocator->struct_size = sizeof(stackimport_allocator);
}

void stackimport_platform_init(stackimport_platform* platform)
{
	if(!platform)
		return;
	*platform = {};
	platform->struct_size = sizeof(stackimport_platform);
	platform->allocate = libc_allocate;
	platform->deallocate = libc_deallocate;
	platform->message = libc_message;
	platform->open_file = libc_open_file;
	platform->write_file = libc_write_file;
	platform->close_file = libc_close_file;
	platform->make_directory = libc_make_directory;
}

void stackimport_import_options_init(stackimport_import_options* options)
{
	if(!options)
		return;
	*options = {};
	options->struct_size = sizeof(stackimport_import_options);
}

size_t stackimport_context_size(void)
{
	return sizeof(stackimport_context);
}

size_t stackimport_context_alignment(void)
{
	return alignof(stackimport_context);
}

stackimport_status stackimport_context_init(
	void* storage,
	size_t storage_size,
	stackimport_context** out_context)
{
	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	return stackimport_context_init_with_platform(storage, storage_size, &platform, out_context);
}

stackimport_status stackimport_context_init_with_platform(
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

void stackimport_context_deinit(stackimport_context* context)
{
	if(!context)
		return;
	stackimport_platform_scope scope(context->internal_platform);
	context->~stackimport_context();
}

stackimport_status stackimport_context_create(
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

stackimport_status stackimport_context_create_with_platform(
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

void stackimport_context_destroy(stackimport_context* context)
{
	if(!context)
		return;
	const stackimport_platform platform = context->platform;
	const bool allocator_owns_context = context->allocator_owns_context;
	stackimport_platform_scope scope(context->internal_platform);
	context->~stackimport_context();
	if(allocator_owns_context)
		platform.deallocate(context, platform.user_data);
}

stackimport_status stackimport_import(
	stackimport_context* context,
	const stackimport_import_options* options)
{
	if(!context ||
		!options ||
		options->struct_size < sizeof(stackimport_import_options) ||
		!options->input_path ||
		!options->output_package_path)
		return STACKIMPORT_STATUS_INVALID_ARGUMENT;
	if((options->flags & ~kKnownImportFlags) != 0)
		return STACKIMPORT_STATUS_UNSUPPORTED_OPTION;

	stackimport_platform_scope scope(context->internal_platform);
	context->stack.~CStackFile();
	new (&context->stack) CStackFile();
	context->stack.SetDumpRawBlockData((options->flags & STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS) != 0);
	context->stack.SetStatusMessages((options->flags & STACKIMPORT_IMPORT_NO_STATUS) == 0);
	context->stack.SetProgressMessages((options->flags & STACKIMPORT_IMPORT_NO_PROGRESS) == 0);
	context->stack.SetDecodeGraphics((options->flags & STACKIMPORT_IMPORT_RAW_GRAPHICS) == 0);

	if(!context->stack.LoadFile(options->input_path, options->output_package_path))
		return STACKIMPORT_STATUS_IMPORT_FAILED;
	return STACKIMPORT_STATUS_OK;
}
