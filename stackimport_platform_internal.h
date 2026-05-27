#pragma once

#include "stackimport_c.h"

#include <cstddef>

struct stackimport_internal_platform {
	stackimport_allocate_fn allocate;
	stackimport_deallocate_fn deallocate;
	stackimport_log_fn log;
	stackimport_message_fn message;
	stackimport_open_file_fn open_file;
	stackimport_read_file_fn read_file;
	stackimport_write_file_fn write_file;
	stackimport_close_file_fn close_file;
	stackimport_make_directory_fn make_directory;
	void* user_data;
	void* log_user_data;
};

const stackimport_internal_platform& stackimport_default_internal_platform();
const stackimport_internal_platform& stackimport_current_internal_platform();
stackimport_internal_platform stackimport_internal_platform_from_api(const stackimport_platform* platform);
stackimport_log_record stackimport_internal_make_log_record(uint32_t severity, const char* message);
void stackimport_internal_message(uint32_t severity, const char* message);
void* stackimport_internal_allocate(size_t size, size_t alignment);
void stackimport_internal_deallocate(void* ptr, stackimport_deallocate_fn deallocate, void* user_data);
void stackimport_internal_note_allocation_failure();
void stackimport_internal_reset_allocation_failure();
bool stackimport_internal_had_allocation_failure();
stackimport_file_handle stackimport_internal_open_file(const char* path, const char* mode);
size_t stackimport_internal_read_file(stackimport_file_handle file, void* data, size_t size);
size_t stackimport_internal_write_file(stackimport_file_handle file, const void* data, size_t size);
int stackimport_internal_close_file(stackimport_file_handle file);
int stackimport_internal_make_directory(const char* path);

class stackimport_platform_scope {
public:
	explicit stackimport_platform_scope(const stackimport_internal_platform& platform);
	~stackimport_platform_scope();

	stackimport_platform_scope(const stackimport_platform_scope&) = delete;
	stackimport_platform_scope& operator=(const stackimport_platform_scope&) = delete;

private:
	const stackimport_internal_platform* previous_;
};
