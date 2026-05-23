#include "stackimport_platform_internal.h"

#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>

namespace {

void* default_allocate(size_t size, size_t alignment, void*)
{
	void* ptr = nullptr;
	if(alignment < sizeof(void*))
		alignment = sizeof(void*);
	if(posix_memalign(&ptr, alignment, size) != 0)
		return nullptr;
	return ptr;
}

void default_deallocate(void* ptr, void*)
{
	free(ptr);
}

void default_message(uint32_t severity, const char* message, void*)
{
	FILE* stream = severity >= STACKIMPORT_MESSAGE_WARNING ? stderr : stdout;
	fprintf(stream, "%s\n", message ? message : "");
}

stackimport_file_handle default_open_file(const char* path, const char* mode, void*)
{
	return fopen(path, mode);
}

size_t default_write_file(stackimport_file_handle file, const void* data, size_t size, void*)
{
	return fwrite(data, 1, size, static_cast<FILE*>(file));
}

int default_close_file(stackimport_file_handle file, void*)
{
	return fclose(static_cast<FILE*>(file));
}

int default_make_directory(const char* path, void*)
{
	return mkdir(path, 0777);
}

const stackimport_internal_platform kDefaultPlatform = {
	default_allocate,
	default_deallocate,
	default_message,
	default_open_file,
	default_write_file,
	default_close_file,
	default_make_directory,
	nullptr,
};

thread_local const stackimport_internal_platform* current_platform = &kDefaultPlatform;

}

const stackimport_internal_platform& stackimport_default_internal_platform()
{
	return kDefaultPlatform;
}

const stackimport_internal_platform& stackimport_current_internal_platform()
{
	return *current_platform;
}

stackimport_internal_platform stackimport_internal_platform_from_api(const stackimport_platform* platform)
{
	if(!platform)
		return stackimport_default_internal_platform();
	return {
		platform->allocate,
		platform->deallocate,
		platform->message ? platform->message : default_message,
		platform->open_file,
		platform->write_file,
		platform->close_file,
		platform->make_directory,
		platform->user_data,
	};
}

void stackimport_internal_message(uint32_t severity, const char* message)
{
	current_platform->message(severity, message, current_platform->user_data);
}

void* stackimport_internal_allocate(size_t size, size_t alignment)
{
	return current_platform->allocate(size, alignment, current_platform->user_data);
}

void stackimport_internal_deallocate(void* ptr, stackimport_deallocate_fn deallocate, void* user_data)
{
	if(ptr)
		deallocate(ptr, user_data);
}

stackimport_file_handle stackimport_internal_open_file(const char* path, const char* mode)
{
	return current_platform->open_file(path, mode, current_platform->user_data);
}

size_t stackimport_internal_write_file(stackimport_file_handle file, const void* data, size_t size)
{
	return current_platform->write_file(file, data, size, current_platform->user_data);
}

int stackimport_internal_close_file(stackimport_file_handle file)
{
	return current_platform->close_file(file, current_platform->user_data);
}

int stackimport_internal_make_directory(const char* path)
{
	return current_platform->make_directory(path, current_platform->user_data);
}

stackimport_platform_scope::stackimport_platform_scope(const stackimport_internal_platform& platform)
	: previous_(current_platform)
{
	current_platform = &platform;
}

stackimport_platform_scope::~stackimport_platform_scope()
{
	current_platform = previous_;
}
