#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STACKIMPORT_API_VERSION 3u

typedef struct stackimport_context stackimport_context;

typedef enum stackimport_status {
	STACKIMPORT_STATUS_OK = 0,
	STACKIMPORT_STATUS_INVALID_ARGUMENT = 1,
	STACKIMPORT_STATUS_ALLOCATION_FAILED = 2,
	STACKIMPORT_STATUS_IMPORT_FAILED = 3,
	STACKIMPORT_STATUS_UNSUPPORTED_OPTION = 4
} stackimport_status;

typedef enum stackimport_import_flags {
	STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS = 1u << 0,
	STACKIMPORT_IMPORT_NO_STATUS = 1u << 1,
	STACKIMPORT_IMPORT_NO_PROGRESS = 1u << 2,
	STACKIMPORT_IMPORT_RAW_GRAPHICS = 1u << 3
} stackimport_import_flags;

typedef void* (*stackimport_allocate_fn)(size_t size, size_t alignment, void* user_data);
typedef void (*stackimport_deallocate_fn)(void* ptr, void* user_data);
typedef void (*stackimport_message_fn)(uint32_t severity, const char* message, void* user_data);

typedef void* stackimport_file_handle;
typedef stackimport_file_handle (*stackimport_open_file_fn)(const char* path, const char* mode, void* user_data);
typedef size_t (*stackimport_write_file_fn)(stackimport_file_handle file, const void* data, size_t size, void* user_data);
typedef int (*stackimport_close_file_fn)(stackimport_file_handle file, void* user_data);
typedef int (*stackimport_make_directory_fn)(const char* path, void* user_data);

typedef enum stackimport_message_severity {
	STACKIMPORT_MESSAGE_INFO = 0,
	STACKIMPORT_MESSAGE_WARNING = 1,
	STACKIMPORT_MESSAGE_ERROR = 2,
	STACKIMPORT_MESSAGE_FATAL = 3
} stackimport_message_severity;

typedef struct stackimport_allocator {
	uint32_t struct_size;
	stackimport_allocate_fn allocate;
	stackimport_deallocate_fn deallocate;
	void* user_data;
} stackimport_allocator;

typedef struct stackimport_platform {
	uint32_t struct_size;
	stackimport_allocate_fn allocate;
	stackimport_deallocate_fn deallocate;
	stackimport_message_fn message;
	stackimport_open_file_fn open_file;
	stackimport_write_file_fn write_file;
	stackimport_close_file_fn close_file;
	stackimport_make_directory_fn make_directory;
	void* user_data;
} stackimport_platform;

typedef struct stackimport_import_options {
	uint32_t struct_size;
	uint32_t flags;
	/* Callers own path resolution and output package naming. */
	const char* input_path;
	const char* output_package_path;
} stackimport_import_options;

uint32_t stackimport_api_version(void);

const char* stackimport_status_string(stackimport_status status);

/* Initialize public structs before filling caller-owned fields. */
void stackimport_allocator_init(stackimport_allocator* allocator);
void stackimport_platform_init(stackimport_platform* platform);
void stackimport_import_options_init(stackimport_import_options* options);

size_t stackimport_context_size(void);
size_t stackimport_context_alignment(void);

stackimport_status stackimport_context_init(
	void* storage,
	size_t storage_size,
	stackimport_context** out_context);

stackimport_status stackimport_context_init_with_platform(
	void* storage,
	size_t storage_size,
	const stackimport_platform* platform,
	stackimport_context** out_context);

void stackimport_context_deinit(stackimport_context* context);

stackimport_status stackimport_context_create(
	const stackimport_allocator* allocator,
	stackimport_context** out_context);

stackimport_status stackimport_context_create_with_platform(
	const stackimport_platform* platform,
	stackimport_context** out_context);

void stackimport_context_destroy(stackimport_context* context);

stackimport_status stackimport_import(
	stackimport_context* context,
	const stackimport_import_options* options);

#ifdef __cplusplus
}
#endif
