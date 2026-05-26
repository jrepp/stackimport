#include "stackimport_c.h"
#include "stackimport_logging.h"
#include "include/stackimport_sax.hpp"
#include "RomDasm.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
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
#define SYNTAXSTR "[--nodumprawblocks] [--dumprawblocks] [--nostatus] [--noprogress] [--rawgraphics] [--scan] [--rom [--rom-base <address>]] [--output <packagePath>] <originalStackPath>"

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

std::string json_escape(const std::string& text)
{
	std::string result;
	result.reserve(text.size() + 8);
	for(char ch : text)
	{
		switch(ch)
		{
			case '\\': result += "\\\\"; break;
			case '"': result += "\\\""; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default:
				if(static_cast<unsigned char>(ch) < 0x20)
				{
					char escaped[8] = {};
					snprintf(escaped, sizeof(escaped), "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(ch)));
					result += escaped;
				}
				else
				{
					result.push_back(ch);
				}
				break;
		}
	}
	return result;
}

bool read_entire_file(const std::string& path, std::vector<uint8_t>& out)
{
	out.clear();
	FILE* file = fopen(path.c_str(), "rb");
	if(!file)
		return false;
	if(fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return false;
	}
	const long fileSize = ftell(file);
	if(fileSize < 0 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return false;
	}
	out.resize(static_cast<size_t>(fileSize));
	const bool ok = fileSize == 0 || fread(out.data(), 1, out.size(), file) == out.size();
	const int closeStatus = fclose(file);
	return ok && closeStatus == 0;
}

bool write_text_file(const std::string& path, const std::string& text)
{
	FILE* file = fopen(path.c_str(), "wb");
	if(!file)
		return false;
	const bool ok = text.empty() || fwrite(text.data(), 1, text.size(), file) == text.size();
	const int closeStatus = fclose(file);
	return ok && closeStatus == 0;
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
	bool scanMode = false;
	bool romMode = false;
	uint32_t romBaseAddress = 0;
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
			else if( strcmp(argv[x],"--scan") == 0 )
				scanMode = true;
			else if( strcmp(argv[x],"--rom") == 0 )
				romMode = true;
			else if( strcmp(argv[x],"--rom-base") == 0 )
			{
				x++;
				if( x >= argc )
				{
					stackimport_quill_diagnosticf( "Error: Missing address after --rom-base, syntax is %s " SYNTAXSTR "\n", argv[0] );
					return 3;
				}
				char* end = nullptr;
				const unsigned long parsedBase = std::strtoul(argv[x], &end, 0);
				if(!end || *end != '\0' || parsedBase > UINT32_MAX)
				{
					stackimport_quill_diagnosticf( "Error: Invalid --rom-base address '%s'.\n", argv[x] );
					return 3;
				}
				romBaseAddress = static_cast<uint32_t>(parsedBase);
			}
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
	if( scanMode )
	{
		std::vector<uint8_t> buf;
		if(!read_entire_file(fpath, buf))
		{
			stackimport_quill_diagnosticf("Error: Failed to read '%s'.\n", fpath.c_str());
			stackimport_logging_shutdown();
			return 5;
		}

		rsrcd::Bytes data{buf.data(), buf.size()};

		struct ScanOutput : stackimport::IBlockOutput {
			int count = 0;

			auto on_block(const stackimport::BlockRef& ref, stackimport::IStackReader&) -> BlockResult override {
				stackimport_quill_diagnosticf("  %08llx  %5u  %.*s  %5d  %7u\n",
					static_cast<unsigned long long>(ref.file_offset),
					ref.payload_bytes + 12u,
					4, ref.type.v,
					ref.id.get(),
					ref.payload_bytes);
				count++;
				return BlockResult::Continue;
			}
			auto on_error(const char* msg) -> bool override {
				stackimport_quill_diagnosticf("  ERROR: %s\n", msg);
				return false;
			}
		};

		ScanOutput scan_out;
		stackimport_quill_diagnosticf("Scanning: %s\n", fpath.c_str());
		stackimport_quill_diagnosticf("  Offset   Size  Type      ID  Payload\n");

		stackimport::BlockParser parser;
		auto err = parser.parse_view(data, scan_out);

		if(err != stackimport::BlockErr::None)
		{
			stackimport_quill_diagnosticf("Scan error: parse failed (code %d).\n", static_cast<int>(err));
			stackimport_logging_shutdown();
			return 5;
		}

		stackimport_quill_diagnosticf("Total blocks: %d\n", scan_out.count);
		stackimport_logging_shutdown();
		return 0;
	}
	if( romMode )
	{
		std::vector<uint8_t> buf;
		if(!read_entire_file(fpath, buf))
		{
			stackimport_quill_diagnosticf("Error: Failed to read '%s'.\n", fpath.c_str());
			stackimport_logging_shutdown();
			return 5;
		}

		const auto filename = fpath.substr(fpath.find_last_of("/\\") + 1);
		stackimport::RomDasm::RomInfo romInfo = stackimport::RomDasm::analyze_rom_header(
			std::span<const uint8_t>(buf.data(), buf.size()), romBaseAddress);
		romInfo.filename = filename;

		stackimport::RomDasm::ScanOptions options = {};
		options.start_address = romBaseAddress;
		options.disassemble_code = true;
		options.detect_pointer_tables = true;
		options.min_string_length = 4;

		stackimport_quill_diagnosticf("Analyzing ROM: %s (%u bytes, base $%08X)\n",
			filename.c_str(), static_cast<unsigned>(buf.size()), static_cast<unsigned>(romBaseAddress));

		auto analysis = stackimport::RomDasm::scan_rom(
			std::span<const uint8_t>(buf.data(), buf.size()), romInfo, options);

		stackimport_quill_diagnosticf("Found %zu code regions, %zu strings, %zu pointer tables\n",
			analysis.code_regions.size(), analysis.strings.size(), analysis.pointer_tables.size());
		stackimport_quill_diagnosticf("Total instructions: %zu\n", analysis.total_instructions);

		std::string outputDir = outputPathArgument ? absolute_path(outputPathArgument) : fpath + ".rom";
		if(cli_make_directory(outputDir.c_str(), nullptr) != 0)
		{
			stackimport_quill_diagnosticf("Error: Failed to create output directory '%s'.\n", outputDir.c_str());
			stackimport_logging_shutdown();
			return 5;
		}

		std::string dasmPath = outputDir + "/disassembly.s";
		std::string disasm = stackimport::RomDasm::export_disassembly(analysis, true);
		if(!write_text_file(dasmPath, disasm))
		{
			stackimport_quill_diagnosticf("Error: Failed to write '%s'.\n", dasmPath.c_str());
			stackimport_logging_shutdown();
			return 5;
		}
		stackimport_quill_diagnosticf("Wrote: %s\n", dasmPath.c_str());

		std::string jsonPath = outputDir + "/analysis.json";
		FILE* jsonFile = fopen(jsonPath.c_str(), "w");
		if(jsonFile) {
			fprintf(jsonFile, "{\n");
			fprintf(jsonFile, "  \"filename\": \"%s\",\n", json_escape(filename).c_str());
			fprintf(jsonFile, "  \"size\": %u,\n", static_cast<unsigned>(analysis.info.size));
			fprintf(jsonFile, "  \"crc32\": \"%08X\",\n", static_cast<unsigned>(analysis.info.crc32));
			fprintf(jsonFile, "  \"sha256\": \"%s\",\n", json_escape(analysis.info.sha256).c_str());
			fprintf(jsonFile, "  \"base_address\": \"0x%08X\",\n", static_cast<unsigned>(romBaseAddress));
			fprintf(jsonFile, "  \"machine_family\": \"%s\",\n", json_escape(analysis.info.machine_family).c_str());
			fprintf(jsonFile, "  \"code_regions\": %zu,\n", analysis.code_regions.size());
			fprintf(jsonFile, "  \"strings\": %zu,\n", analysis.strings.size());
			fprintf(jsonFile, "  \"pointer_tables\": %zu,\n", analysis.pointer_tables.size());
			fprintf(jsonFile, "  \"pointer_table_regions\": %zu,\n", analysis.pointer_table_regions.size());
			fprintf(jsonFile, "  \"function_candidates\": %zu,\n", analysis.function_candidates.size());
			fprintf(jsonFile, "  \"data_regions\": %zu,\n", analysis.data_regions.size());
			fprintf(jsonFile, "  \"resource_markers\": %zu,\n", analysis.resource_markers.size());
			fprintf(jsonFile, "  \"total_instructions\": %zu,\n", analysis.total_instructions);
			fprintf(jsonFile, "  \"function_candidates_sample\": [\n");
			for(size_t i = 0; i < analysis.function_candidates.size() && i < 200; i++) {
				const auto& fn = analysis.function_candidates[i];
				fprintf(jsonFile,
					"    {\"address\":\"%08X\",\"label\":\"%s\",\"calls\":%zu,\"jumps\":%zu,\"references\":%zu,\"confidence\":%.2f}%s\n",
					static_cast<unsigned>(fn.address),
					json_escape(fn.label).c_str(),
					fn.calls,
					fn.jumps,
					fn.references,
					fn.confidence,
					(i + 1 < analysis.function_candidates.size() && i + 1 < 200) ? "," : "");
			}
			fprintf(jsonFile, "  ],\n");
			fprintf(jsonFile, "  \"pointer_table_regions_sample\": [\n");
			for(size_t i = 0; i < analysis.pointer_table_regions.size() && i < 120; i++) {
				const auto& table = analysis.pointer_table_regions[i];
				fprintf(jsonFile,
					"    {\"address\":\"%08X\",\"entry_count\":%zu,\"targets\":[",
					static_cast<unsigned>(table.address),
					table.entry_count);
				for(size_t j = 0; j < table.targets.size() && j < 16; j++) {
					fprintf(jsonFile, "%s\"%08X\"", j ? "," : "", static_cast<unsigned>(table.targets[j]));
				}
				fprintf(jsonFile, "]}%s\n", (i + 1 < analysis.pointer_table_regions.size() && i + 1 < 120) ? "," : "");
			}
			fprintf(jsonFile, "  ],\n");
			fprintf(jsonFile, "  \"data_regions_sample\": [\n");
			for(size_t i = 0; i < analysis.data_regions.size() && i < 120; i++) {
				const auto& region = analysis.data_regions[i];
				fprintf(jsonFile,
					"    {\"start\":\"%08X\",\"end\":\"%08X\",\"kind\":\"%s\",\"item_count\":%zu,\"confidence\":%.2f}%s\n",
					static_cast<unsigned>(region.start_address),
					static_cast<unsigned>(region.end_address),
					json_escape(region.kind).c_str(),
					region.item_count,
					region.confidence,
					(i + 1 < analysis.data_regions.size() && i + 1 < 120) ? "," : "");
			}
			fprintf(jsonFile, "  ],\n");
			fprintf(jsonFile, "  \"resource_markers_sample\": [\n");
			for(size_t i = 0; i < analysis.resource_markers.size() && i < 120; i++) {
				const auto& marker = analysis.resource_markers[i];
				fprintf(jsonFile,
					"    {\"address\":\"%08X\",\"type\":\"%s\",\"context\":\"%s\"}%s\n",
					static_cast<unsigned>(marker.address),
					json_escape(marker.type).c_str(),
					json_escape(marker.context).c_str(),
					(i + 1 < analysis.resource_markers.size() && i + 1 < 120) ? "," : "");
			}
			fprintf(jsonFile, "  ]\n");
			fprintf(jsonFile, "}\n");
			if(fclose(jsonFile) != 0)
			{
				stackimport_quill_diagnosticf("Error: Failed to close '%s'.\n", jsonPath.c_str());
				stackimport_logging_shutdown();
				return 5;
			}
		}
		else
		{
			stackimport_quill_diagnosticf("Error: Failed to write '%s'.\n", jsonPath.c_str());
			stackimport_logging_shutdown();
			return 5;
		}
		stackimport_quill_diagnosticf("Wrote: %s\n", jsonPath.c_str());

		stackimport_quill_diagnosticf("ROM analysis complete. Output: %s\n", outputDir.c_str());
		stackimport_logging_shutdown();
		return 0;
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
	platform.read_file = cli_read_file;
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
