#include "stackimport_c.h"
#include "stackimport_logging.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <malloc.h>
#else
#include <unistd.h>
#endif

#if defined(_WIN32) && !defined(PATH_MAX)
#define PATH_MAX _MAX_PATH
#endif


void	RunTests();


// Arguments as string for syntax info
#define SYNTAXSTR "[--nodumprawblocks] [--dumprawblocks] [--nostatus] [--noprogress] [--rawgraphics] [--output <packagePath>] <originalStackPath>"

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

std::string absolute_path(const char* path)
{
	std::string result(path ? path : "");
	if(!result.empty() && (result[0] == '/'
#if defined(_WIN32)
		|| result[0] == '\\' || (result.size() > 1 && result[1] == ':')
#endif
		))
		return result;

	char cwd[PATH_MAX + 1] = {0};
#if defined(_WIN32)
	if(!_getcwd(cwd, sizeof(cwd)))
#else
	if(!getcwd(cwd, sizeof(cwd)))
#endif
		return std::string();

	std::string fullpath = cwd;
	if(!fullpath.empty() && fullpath[fullpath.size() - 1] != '/'
#if defined(_WIN32)
		&& fullpath[fullpath.size() - 1] != '\\'
#endif
		)
#if defined(_WIN32)
		fullpath += '\\';
#else
		fullpath += '/';
#endif
	fullpath += result;
	return fullpath;
}

std::string default_output_package_path(const std::string& input_path)
{
	std::string package_path(input_path);
	const std::string::size_type slash = package_path.find_last_of("/\\");
	const std::string::size_type dot = package_path.find_last_of('.');
	if(dot != std::string::npos && (slash == std::string::npos || dot > slash))
	{
		package_path.resize(dot);
	}
	package_path.append(".xstk");
	return package_path;
}

}


int main( int argc, char * const argv[] )
{
	stackimport_logging_init();
	#if defined(DEBUG) && DEBUG
	RunTests();
	#endif
	
	if( argc < 2 )
	{
		stackimport_quill_diagnosticf( "Error: Syntax is %s " SYNTAXSTR "\n", argv[0] );
		return 2;
	}
	
	uint32_t flags = STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS;
	const char* outputPathArgument = nullptr;
	int		x = 1;	// Skip command name in argv[0].
	if( argc > 2 )
	{
		for( ; x < argc; x++ )
		{
			if( strcmp(argv[x],"--dumprawblocks") == 0 )
				flags |= STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS;
			else if( strcmp(argv[x],"--nodumprawblocks") == 0 )
				flags &= static_cast<uint32_t>(~STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS);
			else if( strcmp(argv[x],"--nostatus") == 0 )
				flags |= STACKIMPORT_IMPORT_NO_STATUS;
			else if( strcmp(argv[x],"--noprogress") == 0 )
				flags |= STACKIMPORT_IMPORT_NO_PROGRESS;
			else if( strcmp(argv[x],"--rawgraphics") == 0 )
				flags |= STACKIMPORT_IMPORT_RAW_GRAPHICS;
			else if( strcmp(argv[x],"--output") == 0 )
			{
				x++;
				if( x >= argc )
				{
					stackimport_quill_diagnosticf( "Error: Missing path after --output, syntax is %s " SYNTAXSTR "\n", argv[0] );
					return 3;
				}
				outputPathArgument = argv[x];
			}
			else if( argv[x][0] == '-' )
			{
				stackimport_quill_diagnosticf( "Error: Unknown option %s, syntax is %s " SYNTAXSTR "\n", argv[x], argv[0] );
				return 3;
			}
			else	// Doesn't start with a dash? Must be pathname!
				break;	// End of options, exit loop.
		}
	}
	
	if( x >= argc )	// Only options, no file path?
	{
		stackimport_quill_diagnosticf( "Error: Syntax is %s " SYNTAXSTR "\n", argv[0] );
		return 4;
	}
	
	std::string	fpath = absolute_path(argv[x]);
	if( fpath.empty() )
	{
		stackimport_quill_diagnosticf( "Error: Could not resolve current working directory.\n" );
		return 5;
	}
	const std::string outputPath = outputPathArgument ?
		absolute_path(outputPathArgument) :
		default_output_package_path(fpath);
	if( outputPath.empty() )
	{
		stackimport_quill_diagnosticf( "Error: Could not resolve output package path.\n" );
		return 5;
	}

	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	platform.allocate = cli_allocate;
	platform.deallocate = cli_deallocate;
	platform.message = cli_message;
	platform.open_file = cli_open_file;
	platform.write_file = cli_write_file;
	platform.close_file = cli_close_file;
	platform.make_directory = cli_make_directory;

	stackimport_context* context = nullptr;
	stackimport_status status = stackimport_context_create_with_platform(&platform, &context);
	if( status != STACKIMPORT_STATUS_OK )
	{
		stackimport_quill_diagnosticf( "Error: Could not create import context: %s.\n", stackimport_status_string(status) );
		return 5;
	}

	stackimport_import_options options = {};
	stackimport_import_options_init(&options);
	options.flags = flags;
	options.input_path = fpath.c_str();
	options.output_package_path = outputPath.c_str();

	status = stackimport_import(context, &options);
	stackimport_context_destroy(context);
	if( status != STACKIMPORT_STATUS_OK )
	{
		stackimport_quill_diagnosticf( "Error: Conversion of '%s' incomplete/failed: %s.\n", fpath.c_str(), stackimport_status_string(status) );
		stackimport_logging_shutdown();
		return 5;
	}
	
	stackimport_logging_shutdown();
    return 0;
}
