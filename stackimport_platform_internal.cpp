#include "stackimport_platform_internal.h"

#include "stackimport_logging.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <malloc.h>
#endif

namespace {

void* default_allocate(size_t size, size_t alignment, void*)
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

void default_deallocate(void* ptr, void*)
{
#if defined(_WIN32)
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

void default_message(uint32_t severity, const char* message, void*)
{
	stackimport_quill_log_message(severity, message);
}

stackimport_file_handle default_open_file(const char* path, const char* mode, void*)
{
	return fopen(path, mode);
}

size_t default_read_file(stackimport_file_handle file, void* data, size_t size, void*)
{
	return fread(data, 1, size, static_cast<FILE*>(file));
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
#if defined(_WIN32)
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}

const stackimport_internal_platform kDefaultPlatform = {
	default_allocate,
	default_deallocate,
	nullptr,
	default_message,
	default_open_file,
	default_read_file,
	default_write_file,
	default_close_file,
	default_make_directory,
	nullptr,
	nullptr,
};

thread_local const stackimport_internal_platform* current_platform = &kDefaultPlatform;
thread_local bool allocation_failed = false;

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
		nullptr,
		platform->message ? platform->message : default_message,
		platform->open_file,
		platform->read_file,
		platform->write_file,
		platform->close_file,
		platform->make_directory,
		platform->user_data,
		nullptr,
	};
}

stackimport_log_record stackimport_internal_make_log_record(uint32_t severity, const char* message)
{
	const char* detail = message ? message : "";
	uint32_t category = STACKIMPORT_LOG_GENERAL;
	if(message)
	{
		struct Prefix {
			const char* text;
			size_t length;
			uint32_t category;
		};
		const Prefix prefixes[] = {
			{"Progress:", 9, STACKIMPORT_LOG_PROGRESS},
			{"Status:", 7, STACKIMPORT_LOG_STATUS},
			{"Warning:", 8, STACKIMPORT_LOG_WARNING},
			{"Error:", 6, STACKIMPORT_LOG_ERROR},
			{"Fatal:", 6, STACKIMPORT_LOG_FATAL},
		};
		for(const Prefix& prefix : prefixes)
		{
			if(std::strncmp(message, prefix.text, prefix.length) == 0)
			{
				category = prefix.category;
				detail = message + prefix.length;
				if(detail[0] == ' ')
					detail++;
				break;
			}
		}
	}
	return stackimport_log_record{
		sizeof(stackimport_log_record),
		severity,
		category,
		message,
		detail,
	};
}

void stackimport_internal_message(uint32_t severity, const char* message)
{
	if(current_platform->log)
	{
		const stackimport_log_record record = stackimport_internal_make_log_record(severity, message);
		current_platform->log(&record, current_platform->log_user_data);
		return;
	}
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

void stackimport_internal_note_allocation_failure()
{
	allocation_failed = true;
}

void stackimport_internal_reset_allocation_failure()
{
	allocation_failed = false;
}

bool stackimport_internal_had_allocation_failure()
{
	return allocation_failed;
}

stackimport_file_handle stackimport_internal_open_file(const char* path, const char* mode)
{
	return current_platform->open_file(path, mode, current_platform->user_data);
}

size_t stackimport_internal_read_file(stackimport_file_handle file, void* data, size_t size)
{
	return current_platform->read_file(file, data, size, current_platform->user_data);
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
