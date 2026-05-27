#include "StackImportCli.h"

#include "stackimport_logging.h"

#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <malloc.h>
#endif

namespace stackimport::cli {

namespace {

void* cli_allocate(size_t size, size_t alignment, void*)
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

void cli_deallocate(void* ptr, void*)
{
#if defined(_WIN32)
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

void cli_message(uint32_t severity, const char* message, void*)
{
	stackimport_quill_log_message(severity, message);
}

stackimport_file_handle cli_open_file(const char* path, const char* mode, void*)
{
	return fopen(path, mode);
}

size_t cli_read_file(stackimport_file_handle file, void* data, size_t size, void*)
{
	return fread(data, 1, size, static_cast<FILE*>(file));
}

size_t cli_write_file(stackimport_file_handle file, const void* data, size_t size, void*)
{
	return fwrite(data, 1, size, static_cast<FILE*>(file));
}

int cli_close_file(stackimport_file_handle file, void*)
{
	return fclose(static_cast<FILE*>(file));
}

int cli_make_directory(const char* path, void*)
{
#if defined(_WIN32)
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}

}

int run_import_mode(const Options& options)
{
	const std::string outputPath = !options.output_path.empty() ?
		options.output_path :
		default_output_package_path(options.input_path);
	if(outputPath.empty())
	{
		stackimport_quill_diagnosticf("Error: Could not resolve output package path.\n");
		return 5;
	}

	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	platform.allocate = cli_allocate;
	platform.deallocate = cli_deallocate;
	platform.message = cli_message;
	platform.open_file = cli_open_file;
	platform.read_file = cli_read_file;
	platform.write_file = cli_write_file;
	platform.close_file = cli_close_file;
	platform.make_directory = cli_make_directory;

	stackimport_context* context = nullptr;
	stackimport_status status = stackimport_context_create_with_platform(&platform, &context);
	if(status != STACKIMPORT_STATUS_OK)
	{
		stackimport_quill_diagnosticf("Error: Could not create import context: %s.\n", stackimport_status_string(status));
		return 5;
	}

	stackimport_import_options importOptions = {};
	stackimport_import_options_init(&importOptions);
	importOptions.flags = options.flags;
	importOptions.input_path = options.input_path.c_str();
	importOptions.output_package_path = outputPath.c_str();

	status = stackimport_import(context, &importOptions);
	stackimport_context_destroy(context);
	if(status != STACKIMPORT_STATUS_OK)
	{
		stackimport_quill_diagnosticf("Error: Conversion of '%s' incomplete/failed: %s.\n", options.input_path.c_str(), stackimport_status_string(status));
		return 5;
	}

	return 0;
}

} // namespace stackimport::cli
