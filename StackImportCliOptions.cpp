#include "StackImportCli.h"

#include "stackimport_logging.h"

#include <climits>
#include <cstdlib>
#include <cstring>

namespace stackimport::cli {

const char* syntax_string()
{
	return "[--nodumprawblocks] [--dumprawblocks] [--nostatus] [--noprogress] [--rawgraphics] [--scan] [--rom [--rom-base <address>] [--emit-atlas] [--emit-json] [--emit-assets] [--atlas-output <path>] [--source-root <path>]] [--output <packagePath>] <originalStackPath>";
}

namespace {

bool option_requires_value(int argc, int& index, const char* option)
{
	index++;
	if(index < argc)
		return true;
	stackimport_quill_diagnosticf("Error: Missing value after %s, syntax is stackimport %s\n", option, syntax_string());
	return false;
}

bool parse_rom_base(const char* text, uint32_t& out)
{
	char* end = nullptr;
	const unsigned long parsed = std::strtoul(text, &end, 0);
	if(!end || *end != '\0' || parsed > UINT32_MAX)
		return false;
	out = static_cast<uint32_t>(parsed);
	return true;
}

}

int parse_arguments(int argc, char* const argv[], Options& options)
{
	if(argc < 2)
	{
		stackimport_quill_diagnosticf("Error: Syntax is %s %s\n", argv[0], syntax_string());
		return 2;
	}

	const char* inputPathArgument = nullptr;
	for(int index = 1; index < argc; index++)
	{
		if(strcmp(argv[index], "--dumprawblocks") == 0)
			options.flags |= STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS;
		else if(strcmp(argv[index], "--nodumprawblocks") == 0)
			options.flags &= static_cast<uint32_t>(~STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS);
		else if(strcmp(argv[index], "--nostatus") == 0)
			options.flags |= STACKIMPORT_IMPORT_NO_STATUS;
		else if(strcmp(argv[index], "--noprogress") == 0)
			options.flags |= STACKIMPORT_IMPORT_NO_PROGRESS;
		else if(strcmp(argv[index], "--rawgraphics") == 0)
			options.flags |= STACKIMPORT_IMPORT_RAW_GRAPHICS;
		else if(strcmp(argv[index], "--scan") == 0)
			options.mode = Mode::Scan;
		else if(strcmp(argv[index], "--rom") == 0)
			options.mode = Mode::Rom;
		else if(strcmp(argv[index], "--emit-atlas") == 0)
			options.emit_atlas = true;
		else if(strcmp(argv[index], "--emit-json") == 0)
			options.emit_json = true;
		else if(strcmp(argv[index], "--emit-assets") == 0)
			options.emit_assets = true;
		else if(strcmp(argv[index], "--rom-base") == 0)
		{
			if(!option_requires_value(argc, index, "--rom-base"))
				return 3;
			if(!parse_rom_base(argv[index], options.rom_base_address))
			{
				stackimport_quill_diagnosticf("Error: Invalid --rom-base address '%s'.\n", argv[index]);
				return 3;
			}
		}
		else if(strcmp(argv[index], "--atlas-output") == 0)
		{
			if(!option_requires_value(argc, index, "--atlas-output"))
				return 3;
			options.atlas_output_path = absolute_path(argv[index]);
		}
		else if(strcmp(argv[index], "--source-root") == 0)
		{
			if(!option_requires_value(argc, index, "--source-root"))
				return 3;
			options.source_root_path = absolute_path(argv[index]);
		}
		else if(strcmp(argv[index], "--output") == 0)
		{
			if(!option_requires_value(argc, index, "--output"))
				return 3;
			options.output_path = absolute_path(argv[index]);
		}
		else if(argv[index][0] == '-')
		{
			stackimport_quill_diagnosticf("Error: Unknown option %s, syntax is %s %s\n", argv[index], argv[0], syntax_string());
			return 3;
		}
		else
		{
			if(inputPathArgument)
			{
				stackimport_quill_diagnosticf("Error: Multiple input paths supplied: '%s' and '%s'.\n", inputPathArgument, argv[index]);
				return 3;
			}
			inputPathArgument = argv[index];
		}
	}

	if(!inputPathArgument)
	{
		stackimport_quill_diagnosticf("Error: Syntax is %s %s\n", argv[0], syntax_string());
		return 4;
	}

	options.input_path = absolute_path(inputPathArgument);
	if(options.input_path.empty())
	{
		stackimport_quill_diagnosticf("Error: Could not resolve current working directory.\n");
		return 5;
	}
	return 0;
}

} // namespace stackimport::cli
