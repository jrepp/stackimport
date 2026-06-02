#include "StackImportCli.h"

#include "stackimport_logging.h"
#include "stackimport_version.h"

#include <CLI/CLI.hpp>

#include <climits>
#include <cstdlib>
#include <string>

namespace stackimport::cli {

const char* syntax_string()
{
	return "[import|scan|rom] [options] <input>";
}

namespace {

struct ParsedFlags {
	bool dump_raw_blocks = false;
	bool no_dump_raw_blocks = false;
	bool no_status = false;
	bool no_progress = false;
	bool raw_graphics = false;
	bool legacy_scan = false;
	bool legacy_rom = false;
	std::string input_path;
	std::string output_path;
	std::string media_root_path;
	std::string quicktime_default_palette_path;
	uint32_t quicktime_frame_limit = 16;
	bool media_reference_only = false;
	std::string atlas_output_path;
	std::string source_root_path;
	std::string rom_base;
	bool emit_atlas = false;
	bool emit_json = false;
	bool emit_assets = false;
	bool emit_resource_index = false;
	std::string resource_type;
	int32_t resource_id = 0;
	bool resource_converted = true;
};

void add_stack_flags(CLI::App& app, ParsedFlags& flags)
{
	app.add_flag("--dumprawblocks", flags.dump_raw_blocks, "Dump raw stack blocks into the output package");
	app.add_flag("--nodumprawblocks", flags.no_dump_raw_blocks, "Do not dump raw stack blocks");
	app.add_flag("--nostatus", flags.no_status, "Suppress status messages");
	app.add_flag("--noprogress", flags.no_progress, "Suppress progress messages");
	app.add_flag("--rawgraphics", flags.raw_graphics, "Write raw graphic payloads where available");
}

void add_output_option(CLI::App& app, ParsedFlags& flags)
{
	app.add_option("-o,--output", flags.output_path, "Output package or analysis directory");
}

void add_import_options(CLI::App& app, ParsedFlags& flags)
{
	app.add_option("--media-root", flags.media_root_path, "Package loose external media from this directory");
	app.add_flag("--media-reference-only", flags.media_reference_only, "Reference loose media in the manifest without copying source bytes");
	app.add_option("--quicktime-default-palette", flags.quicktime_default_palette_path, "ROM clut JSON to use for default-palette 8-bit QuickTime video");
	app.add_option("--quicktime-frame-limit", flags.quicktime_frame_limit, "Maximum decoded QuickTime video frames per track; 0 exports all frames");
}

void add_rom_options(CLI::App& app, ParsedFlags& flags)
{
	app.add_option("--rom-base", flags.rom_base, "Base address for ROM virtual addresses, such as 0x40800000");
	app.add_option("--atlas-output", flags.atlas_output_path, "Directory for atlas TSV outputs");
	app.add_option("--source-root", flags.source_root_path, "SuperMario source root used by correlation passes");
	app.add_flag("--emit-atlas", flags.emit_atlas, "Emit atlas TSV interchange files");
	app.add_flag("--emit-json", flags.emit_json, "Emit machine-readable JSON analysis output");
	app.add_flag("--emit-assets", flags.emit_assets, "Emit converted or preserved asset outputs when supported");
	app.add_flag("--emit-resource-index", flags.emit_resource_index, "Emit a portable ROM resource index for converter lookups");
}

void add_convert_options(CLI::App& app, ParsedFlags& flags)
{
	app.add_option("-t,--type", flags.resource_type, "Four-byte resource type such as PICT, snd , ICON, cicn");
	app.add_option("-i,--id", flags.resource_id, "Numeric resource ID");
	app.add_flag("--native", flags.resource_converted, "Output native format instead of converted");
}

bool parse_rom_base(const std::string& text, uint32_t& out)
{
	if(text.empty())
		return true;
	char* end = nullptr;
	const unsigned long parsed = std::strtoul(text.c_str(), &end, 0);
	if(!end || *end != '\0' || parsed > UINT32_MAX)
		return false;
	out = static_cast<uint32_t>(parsed);
	return true;
}

void apply_shared_flags(const ParsedFlags& parsed, Options& options)
{
	if(parsed.dump_raw_blocks)
		options.flags |= STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS;
	if(parsed.no_dump_raw_blocks)
		options.flags &= static_cast<uint32_t>(~STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS);
	if(parsed.no_status)
		options.flags |= STACKIMPORT_IMPORT_NO_STATUS;
	if(parsed.no_progress)
		options.flags |= STACKIMPORT_IMPORT_NO_PROGRESS;
	if(parsed.raw_graphics)
		options.flags |= STACKIMPORT_IMPORT_RAW_GRAPHICS;

	if(!parsed.output_path.empty())
		options.output_path = absolute_path(parsed.output_path.c_str());
	if(!parsed.media_root_path.empty())
		options.media_root_path = absolute_path(parsed.media_root_path.c_str());
	if(!parsed.quicktime_default_palette_path.empty())
		options.quicktime_default_palette_path = absolute_path(parsed.quicktime_default_palette_path.c_str());
	options.quicktime_frame_limit = parsed.quicktime_frame_limit;
	options.media_reference_only = parsed.media_reference_only;
	if(!parsed.atlas_output_path.empty())
		options.atlas_output_path = absolute_path(parsed.atlas_output_path.c_str());
	if(!parsed.source_root_path.empty())
		options.source_root_path = absolute_path(parsed.source_root_path.c_str());
	options.emit_atlas = parsed.emit_atlas;
	options.emit_json = parsed.emit_json;
	options.emit_assets = parsed.emit_assets;
	options.emit_resource_index = parsed.emit_resource_index;
}

int finalize_options(
	const char* executable,
	const CLI::App& app,
	const ParsedFlags& root,
	const ParsedFlags& importFlags,
	const ParsedFlags& scanFlags,
	const ParsedFlags& romFlags,
	const ParsedFlags& convertFlags,
	const CLI::App* importCommand,
	const CLI::App* scanCommand,
	const CLI::App* romCommand,
	const CLI::App* convertCommand,
	Options& options)
{
	const ParsedFlags* selected = &root;
	if(*importCommand)
	{
		options.mode = Mode::Import;
		selected = &importFlags;
	}
	else if(*scanCommand)
	{
		options.mode = Mode::Scan;
		selected = &scanFlags;
	}
	else if(*romCommand)
	{
		options.mode = Mode::Rom;
		selected = &romFlags;
	}
	else if(*convertCommand)
	{
		options.mode = Mode::Convert;
		selected = &convertFlags;
	}
	else if(root.legacy_scan)
	{
		options.mode = Mode::Scan;
	}
	else if(root.legacy_rom)
	{
		options.mode = Mode::Rom;
	}

	apply_shared_flags(root, options);
	if(selected != &root)
		apply_shared_flags(*selected, options);

	const std::string inputPath = selected->input_path.empty() ? root.input_path : selected->input_path;
	if(inputPath.empty())
	{
		stackimport_quill_diagnosticf("Error: Missing input path.\n%s\n", app.help().c_str());
		return 4;
	}

	if(options.mode != Mode::Rom && (!root.rom_base.empty() || root.emit_atlas || root.emit_json || root.emit_assets ||
		root.emit_resource_index ||
		!root.atlas_output_path.empty() || !root.source_root_path.empty()))
	{
		stackimport_quill_diagnosticf("Error: ROM options require --rom or the rom subcommand.\n");
		return 3;
	}
	if(options.mode != Mode::Import && (!root.media_root_path.empty() || !root.quicktime_default_palette_path.empty()))
	{
		stackimport_quill_diagnosticf("Error: QuickTime import options require import mode.\n");
		return 3;
	}
	if(options.mode == Mode::Convert)
	{
		if(root.resource_type.empty())
		{
			stackimport_quill_diagnosticf("Error: --type is required for convert mode.\n");
			return 3;
		}
		options.resource_type = root.resource_type;
		options.resource_id = convertFlags.resource_id;
		options.resource_converted = convertFlags.resource_id == 0 ? true : convertFlags.resource_converted;
	}

	const std::string romBaseText = selected->rom_base.empty() ? root.rom_base : selected->rom_base;
	if(!parse_rom_base(romBaseText, options.rom_base_address))
	{
		stackimport_quill_diagnosticf("Error: Invalid --rom-base address '%s'.\n", romBaseText.c_str());
		return 3;
	}

	options.input_path = absolute_path(inputPath.c_str());
	if(options.input_path.empty())
	{
		stackimport_quill_diagnosticf("Error: Could not resolve current working directory for %s.\n", executable);
		return 5;
	}
	return 0;
}

}

int parse_arguments(int argc, char* const argv[], Options& options)
{
	ParsedFlags root;
	ParsedFlags importFlags;
	ParsedFlags scanFlags;
	ParsedFlags romFlags;
	ParsedFlags convertFlags;

	CLI::App app{"Read HyperCard stacks and analyze classic Mac ROM images."};
	app.option_defaults()->always_capture_default();
	app.set_version_flag("--version", STACKIMPORT_VERSION_STRING, "Print stackimport version and exit");
	app.require_subcommand(0, 1);
	app.footer(
		"Examples:\n"
		"  stackimport Resources.stak\n"
		"  stackimport scan Resources.stak\n"
		"  stackimport rom --rom-base 0x40800000 --emit-atlas --atlas-output atlas/maps/rom input.ROM\n"
		"\n"
		"Legacy --scan and --rom flags are still accepted.");

	add_stack_flags(app, root);
	add_output_option(app, root);
	add_import_options(app, root);
	app.add_flag("--scan", root.legacy_scan, "Legacy spelling for the scan subcommand");
	app.add_flag("--rom", root.legacy_rom, "Legacy spelling for the rom subcommand");
	add_rom_options(app, root);
	app.add_option("input", root.input_path, "Input stack or ROM path");

	CLI::App* importCommand = app.add_subcommand("import", "Import a HyperCard stack into a .xstk package");
	add_stack_flags(*importCommand, importFlags);
	add_output_option(*importCommand, importFlags);
	add_import_options(*importCommand, importFlags);
	importCommand->add_option("input", importFlags.input_path, "Input HyperCard stack path")->required();

	CLI::App* scanCommand = app.add_subcommand("scan", "Print the HyperCard block table without importing");
	scanCommand->add_option("input", scanFlags.input_path, "Input HyperCard stack path")->required();

	CLI::App* romCommand = app.add_subcommand("rom", "Analyze an Old World Macintosh ROM image");
	add_output_option(*romCommand, romFlags);
	add_rom_options(*romCommand, romFlags);
	romCommand->add_option("input", romFlags.input_path, "Input ROM path")->required();

	CLI::App* convertCommand = app.add_subcommand("convert", "Convert a loose resource file to modern format");
	add_output_option(*convertCommand, convertFlags);
	add_convert_options(*convertCommand, convertFlags);
	convertCommand->add_option("input", convertFlags.input_path, "Input resource file path")->required();

	try
	{
		app.parse(argc, argv);
	}
	catch(const CLI::ParseError& error)
	{
		const int exitCode = app.exit(error);
		if(exitCode == 0)
			options.exit_after_parse = true;
		return exitCode;
	}

	return finalize_options(
		argv[0],
		app,
		root,
		importFlags,
		scanFlags,
		romFlags,
		convertFlags,
		importCommand,
		scanCommand,
		romCommand,
		convertCommand,
		options);
}

} // namespace stackimport::cli
