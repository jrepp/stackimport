#include "StackImportCli.h"

#include "stackimport_logging.h"
#include "stackimport_version.h"

#include <algorithm>
#include <cstdio>
#include <span>

namespace stackimport::cli {

namespace {

bool write_atlas_outputs(
	const std::string& atlasDir,
	const std::string& inputPath,
	const std::string& outputDir,
	const std::vector<uint8_t>& buf,
	const RomDasm::RomAnalysis& analysis)
{
	if(!make_directories_recursive(atlasDir))
		return false;

	const std::string romId = rom_id_from_info(analysis);
	const std::string inputRel = relative_to_cwd(inputPath);
	const std::string outputRel = relative_to_cwd(outputDir);
	(void)outputRel;
	double firstBytesConfidence = 0.0;
	const char* firstBytesKind = initial_bytes_hypothesis(buf, firstBytesConfidence);

	std::string roms = "id\tpath\tsize\tcrc32\tsha256\tbase_address\tmachine_family\tmodel_tokens\n";
	roms += romId + "\t" + tsv_escape(inputRel) + "\t" + std::to_string(analysis.info.size) + "\t";
	roms += RomDasm::format_address(analysis.info.crc32) + "\t" + analysis.info.sha256 + "\t";
	roms += RomDasm::format_address(analysis.info.base_address) + "\t";
	roms += tsv_escape(analysis.info.machine_family) + "\t";
	for(size_t i = 0; i < analysis.info.model_names.size(); i++)
	{
		if(i)
			roms += ",";
		roms += tsv_escape(analysis.info.model_names[i]);
	}
	roms += "\n";

	std::string inventory = "rom_id\tcode_regions\tstrings\tpointer_entries\tpointer_table_regions\tfunction_candidates\tdata_regions\tresource_markers\tdisassembly_mode\tsource_status\n";
	inventory += romId + "\t" + std::to_string(analysis.code_regions.size()) + "\t";
	inventory += std::to_string(analysis.strings.size()) + "\t" + std::to_string(analysis.pointer_tables.size()) + "\t";
	inventory += std::to_string(analysis.pointer_table_regions.size()) + "\t" + std::to_string(analysis.function_candidates.size()) + "\t";
	inventory += std::to_string(analysis.data_regions.size()) + "\t" + std::to_string(analysis.resource_markers.size()) + "\t";
	inventory += rom_disassembly_mode();
	inventory += "\tunmatched\n";

	std::string regions = "id\trom_id\tstart\tend\tkind\titem_count\tconfidence\tsource\n";
	regions += "region-" + romId + "-first-bytes\t" + romId + "\t";
	regions += RomDasm::format_address(analysis.info.base_address) + "\t";
	regions += RomDasm::format_address(analysis.info.base_address + static_cast<uint32_t>(std::min<size_t>(buf.size(), 32))) + "\t";
	regions += firstBytesKind;
	regions += "\t1\t" + std::to_string(firstBytesConfidence) + "\tidentity:first-bytes\n";
	for(const auto& region : analysis.code_regions)
	{
		regions += "region-" + RomDasm::format_address(region.start_address) + "\t" + romId + "\t";
		regions += RomDasm::format_address(region.start_address) + "\t" + RomDasm::format_address(region.end_address) + "\t";
		regions += (region.is_code ? "code" : "mixed");
		regions += "\t" + std::to_string(analysis.total_instructions) + "\t" + std::to_string(region.confidence) + "\tdisassembly\n";
	}
	for(const auto& region : analysis.data_regions)
	{
		regions += "region-" + RomDasm::format_address(region.start_address) + "\t" + romId + "\t";
		regions += RomDasm::format_address(region.start_address) + "\t" + RomDasm::format_address(region.end_address) + "\t";
		regions += tsv_escape(region.kind) + "\t" + std::to_string(region.item_count) + "\t";
		regions += std::to_string(region.confidence) + "\tscanner\n";
	}

	std::string functions = "id\trom_id\tstart\tend\tkind\tlabel\tinstruction_count\tinbound_calls\toutbound_calls\treferences\tconfidence\tevidence\n";
	for(const auto& fn : analysis.function_candidates)
	{
		const std::string id = "fn-" + RomDasm::format_address(fn.address);
		functions += id + "\t" + romId + "\t" + RomDasm::format_address(fn.address) + "\t\tcandidate\t";
		functions += tsv_escape(fn.label) + "\t0\t" + std::to_string(fn.calls) + "\t";
		functions += std::to_string(fn.jumps) + "\t" + std::to_string(fn.references) + "\t";
		functions += std::to_string(fn.confidence) + "\tinbound references from linear disassembly\n";
	}

	std::string pointerTables = "id\trom_id\taddress\tentry_count\tdecoded_target_count\tconfidence\tsource\n";
	for(const auto& table : analysis.pointer_table_regions)
	{
		pointerTables += "ptrtab-" + RomDasm::format_address(table.address) + "\t" + romId + "\t";
		pointerTables += RomDasm::format_address(table.address) + "\t" + std::to_string(table.entry_count) + "\t";
		pointerTables += std::to_string(table.targets.size()) + "\t0.75\tscanner:aligned-absolute-addresses\n";
	}

	std::string dataRegions = "id\trom_id\tstart\tend\tkind\titem_count\tconfidence\tsource\n";
	for(const auto& region : analysis.data_regions)
	{
		dataRegions += "data-" + RomDasm::format_address(region.start_address) + "\t" + romId + "\t";
		dataRegions += RomDasm::format_address(region.start_address) + "\t" + RomDasm::format_address(region.end_address) + "\t";
		dataRegions += tsv_escape(region.kind) + "\t" + std::to_string(region.item_count) + "\t";
		dataRegions += std::to_string(region.confidence) + "\tscanner\n";
	}

	std::string resources = "id\taddress\tkind\tresource_type\tresource_id\tname\tmedia_type\toutput_file\tconfidence\tsource\n";
	for(size_t i = 0; i < analysis.resource_markers.size(); i++)
	{
		const auto& marker = analysis.resource_markers[i];
		resources += "resmarker-" + RomDasm::format_address(marker.address) + "-" + std::to_string(i) + "\t";
		resources += RomDasm::format_address(marker.address) + "\tmarker\t" + tsv_escape(marker.type);
		resources += "\t\t\t\t\t0.40\tmarker-scan:" + tsv_escape(marker.context) + "\n";
	}

	std::string labels = "id\trom_id\taddress\tname\tkind\tconfidence\tsource\n";
	for(const auto& fn : analysis.function_candidates)
	{
		const std::string address = RomDasm::format_address(fn.address);
		labels += "label-" + address + "\t" + romId + "\t" + address + "\t";
		labels += fn.label.empty() ? ("sub_" + address) : tsv_escape(fn.label);
		labels += "\tfunction_candidate\t" + std::to_string(fn.confidence) + "\tfunction-scanner\n";
	}

	std::string strings = "id\trom_id\taddress\tkind\tlength\ttext\tconfidence\tsource\n";
	for(const auto& s : analysis.strings)
	{
		const uint32_t address = analysis.info.base_address + s.address;
		strings += "str-" + RomDasm::format_address(address) + "\t" + romId + "\t";
		strings += RomDasm::format_address(address) + "\t";
		strings += s.is_pascal ? "pascal" : "ascii";
		strings += "\t" + std::to_string(s.length) + "\t" + tsv_escape(s.value) + "\t0.65\tstring-scanner\n";
	}

	std::string xrefs = "id\tfrom\tto\tkind\tsource_node\ttarget_node\tline\tconfidence\tsource\n";
	std::string traps = "id\trom_id\taddress\ttrap_number\ttrap_name\tspace\tconfidence\tsource\n";
	std::string sourceOverlays = "id\trom_id\taddress\tsource_path\tsymbol\tconfidence\tevidence\n";
	std::string sourceGaps = "id\trom_id\taddress\tkind\tpriority\tconfidence\tevidence\n";
	for(const auto& fn : analysis.function_candidates)
	{
		if(fn.label.empty() && fn.confidence >= 0.50)
		{
			const std::string address = RomDasm::format_address(fn.address);
			sourceGaps += "gap-" + address + "\t" + romId + "\t" + address + "\tfunction_candidate\tmedium\t";
			sourceGaps += std::to_string(fn.confidence) + "\tno source overlay supplied\n";
		}
	}

	return write_text_file(path_join(atlasDir, "roms.tsv"), roms) &&
		write_text_file(path_join(atlasDir, "inventory.tsv"), inventory) &&
		write_text_file(path_join(atlasDir, "regions.tsv"), regions) &&
		write_text_file(path_join(atlasDir, "functions.tsv"), functions) &&
		write_text_file(path_join(atlasDir, "xrefs.tsv"), xrefs) &&
		write_text_file(path_join(atlasDir, "pointer-tables.tsv"), pointerTables) &&
		write_text_file(path_join(atlasDir, "data-regions.tsv"), dataRegions) &&
		write_text_file(path_join(atlasDir, "resources.tsv"), resources) &&
		write_text_file(path_join(atlasDir, "labels.tsv"), labels) &&
		write_text_file(path_join(atlasDir, "strings.tsv"), strings) &&
		write_text_file(path_join(atlasDir, "traps.tsv"), traps) &&
		write_text_file(path_join(atlasDir, "source-overlays.tsv"), sourceOverlays) &&
		write_text_file(path_join(atlasDir, "source-gaps.tsv"), sourceGaps);
}

bool write_analysis_json(
	const std::string& jsonPath,
	const std::string& filename,
	const std::string& inputPath,
	const std::string& outputDir,
	const std::vector<uint8_t>& buf,
	const RomDasm::RomAnalysis& analysis)
{
	FILE* jsonFile = fopen(jsonPath.c_str(), "w");
	if(!jsonFile)
		return false;

	const std::string generatedAt = current_utc_timestamp();
	double firstBytesConfidence = 0.0;
	const char* firstBytesKind = initial_bytes_hypothesis(buf, firstBytesConfidence);
	const std::string inputRel = relative_to_cwd(inputPath);
	const std::string outputRel = relative_to_cwd(outputDir);

	fprintf(jsonFile, "{\n");
	fprintf(jsonFile, "  \"tool\": {\"name\": \"stackimport\", \"version\": \"%s\", \"commit\": \"unknown\"},\n", STACKIMPORT_VERSION_STRING);
	fprintf(jsonFile, "  \"input\": {\"path\": \"%s\", \"size\": %u, \"crc32\": \"%08X\", \"sha256\": \"%s\"},\n",
		json_escape(inputRel).c_str(),
		static_cast<unsigned>(analysis.info.size),
		static_cast<unsigned>(analysis.info.crc32),
		json_escape(analysis.info.sha256).c_str());
	fprintf(jsonFile, "  \"generated_at\": \"%s\",\n", generatedAt.c_str());
	fprintf(jsonFile, "  \"output_path\": \"%s\",\n", json_escape(outputRel).c_str());
	fprintf(jsonFile, "  \"filename\": \"%s\",\n", json_escape(filename).c_str());
	fprintf(jsonFile, "  \"size\": %u,\n", static_cast<unsigned>(analysis.info.size));
	fprintf(jsonFile, "  \"crc32\": \"%08X\",\n", static_cast<unsigned>(analysis.info.crc32));
	fprintf(jsonFile, "  \"sha256\": \"%s\",\n", json_escape(analysis.info.sha256).c_str());
	fprintf(jsonFile, "  \"base_address\": \"%08X\",\n", static_cast<unsigned>(analysis.info.base_address));
	fprintf(jsonFile, "  \"identity\": {\"path\": \"%s\", \"size\": %u, \"crc32\": \"%08X\", \"sha256\": \"%s\", \"base_address\": \"%08X\", \"machine_family\": \"%s\", \"first_bytes\": {\"kind\": \"%s\", \"confidence\": %.2f}},\n",
		json_escape(inputRel).c_str(),
		static_cast<unsigned>(analysis.info.size),
		static_cast<unsigned>(analysis.info.crc32),
		json_escape(analysis.info.sha256).c_str(),
		static_cast<unsigned>(analysis.info.base_address),
		json_escape(analysis.info.machine_family).c_str(),
		firstBytesKind,
		firstBytesConfidence);
	fprintf(jsonFile, "  \"disassembly\": {\"mode\": \"%s\", \"linear_warning\": true, \"instruction_count\": %zu},\n",
		rom_disassembly_mode(),
		analysis.total_instructions);
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
	for(size_t i = 0; i < analysis.function_candidates.size() && i < 200; i++)
	{
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
	for(size_t i = 0; i < analysis.pointer_table_regions.size() && i < 120; i++)
	{
		const auto& table = analysis.pointer_table_regions[i];
		fprintf(jsonFile,
			"    {\"address\":\"%08X\",\"entry_count\":%zu,\"targets\":[",
			static_cast<unsigned>(table.address),
			table.entry_count);
		for(size_t j = 0; j < table.targets.size() && j < 16; j++)
			fprintf(jsonFile, "%s\"%08X\"", j ? "," : "", static_cast<unsigned>(table.targets[j]));
		fprintf(jsonFile, "]}%s\n", (i + 1 < analysis.pointer_table_regions.size() && i + 1 < 120) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"data_regions_sample\": [\n");
	for(size_t i = 0; i < analysis.data_regions.size() && i < 120; i++)
	{
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
	for(size_t i = 0; i < analysis.resource_markers.size() && i < 120; i++)
	{
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
	return fclose(jsonFile) == 0;
}

}

int run_rom_mode(const Options& options)
{
	std::vector<uint8_t> buf;
	if(!read_entire_file(options.input_path, buf))
	{
		stackimport_quill_diagnosticf("Error: Failed to read '%s'.\n", options.input_path.c_str());
		return 5;
	}

	const auto filename = options.input_path.substr(options.input_path.find_last_of("/\\") + 1);
	RomDasm::RomInfo romInfo = RomDasm::analyze_rom_header(
		std::span<const uint8_t>(buf.data(), buf.size()), options.rom_base_address);
	romInfo.filename = filename;

	RomDasm::ScanOptions scanOptions = {};
	scanOptions.start_address = options.rom_base_address;
	scanOptions.disassemble_code = true;
	scanOptions.detect_pointer_tables = true;
	scanOptions.min_string_length = 4;

	stackimport_quill_diagnosticf("Analyzing ROM: %s (%u bytes, base $%08X)\n",
		filename.c_str(), static_cast<unsigned>(buf.size()), static_cast<unsigned>(options.rom_base_address));

	auto analysis = RomDasm::scan_rom(
		std::span<const uint8_t>(buf.data(), buf.size()), romInfo, scanOptions);

	stackimport_quill_diagnosticf("Found %zu code regions, %zu strings, %zu pointer tables\n",
		analysis.code_regions.size(), analysis.strings.size(), analysis.pointer_tables.size());
	stackimport_quill_diagnosticf("Total instructions: %zu\n", analysis.total_instructions);

	const std::string outputDir = !options.output_path.empty() ? options.output_path : options.input_path + ".rom";
	if(!make_directories_recursive(outputDir))
	{
		stackimport_quill_diagnosticf("Error: Failed to create output directory '%s'.\n", outputDir.c_str());
		return 5;
	}

	const std::string dasmPath = path_join(outputDir, "disassembly.s");
	const std::string disasm = RomDasm::export_disassembly(analysis, true);
	if(!write_text_file(dasmPath, disasm))
	{
		stackimport_quill_diagnosticf("Error: Failed to write '%s'.\n", dasmPath.c_str());
		return 5;
	}
	stackimport_quill_diagnosticf("Wrote: %s\n", dasmPath.c_str());

	const std::string jsonPath = path_join(outputDir, "analysis.json");
	if(!write_analysis_json(jsonPath, filename, options.input_path, outputDir, buf, analysis))
	{
		stackimport_quill_diagnosticf("Error: Failed to write '%s'.\n", jsonPath.c_str());
		return 5;
	}
	stackimport_quill_diagnosticf("Wrote: %s\n", jsonPath.c_str());

	if(options.emit_atlas || !options.atlas_output_path.empty())
	{
		const std::string atlasDir = !options.atlas_output_path.empty() ? options.atlas_output_path : path_join(outputDir, "atlas");
		if(!write_atlas_outputs(atlasDir, options.input_path, outputDir, buf, analysis))
		{
			stackimport_quill_diagnosticf("Error: Failed to write atlas outputs under '%s'.\n", atlasDir.c_str());
			return 5;
		}
		stackimport_quill_diagnosticf("Wrote atlas outputs: %s\n", atlasDir.c_str());
	}

	(void)options.source_root_path;
	(void)options.emit_json;
	(void)options.emit_assets;
	stackimport_quill_diagnosticf("ROM analysis complete. Output: %s\n", outputDir.c_str());
	return 0;
}

} // namespace stackimport::cli
