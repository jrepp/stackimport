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
	for(size_t i = 0; i < analysis.xrefs.size(); i++)
	{
		const auto& xref = analysis.xrefs[i];
		const std::string from = RomDasm::format_address(xref.from);
		const std::string to = RomDasm::format_address(xref.to);
		xrefs += "xref-" + from + "-" + to + "-" + tsv_escape(xref.kind) + "-" + std::to_string(i) + "\t";
		xrefs += from + "\t" + to + "\t" + tsv_escape(xref.kind) + "\t";
		xrefs += "addr-" + from + "\taddr-" + to + "\t" + std::to_string(xref.line) + "\t";
		xrefs += std::to_string(xref.confidence) + "\t" + tsv_escape(xref.source) + "\n";
	}
	std::string traps = "id\trom_id\taddress\ttrap_number\ttrap_name\tspace\tconfidence\tsource\n";
	for(size_t i = 0; i < analysis.traps.size(); i++)
	{
		const auto& trap = analysis.traps[i];
		const std::string address = RomDasm::format_address(trap.address);
		char trapNumber[8] = {};
		snprintf(trapNumber, sizeof(trapNumber), "A%03X", static_cast<unsigned>(trap.trap_number));
		traps += "trap-" + address + "-" + trapNumber + "-" + std::to_string(i) + "\t" + romId + "\t";
		traps += address + "\t" + trapNumber + "\t" + tsv_escape(trap.trap_name) + "\t";
		traps += tsv_escape(trap.space) + "\t" + std::to_string(trap.confidence) + "\t" + tsv_escape(trap.source) + "\n";
	}
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
	fprintf(jsonFile, "  \"schema_version\": 1,\n");
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
	fprintf(jsonFile, "  \"counts\": {\"code_regions\": %zu, \"strings\": %zu, \"pointer_entries\": %zu, \"pointer_table_regions\": %zu, \"function_candidates\": %zu, \"data_regions\": %zu, \"resource_markers\": %zu, \"xrefs\": %zu, \"traps\": %zu},\n",
		analysis.code_regions.size(),
		analysis.strings.size(),
		analysis.pointer_tables.size(),
		analysis.pointer_table_regions.size(),
		analysis.function_candidates.size(),
		analysis.data_regions.size(),
		analysis.resource_markers.size(),
		analysis.xrefs.size(),
		analysis.traps.size());
	fprintf(jsonFile, "  \"total_instructions\": %zu,\n", analysis.total_instructions);
	fprintf(jsonFile, "  \"regions\": [\n");
	fprintf(jsonFile,
		"    {\"id\":\"region-%s-first-bytes\",\"start\":\"%08X\",\"end\":\"%08X\",\"kind\":\"%s\",\"item_count\":1,\"confidence\":%.2f,\"source\":\"identity:first-bytes\"}%s\n",
		RomDasm::format_address(analysis.info.crc32).c_str(),
		static_cast<unsigned>(analysis.info.base_address),
		static_cast<unsigned>(analysis.info.base_address + static_cast<uint32_t>(std::min<size_t>(buf.size(), 32))),
		firstBytesKind,
		firstBytesConfidence,
		(!analysis.code_regions.empty() || !analysis.data_regions.empty()) ? "," : "");
	for(size_t i = 0; i < analysis.code_regions.size(); i++)
	{
		const auto& region = analysis.code_regions[i];
		const bool hasMore = (i + 1 < analysis.code_regions.size()) || !analysis.data_regions.empty();
		fprintf(jsonFile,
			"    {\"id\":\"region-%08X\",\"start\":\"%08X\",\"end\":\"%08X\",\"kind\":\"%s\",\"item_count\":%zu,\"confidence\":%.2f,\"source\":\"disassembly\"}%s\n",
			static_cast<unsigned>(region.start_address),
			static_cast<unsigned>(region.start_address),
			static_cast<unsigned>(region.end_address),
			region.is_code ? "code" : "mixed",
			analysis.total_instructions,
			region.confidence,
			hasMore ? "," : "");
	}
	for(size_t i = 0; i < analysis.data_regions.size(); i++)
	{
		const auto& region = analysis.data_regions[i];
		fprintf(jsonFile,
			"    {\"id\":\"data-%08X\",\"start\":\"%08X\",\"end\":\"%08X\",\"kind\":\"%s\",\"item_count\":%zu,\"confidence\":%.2f,\"source\":\"scanner\"}%s\n",
			static_cast<unsigned>(region.start_address),
			static_cast<unsigned>(region.start_address),
			static_cast<unsigned>(region.end_address),
			json_escape(region.kind).c_str(),
			region.item_count,
			region.confidence,
			(i + 1 < analysis.data_regions.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"functions\": [\n");
	for(size_t i = 0; i < analysis.function_candidates.size(); i++)
	{
		const auto& fn = analysis.function_candidates[i];
		fprintf(jsonFile,
			"    {\"id\":\"fn-%08X\",\"start\":\"%08X\",\"end\":null,\"kind\":\"candidate\",\"label\":\"%s\",\"instruction_count\":0,\"inbound_calls\":%zu,\"outbound_calls\":%zu,\"references\":%zu,\"confidence\":%.2f,\"evidence\":\"inbound references from linear disassembly\"}%s\n",
			static_cast<unsigned>(fn.address),
			static_cast<unsigned>(fn.address),
			json_escape(fn.label).c_str(),
			fn.calls,
			fn.jumps,
			fn.references,
			fn.confidence,
			(i + 1 < analysis.function_candidates.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"xrefs\": [\n");
	for(size_t i = 0; i < analysis.xrefs.size(); i++)
	{
		const auto& xref = analysis.xrefs[i];
		const std::string from = RomDasm::format_address(xref.from);
		const std::string to = RomDasm::format_address(xref.to);
		fprintf(jsonFile,
			"    {\"id\":\"xref-%s-%s-%s-%zu\",\"from\":\"%s\",\"to\":\"%s\",\"kind\":\"%s\",\"source_node\":\"addr-%s\",\"target_node\":\"addr-%s\",\"mnemonic\":\"%s\",\"line\":%zu,\"confidence\":%.2f,\"source\":\"%s\"}%s\n",
			from.c_str(),
			to.c_str(),
			json_escape(xref.kind).c_str(),
			i,
			from.c_str(),
			to.c_str(),
			json_escape(xref.kind).c_str(),
			from.c_str(),
			to.c_str(),
			json_escape(xref.mnemonic).c_str(),
			xref.line,
			xref.confidence,
			json_escape(xref.source).c_str(),
			(i + 1 < analysis.xrefs.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"pointer_tables\": [\n");
	for(size_t i = 0; i < analysis.pointer_table_regions.size(); i++)
	{
		const auto& table = analysis.pointer_table_regions[i];
		fprintf(jsonFile,
			"    {\"id\":\"ptrtab-%08X\",\"address\":\"%08X\",\"entry_count\":%zu,\"decoded_target_count\":%zu,\"confidence\":0.75,\"source\":\"scanner:aligned-absolute-addresses\",\"targets\":[",
			static_cast<unsigned>(table.address),
			static_cast<unsigned>(table.address),
			table.entry_count,
			table.targets.size());
		for(size_t j = 0; j < table.targets.size(); j++)
			fprintf(jsonFile, "%s\"%08X\"", j ? "," : "", static_cast<unsigned>(table.targets[j]));
		fprintf(jsonFile, "]}%s\n", (i + 1 < analysis.pointer_table_regions.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"data_regions\": [\n");
	for(size_t i = 0; i < analysis.data_regions.size(); i++)
	{
		const auto& region = analysis.data_regions[i];
		fprintf(jsonFile,
			"    {\"id\":\"data-%08X\",\"start\":\"%08X\",\"end\":\"%08X\",\"kind\":\"%s\",\"item_count\":%zu,\"confidence\":%.2f,\"source\":\"scanner\"}%s\n",
			static_cast<unsigned>(region.start_address),
			static_cast<unsigned>(region.start_address),
			static_cast<unsigned>(region.end_address),
			json_escape(region.kind).c_str(),
			region.item_count,
			region.confidence,
			(i + 1 < analysis.data_regions.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"resources\": [\n");
	for(size_t i = 0; i < analysis.resource_markers.size(); i++)
	{
		const auto& marker = analysis.resource_markers[i];
		fprintf(jsonFile,
			"    {\"id\":\"resmarker-%08X-%zu\",\"address\":\"%08X\",\"kind\":\"marker\",\"resource_type\":\"%s\",\"resource_id\":null,\"name\":\"\",\"media_type\":\"\",\"output_file\":\"\",\"confidence\":0.40,\"source\":\"marker-scan\",\"context\":\"%s\"}%s\n",
			static_cast<unsigned>(marker.address),
			i,
			static_cast<unsigned>(marker.address),
			json_escape(marker.type).c_str(),
			json_escape(marker.context).c_str(),
			(i + 1 < analysis.resource_markers.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"labels\": [\n");
	for(size_t i = 0; i < analysis.function_candidates.size(); i++)
	{
		const auto& fn = analysis.function_candidates[i];
		const std::string address = RomDasm::format_address(fn.address);
		const std::string label = fn.label.empty() ? ("sub_" + address) : fn.label;
		fprintf(jsonFile,
			"    {\"id\":\"label-%s\",\"address\":\"%s\",\"name\":\"%s\",\"kind\":\"function_candidate\",\"confidence\":%.2f,\"source\":\"function-scanner\"}%s\n",
			address.c_str(),
			address.c_str(),
			json_escape(label).c_str(),
			fn.confidence,
			(i + 1 < analysis.function_candidates.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"strings\": [\n");
	for(size_t i = 0; i < analysis.strings.size(); i++)
	{
		const auto& s = analysis.strings[i];
		const uint32_t address = analysis.info.base_address + s.address;
		fprintf(jsonFile,
			"    {\"id\":\"str-%08X\",\"address\":\"%08X\",\"kind\":\"%s\",\"length\":%zu,\"text\":\"%s\",\"confidence\":0.65,\"source\":\"string-scanner\"}%s\n",
			static_cast<unsigned>(address),
			static_cast<unsigned>(address),
			s.is_pascal ? "pascal" : "ascii",
			s.length,
			json_escape(s.value).c_str(),
			(i + 1 < analysis.strings.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"traps\": [\n");
	for(size_t i = 0; i < analysis.traps.size(); i++)
	{
		const auto& trap = analysis.traps[i];
		char trapNumber[8] = {};
		snprintf(trapNumber, sizeof(trapNumber), "A%03X", static_cast<unsigned>(trap.trap_number));
		fprintf(jsonFile,
			"    {\"id\":\"trap-%08X-%s-%zu\",\"address\":\"%08X\",\"trap_number\":\"%s\",\"trap_name\":\"%s\",\"space\":\"%s\",\"confidence\":%.2f,\"source\":\"%s\"}%s\n",
			static_cast<unsigned>(trap.address),
			trapNumber,
			i,
			static_cast<unsigned>(trap.address),
			trapNumber,
			json_escape(trap.trap_name).c_str(),
			json_escape(trap.space).c_str(),
			trap.confidence,
			json_escape(trap.source).c_str(),
			(i + 1 < analysis.traps.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"source_overlays\": [],\n");
	fprintf(jsonFile, "  \"source_gaps\": [\n");
	size_t sourceGapIndex = 0;
	for(const auto& fn : analysis.function_candidates)
	{
		if(fn.label.empty() && fn.confidence >= 0.50)
		{
			if(sourceGapIndex > 0)
				fprintf(jsonFile, ",\n");
			const std::string address = RomDasm::format_address(fn.address);
			fprintf(jsonFile,
				"    {\"id\":\"gap-%s\",\"address\":\"%s\",\"kind\":\"function_candidate\",\"priority\":\"medium\",\"confidence\":%.2f,\"evidence\":\"no source overlay supplied\"}",
				address.c_str(),
				address.c_str(),
				fn.confidence);
			sourceGapIndex++;
		}
	}
	if(sourceGapIndex > 0)
		fprintf(jsonFile, "\n");
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"warnings\": []\n");
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
