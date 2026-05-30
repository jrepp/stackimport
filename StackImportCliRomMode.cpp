#include "StackImportCli.h"

#include "StackImportPngWriter.h"
#include "StackImportResourceTransforms.h"
#include "stackimport_logging.h"
#include "stackimport_version.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>

namespace stackimport::cli {

namespace {

struct RomResourceAsset {
	std::string relative_path;
	std::string media_type;
	std::string decode_status;
	std::string converter;
	ResourcePayloadFormat format = ResourcePayloadFormat::Native;
	uint32_t variant_index = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t row_bytes = 0;
	std::vector<uint8_t> data;
};

struct SourceOverlay {
	std::string id;
	uint32_t address = 0;
	std::string source_path;
	size_t source_line = 0;
	std::string symbol;
	double confidence = 0.0;
	std::string evidence;
};

class RomResourceTransformOutput final : public IResourceOutput {
public:
	auto wants_resource_payload(const ResourcePayload& payload) -> bool override
	{
		return payload.format != ResourcePayloadFormat::Native;
	}

	auto on_resource_payload(const ResourcePayload& payload) -> bool override
	{
		RomResourceAsset asset;
		asset.media_type = payload.media_type != nullptr ? payload.media_type : "application/octet-stream";
		asset.decode_status = "converted";
		asset.converter = "builtin-resource-transform";
		asset.format = payload.format;
		asset.variant_index = payload.variant_index;
		asset.width = payload.width;
		asset.height = payload.height;
		asset.row_bytes = payload.row_bytes;
		if(payload.format == ResourcePayloadFormat::Rgba32 &&
			payload.data.data != nullptr &&
			payload.width != 0 &&
			payload.height != 0 &&
			payload.row_bytes != 0 &&
			payload.width <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
			payload.height <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
			payload.row_bytes <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
			WritePngToMemory(
				asset.data,
				static_cast<int>(payload.width),
				static_cast<int>(payload.height),
				4,
				payload.data.data,
				static_cast<int>(payload.row_bytes)))
		{
			asset.media_type = "image/png";
			asset.format = ResourcePayloadFormat::Binary;
		}
		else if(payload.data.data != nullptr && payload.data.size != 0)
			asset.data.assign(payload.data.data, payload.data.data + payload.data.size);
		assets.push_back(std::move(asset));
		return true;
	}

	auto on_resource_error(const ResourceRef&, const char*) -> bool override
	{
		had_error = true;
		return true;
	}

	std::vector<RomResourceAsset> assets;
	bool had_error = false;
};

std::string resource_asset_relative_path(const RomDasm::ResourceRecord& resource)
{
	std::string type = resource.type;
	for(char& ch : type)
	{
		const unsigned char byte = static_cast<unsigned char>(ch);
		if(!std::isalnum(byte))
			ch = '_';
	}
	return path_join("assets/resources", type + "_" + std::to_string(resource.id) + "_" + RomDasm::format_address(resource.address) + ".bin");
}

std::string path_basename(const std::string& path)
{
	const size_t pos = path.find_last_of("/\\");
	return pos == std::string::npos ? path : path.substr(pos + 1u);
}

std::string resource_index_asset_relative_path(const std::string& assetPath)
{
	return path_join("resources", path_basename(assetPath));
}

std::string resource_converted_asset_relative_path(
	const RomDasm::ResourceRecord& resource,
	const RomResourceAsset& asset,
	size_t variant)
{
	std::string type = resource.type;
	for(char& ch : type)
	{
		const unsigned char byte = static_cast<unsigned char>(ch);
		if(!std::isalnum(byte))
			ch = '_';
	}
	const char* extension = ".bin";
	if(asset.media_type == "image/png")
		extension = ".png";
	else if(asset.media_type == "audio/wav")
		extension = ".wav";
	else if(asset.media_type == "text/x-asm; charset=utf-8")
		extension = ".s";
	else if(asset.format == ResourcePayloadFormat::TextUtf8)
		extension = ".txt";
	else if(asset.format == ResourcePayloadFormat::JsonUtf8)
		extension = ".json";
	else if(asset.format == ResourcePayloadFormat::Rgba32)
		extension = ".rgba";
	std::string suffix;
	if(variant != 0)
		suffix = "_" + std::to_string(variant);
	return path_join("assets/resources", type + "_" + std::to_string(resource.id) + "_" + RomDasm::format_address(resource.address) + suffix + extension);
}

std::vector<RomResourceAsset> converted_resource_assets(
	const RomDasm::ResourceRecord& resource,
	const std::vector<uint8_t>& buf,
	const RomDasm::RomAnalysis& analysis)
{
	if(resource.source.find(":compressed") != std::string::npos)
		return {};
	if(resource.address < analysis.info.base_address)
		return {};
	const size_t offset = static_cast<size_t>(resource.address - analysis.info.base_address);
	if(offset > buf.size() || resource.length > buf.size() - offset)
		return {};

	rsrcd::ResRef res{};
	res.type = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(resource.type.data()), resource.type.size()};
	res.id = resource.id;
	res.data = rsrcd::Bytes{buf.data() + offset, resource.length};
	res.flags = resource.flags;
	res.order = static_cast<uint32_t>(resource.order);
	res.name = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(resource.name.data()), resource.name.size()};

	ResourceRef ref = resource_ref_from_resref(res);
	RomResourceTransformOutput output;
	if(!emit_builtin_resource_transforms(res, ref, output))
		return {};
	for(size_t i = 0; i < output.assets.size(); i++)
		output.assets[i].relative_path = resource_converted_asset_relative_path(resource, output.assets[i], i);
	return output.assets;
}

RomResourceAsset primary_resource_asset(
	const RomDasm::ResourceRecord& resource,
	const std::vector<uint8_t>& buf,
	const RomDasm::RomAnalysis& analysis,
	bool emitAssets)
{
	RomResourceAsset asset;
	if(!emitAssets)
	{
		asset.media_type = "application/octet-stream";
		asset.decode_status = "metadata_only";
		return asset;
	}

	std::vector<RomResourceAsset> converted = converted_resource_assets(resource, buf, analysis);
	if(!converted.empty())
		return converted.front();

	asset.relative_path = resource_asset_relative_path(resource);
	asset.media_type = "application/octet-stream";
	asset.decode_status = "preserved";
	return asset;
}

void write_resource_asset_json(FILE* jsonFile, const RomResourceAsset& asset)
{
	fprintf(jsonFile,
		"{\"path\":\"%s\",\"media_type\":\"%s\",\"decode_status\":\"%s\",\"converter\":\"%s\",\"variant_index\":%u,\"width\":%u,\"height\":%u,\"row_bytes\":%u}",
		json_escape(asset.relative_path).c_str(),
		json_escape(asset.media_type).c_str(),
		json_escape(asset.decode_status).c_str(),
		json_escape(asset.converter).c_str(),
		static_cast<unsigned>(asset.variant_index),
		static_cast<unsigned>(asset.width),
		static_cast<unsigned>(asset.height),
		static_cast<unsigned>(asset.row_bytes));
}

bool write_resource_assets(
	const std::string& outputDir,
	const std::vector<uint8_t>& buf,
	const RomDasm::RomAnalysis& analysis)
{
	if(analysis.resources.empty())
		return true;
	const std::string assetDir = path_join(outputDir, "assets/resources");
	if(!make_directories_recursive(assetDir))
		return false;
	for(const auto& resource : analysis.resources)
	{
		if(resource.address < analysis.info.base_address)
			return false;
		const size_t offset = static_cast<size_t>(resource.address - analysis.info.base_address);
		if(offset > buf.size() || resource.length > buf.size() - offset)
			return false;
		const std::string relPath = resource_asset_relative_path(resource);
		const std::string path = path_join(outputDir, relPath);
		const std::string bytes(reinterpret_cast<const char*>(buf.data() + offset), resource.length);
		if(!write_text_file(path, bytes))
			return false;
		for(const RomResourceAsset& asset : converted_resource_assets(resource, buf, analysis))
		{
			const std::string convertedPath = path_join(outputDir, asset.relative_path);
			const std::string convertedBytes(
				asset.data.empty() ? "" : reinterpret_cast<const char*>(asset.data.data()),
				asset.data.size());
			if(!write_text_file(convertedPath, convertedBytes))
				return false;
		}
	}
	return true;
}

bool write_resource_index(
	const std::string& outputDir,
	const std::vector<uint8_t>& buf,
	const RomDasm::RomAnalysis& analysis)
{
	const std::string indexDir = path_join(outputDir, "resource-index");
	if(!make_directories_recursive(indexDir))
		return false;
	const std::string indexResourceDir = path_join(indexDir, "resources");
	if(!make_directories_recursive(indexResourceDir))
		return false;
	const std::string indexPath = path_join(indexDir, "index.json");
	FILE* jsonFile = fopen(indexPath.c_str(), "w");
	if(!jsonFile)
		return false;

	const std::string romId = rom_id_from_info(analysis);
	fprintf(jsonFile, "{\n");
	fprintf(jsonFile, "  \"schema_version\": 1,\n");
	fprintf(jsonFile, "  \"tool\": {\"name\": \"stackimport\", \"version\": \"%s\"},\n", STACKIMPORT_VERSION_STRING);
	fprintf(jsonFile,
		"  \"rom\": {\"id\": \"%s\", \"filename\": \"%s\", \"size\": %u, \"crc32\": \"%08X\", \"sha256\": \"%s\", \"base_address\": \"%08X\", \"machine_family\": \"%s\"},\n",
		json_escape(romId).c_str(),
		json_escape(analysis.info.filename).c_str(),
		static_cast<unsigned>(analysis.info.size),
		static_cast<unsigned>(analysis.info.crc32),
		json_escape(analysis.info.sha256).c_str(),
		static_cast<unsigned>(analysis.info.base_address),
		json_escape(analysis.info.machine_family).c_str());
	fprintf(jsonFile, "  \"resources\": [\n");
	for(size_t i = 0; i < analysis.resources.size(); i++)
	{
		const auto& resource = analysis.resources[i];
		if(resource.address < analysis.info.base_address)
		{
			fclose(jsonFile);
			return false;
		}
		const size_t offset = static_cast<size_t>(resource.address - analysis.info.base_address);
		if(offset > buf.size() || resource.length > buf.size() - offset)
		{
			fclose(jsonFile);
			return false;
		}
		const std::string rawPath = resource_index_asset_relative_path(resource_asset_relative_path(resource));
		const std::string rawBytes(reinterpret_cast<const char*>(buf.data() + offset), resource.length);
		if(!write_text_file(path_join(indexDir, rawPath), rawBytes))
		{
			fclose(jsonFile);
			return false;
		}
		fprintf(jsonFile,
			"    {\"key\":\"%s:%s:%d:%08X\",\"type\":\"%s\",\"id\":%d,\"name\":\"%s\",\"address\":\"%08X\",\"map_address\":\"%08X\",\"data_address\":\"%08X\",\"flags\":%u,\"length\":%zu,\"stored_length\":%zu,\"expected_length\":%zu,\"wrapper_format\":\"%s\",\"source\":\"%s\",\"confidence\":%.2f,\"raw_path\":\"%s\",\"artifacts\":[",
			json_escape(romId).c_str(),
			json_escape(resource.type).c_str(),
			resource.id,
			static_cast<unsigned>(resource.address),
			json_escape(resource.type).c_str(),
			resource.id,
			json_escape(resource.name).c_str(),
			static_cast<unsigned>(resource.address),
			static_cast<unsigned>(resource.map_address),
			static_cast<unsigned>(resource.data_address),
			static_cast<unsigned>(resource.flags),
			resource.length,
			resource.stored_length,
			resource.expected_length,
			json_escape(resource.wrapper_format).c_str(),
			json_escape(resource.source).c_str(),
			resource.confidence,
			json_escape(rawPath).c_str());

		RomResourceAsset rawAsset;
		rawAsset.relative_path = rawPath;
		rawAsset.media_type = "application/octet-stream";
		rawAsset.decode_status = "preserved";
		write_resource_asset_json(jsonFile, rawAsset);
		for(RomResourceAsset converted : converted_resource_assets(resource, buf, analysis))
		{
			converted.relative_path = resource_index_asset_relative_path(converted.relative_path);
			const std::string convertedBytes(
				converted.data.empty() ? "" : reinterpret_cast<const char*>(converted.data.data()),
				converted.data.size());
			if(!write_text_file(path_join(indexDir, converted.relative_path), convertedBytes))
			{
				fclose(jsonFile);
				return false;
			}
			fprintf(jsonFile, ",");
			write_resource_asset_json(jsonFile, converted);
		}
		fprintf(jsonFile, "]}%s\n", (i + 1 < analysis.resources.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ]\n");
	fprintf(jsonFile, "}\n");
	return fclose(jsonFile) == 0;
}

bool source_file_extension_is_relevant(const std::filesystem::path& path)
{
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return extension == ".c" || extension == ".h" || extension == ".p" ||
		extension == ".pas" || extension == ".a" || extension == ".s" ||
		extension == ".asm" || extension == ".r" || extension == ".rez" ||
		extension == ".txt";
}

std::string clipped_symbol_text(const std::string& text)
{
	constexpr size_t maxLength = 48;
	if(text.size() <= maxLength)
		return text;
	return text.substr(0, maxLength);
}

std::vector<SourceOverlay> correlate_source_strings(
	const std::string& sourceRoot,
	const RomDasm::RomAnalysis& analysis)
{
	std::vector<SourceOverlay> overlays;
	if(sourceRoot.empty())
		return overlays;
	std::error_code ec;
	const std::filesystem::path root(sourceRoot);
	if(!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
		return overlays;

	std::vector<const RomDasm::StringRegion*> candidateStrings;
	for(const auto& stringRegion : analysis.strings)
	{
		if(stringRegion.length >= 6 && stringRegion.confidence >= 0.70)
			candidateStrings.push_back(&stringRegion);
	}
	if(candidateStrings.empty())
		return overlays;

	constexpr size_t maxOverlays = 250;
	constexpr uintmax_t maxSourceFileSize = 2u * 1024u * 1024u;
	for(std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
		!ec && it != end && overlays.size() < maxOverlays;
		it.increment(ec))
	{
		if(!it->is_regular_file(ec) || !source_file_extension_is_relevant(it->path()))
			continue;
		const uintmax_t fileSize = it->file_size(ec);
		if(ec || fileSize > maxSourceFileSize)
		{
			ec.clear();
			continue;
		}
		std::ifstream input(it->path());
		if(!input)
			continue;
		std::string relativePath = relative_to_cwd(it->path().string());
		std::string line;
		size_t lineNumber = 0;
		while(std::getline(input, line) && overlays.size() < maxOverlays)
		{
			lineNumber++;
			for(const RomDasm::StringRegion* stringRegion : candidateStrings)
			{
				if(line.find(stringRegion->value) == std::string::npos)
					continue;
				const uint32_t address = analysis.info.base_address + stringRegion->address;
				SourceOverlay overlay;
				overlay.id = "overlay-str-" + RomDasm::format_address(address) + "-" + std::to_string(overlays.size());
				overlay.address = address;
				overlay.source_path = relativePath;
				overlay.source_line = lineNumber;
				overlay.symbol = "string:" + clipped_symbol_text(stringRegion->value);
				overlay.confidence = std::min(0.95, stringRegion->confidence + 0.10);
				overlay.evidence = "ROM string matched source line " + std::to_string(lineNumber);
				overlays.push_back(std::move(overlay));
				if(overlays.size() >= maxOverlays)
					break;
			}
		}
	}
	std::sort(overlays.begin(), overlays.end(),
		[](const SourceOverlay& a, const SourceOverlay& b) {
			if(a.address != b.address)
				return a.address < b.address;
			if(a.source_path != b.source_path)
				return a.source_path < b.source_path;
			return a.source_line < b.source_line;
		});
	for(size_t i = 0; i < overlays.size(); i++)
		overlays[i].id = "overlay-str-" + RomDasm::format_address(overlays[i].address) + "-" + std::to_string(i);
	return overlays;
}

bool write_atlas_outputs(
	const std::string& atlasDir,
	const std::string& inputPath,
	const std::string& outputDir,
	const std::string& sourceRoot,
	const std::vector<uint8_t>& buf,
	const RomDasm::RomAnalysis& analysis,
	bool emitAssets)
{
	if(!make_directories_recursive(atlasDir))
		return false;

	const std::string romId = rom_id_from_info(analysis);
	const std::string inputRel = relative_to_cwd(inputPath);
	const std::string outputRel = relative_to_cwd(outputDir);
	(void)outputRel;
	double firstBytesConfidence = 0.0;
	const char* firstBytesKind = initial_bytes_hypothesis(buf, firstBytesConfidence);
	const bool hasParsedResources = !analysis.resources.empty();
	const bool hasMarkerOnlyResources = !analysis.resource_markers.empty() && analysis.resources.empty();
	const std::vector<SourceOverlay> sourceOverlaysData = correlate_source_strings(sourceRoot, analysis);
	const bool hasSourceRoot = !sourceRoot.empty();
	const bool hasSourceOverlays = !sourceOverlaysData.empty();

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

	std::string inventory = "rom_id\tcode_regions\tstrings\tpointer_entries\tpointer_table_regions\tfunction_candidates\tdata_regions\tresource_markers\tresource_maps\tresource_records\tdisassembly_mode\tsource_status\n";
	inventory += romId + "\t" + std::to_string(analysis.code_regions.size()) + "\t";
	inventory += std::to_string(analysis.strings.size()) + "\t" + std::to_string(analysis.pointer_tables.size()) + "\t";
	inventory += std::to_string(analysis.pointer_table_regions.size()) + "\t" + std::to_string(analysis.function_candidates.size()) + "\t";
	inventory += std::to_string(analysis.data_regions.size()) + "\t" + std::to_string(analysis.resource_markers.size()) + "\t";
	inventory += std::to_string(analysis.resource_maps.size()) + "\t" + std::to_string(analysis.resources.size()) + "\t";
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

	std::string functions = "id\trom_id\tstart\tend\tboundary_status\tkind\tlabel\tinstruction_count\tinbound_calls\toutbound_calls\treferences\tconfidence\tevidence\n";
	for(const auto& fn : analysis.function_candidates)
	{
		const std::string id = "fn-" + RomDasm::format_address(fn.address);
		const std::string label = fn.label.empty() ? ("sub_" + RomDasm::format_address(fn.address)) : fn.label;
		functions += id + "\t" + romId + "\t" + RomDasm::format_address(fn.address) + "\t";
		functions += (fn.end_address != 0 ? RomDasm::format_address(fn.end_address) : "") + "\t";
		functions += tsv_escape(fn.boundary_status.empty() ? "unknown" : fn.boundary_status) + "\tcandidate\t";
		functions += tsv_escape(label) + "\t";
		functions += (fn.instruction_count != 0 ? std::to_string(fn.instruction_count) : "") + "\t";
		functions += std::to_string(fn.calls) + "\t";
		functions += std::to_string(fn.jumps) + "\t" + std::to_string(fn.references) + "\t";
		functions += std::to_string(fn.confidence) + "\t" + tsv_escape(fn.evidence) + "\n";
	}

	std::string pointerTables = "id\trom_id\taddress\tentry_count\tdecoded_target_count\tconfidence\tsource\n";
	for(const auto& table : analysis.pointer_table_regions)
	{
		pointerTables += "ptrtab-" + RomDasm::format_address(table.address) + "\t" + romId + "\t";
		pointerTables += RomDasm::format_address(table.address) + "\t" + std::to_string(table.entry_count) + "\t";
		pointerTables += std::to_string(table.targets.size()) + "\t" + std::to_string(table.confidence) + "\t";
		pointerTables += tsv_escape(table.evidence.empty() ? "scanner:aligned-absolute-addresses" : table.evidence) + "\n";
	}

	std::string dataRegions = "id\trom_id\tstart\tend\tkind\titem_count\tconfidence\tsource\n";
	for(const auto& region : analysis.data_regions)
	{
		dataRegions += "data-" + RomDasm::format_address(region.start_address) + "\t" + romId + "\t";
		dataRegions += RomDasm::format_address(region.start_address) + "\t" + RomDasm::format_address(region.end_address) + "\t";
		dataRegions += tsv_escape(region.kind) + "\t" + std::to_string(region.item_count) + "\t";
		dataRegions += std::to_string(region.confidence) + "\tscanner\n";
	}

	std::string resources = "id\trom_id\taddress\tkind\tresource_type\tresource_id\tname\tlength\tstored_length\texpected_length\twrapper_format\tmedia_type\toutput_file\tconfidence\tsource\n";
	for(size_t i = 0; i < analysis.resource_markers.size(); i++)
	{
		const auto& marker = analysis.resource_markers[i];
		resources += "resmarker-" + RomDasm::format_address(marker.address) + "-" + std::to_string(i) + "\t";
		resources += romId + "\t" + RomDasm::format_address(marker.address) + "\tmarker\t" + tsv_escape(marker.type);
		resources += "\t\t\t\t\t\t\t\t\t0.40\tmarker-scan:" + tsv_escape(marker.context) + "\n";
	}
	for(size_t i = 0; i < analysis.resources.size(); i++)
	{
		const auto& resource = analysis.resources[i];
		const std::string address = RomDasm::format_address(resource.address);
		const RomResourceAsset asset = primary_resource_asset(resource, buf, analysis, emitAssets);
		resources += "res-" + address + "-" + tsv_escape(resource.type) + "-" + std::to_string(resource.id) + "-" + std::to_string(i) + "\t";
		resources += romId + "\t" + address + "\tparsed\t" + tsv_escape(resource.type) + "\t" + std::to_string(resource.id) + "\t";
		resources += tsv_escape(resource.name) + "\t" + std::to_string(resource.length) + "\t" + std::to_string(resource.stored_length) + "\t";
		resources += std::to_string(resource.expected_length) + "\t" + tsv_escape(resource.wrapper_format) + "\t";
		resources += tsv_escape(asset.media_type) + "\t" + tsv_escape(asset.relative_path) + "\t";
		resources += std::to_string(resource.confidence) + "\t" + tsv_escape(resource.source) + "\n";
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
		strings += "\t" + std::to_string(s.length) + "\t" + tsv_escape(s.value) + "\t";
		strings += std::to_string(s.confidence) + "\tstring-scanner\n";
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
	std::string sourceOverlays = "id\trom_id\taddress\tsource_path\tsource_line\tsymbol\tconfidence\tevidence\n";
	for(const auto& overlay : sourceOverlaysData)
	{
		sourceOverlays += tsv_escape(overlay.id) + "\t" + romId + "\t" + RomDasm::format_address(overlay.address) + "\t";
		sourceOverlays += tsv_escape(overlay.source_path) + "\t" + std::to_string(overlay.source_line) + "\t";
		sourceOverlays += tsv_escape(overlay.symbol) + "\t";
		sourceOverlays += std::to_string(overlay.confidence) + "\t";
		sourceOverlays += tsv_escape(overlay.evidence + "; source_path=" + overlay.source_path) + "\n";
	}
	std::string sourceGaps = "id\trom_id\taddress\tkind\tpriority\tconfidence\tevidence\n";
	size_t emittedFunctionGaps = 0;
	for(const auto& fn : analysis.function_candidates)
	{
		if(fn.confidence >= 0.75 && emittedFunctionGaps < 100)
		{
			const std::string address = RomDasm::format_address(fn.address);
			sourceGaps += "gap-function-" + address + "\t" + romId + "\t" + address + "\tfunction_candidate\thigh\t";
			sourceGaps += std::to_string(fn.confidence) + "\thigh-confidence function candidate has no source overlay; inbound_calls=";
			sourceGaps += std::to_string(fn.calls) + "; references=" + std::to_string(fn.references) + "\n";
			emittedFunctionGaps++;
		}
	}
	size_t emittedResourceGaps = 0;
	for(const auto& marker : analysis.resource_markers)
	{
		if(emittedResourceGaps >= 100)
			break;
		const std::string address = RomDasm::format_address(marker.address);
		sourceGaps += "gap-resource-marker-" + address + "-" + std::to_string(emittedResourceGaps) + "\t" + romId + "\t";
		sourceGaps += address + "\tresource_marker\tmedium\t0.40\tmarker-only resource candidate lacks parsed resource-map record; type=";
		sourceGaps += tsv_escape(marker.type) + "\n";
		emittedResourceGaps++;
	}
	if(!hasSourceOverlays)
		sourceGaps += "gap-source-overlay-missing-" + romId + "\t" + romId + "\t\tanalysis_phase\tmedium\t0.50\t" +
			std::string(hasSourceRoot ? "source root supplied but no source overlays matched for this run" : "no source root overlay supplied or matched for this run") + "\n";

	std::string manifest = "schema_version: 1\n";
	manifest += "tool:\n";
	manifest += "  name: stackimport\n";
	manifest += "  version: " STACKIMPORT_VERSION_STRING "\n";
	manifest += "rom:\n";
	manifest += "  id: " + romId + "\n";
	manifest += "  path: " + inputRel + "\n";
	manifest += "  size: " + std::to_string(analysis.info.size) + "\n";
	manifest += "  crc32: " + RomDasm::format_address(analysis.info.crc32) + "\n";
	manifest += "  sha256: " + analysis.info.sha256 + "\n";
	manifest += "  base_address: " + RomDasm::format_address(analysis.info.base_address) + "\n";
	manifest += "validation_status: partial\n";
	manifest += "phase_status:\n";
	manifest += "  identity: complete\n";
	manifest += "  disassembly: complete\n";
	manifest += "  xrefs: partial_linear\n";
	manifest += "  regions: partial\n";
	manifest += std::string("  resources: ") + (hasParsedResources ? "partial_parsed\n" : "marker_only\n");
	manifest += std::string("  source_overlays: ") + (hasSourceRoot ? (hasSourceOverlays ? "partial_string_matches\n" : "no_matches\n") : "not_run\n");
	manifest += "row_counts:\n";
	manifest += "  functions: " + std::to_string(analysis.function_candidates.size()) + "\n";
	manifest += "  xrefs: " + std::to_string(analysis.xrefs.size()) + "\n";
	manifest += "  traps: " + std::to_string(analysis.traps.size()) + "\n";
	manifest += "  strings: " + std::to_string(analysis.strings.size()) + "\n";
	manifest += "  pointer_table_regions: " + std::to_string(analysis.pointer_table_regions.size()) + "\n";
	manifest += "  data_regions: " + std::to_string(analysis.data_regions.size()) + "\n";
	manifest += "  resource_markers: " + std::to_string(analysis.resource_markers.size()) + "\n";
	manifest += "  resource_maps: " + std::to_string(analysis.resource_maps.size()) + "\n";
	manifest += "  resource_records: " + std::to_string(analysis.resources.size()) + "\n";
	manifest += "  source_overlays: " + std::to_string(sourceOverlaysData.size()) + "\n";
	manifest += "warnings:\n";
	manifest += "  - whole-ROM linear disassembly is region-overlay input, not confirmed code coverage\n";
	if(hasMarkerOnlyResources)
		manifest += "  - resource scan found markers but no parsed resource map records\n";
	if(!hasSourceOverlays)
		manifest += std::string("  - ") + (hasSourceRoot ? "source root supplied but no source overlay matches were found\n" : "source overlay phase did not produce matches\n");

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
		write_text_file(path_join(atlasDir, "source-gaps.tsv"), sourceGaps) &&
		write_text_file(path_join(atlasDir, "manifest.yaml"), manifest);
}

bool write_analysis_json(
	const std::string& jsonPath,
	const std::string& filename,
	const std::string& inputPath,
	const std::string& outputDir,
	const std::string& sourceRoot,
	const std::vector<uint8_t>& buf,
	const RomDasm::RomAnalysis& analysis,
	bool emitAssets)
{
	FILE* jsonFile = fopen(jsonPath.c_str(), "w");
	if(!jsonFile)
		return false;

	const std::string generatedAt = current_utc_timestamp();
	double firstBytesConfidence = 0.0;
	const char* firstBytesKind = initial_bytes_hypothesis(buf, firstBytesConfidence);
	const std::string inputRel = relative_to_cwd(inputPath);
	const std::string outputRel = relative_to_cwd(outputDir);
	const std::string romId = rom_id_from_info(analysis);
	const bool hasParsedResources = !analysis.resources.empty();
	const bool hasMarkerOnlyResources = !analysis.resource_markers.empty() && analysis.resources.empty();
	const std::vector<SourceOverlay> sourceOverlaysData = correlate_source_strings(sourceRoot, analysis);
	const bool hasSourceRoot = !sourceRoot.empty();
	const bool hasSourceOverlays = !sourceOverlaysData.empty();

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
	fprintf(jsonFile, "  \"phase_status\": {\"identity\":\"complete\",\"disassembly\":\"complete\",\"xrefs\":\"partial_linear\",\"regions\":\"partial\",\"resources\":\"%s\",\"source_overlays\":\"%s\"},\n",
		hasParsedResources ? "partial_parsed" : "marker_only",
		hasSourceRoot ? (hasSourceOverlays ? "partial_string_matches" : "no_matches") : "not_run");
	fprintf(jsonFile, "  \"machine_family\": \"%s\",\n", json_escape(analysis.info.machine_family).c_str());
	fprintf(jsonFile, "  \"counts\": {\"code_regions\": %zu, \"strings\": %zu, \"pointer_entries\": %zu, \"pointer_table_regions\": %zu, \"function_candidates\": %zu, \"data_regions\": %zu, \"resource_markers\": %zu, \"resource_maps\": %zu, \"resource_records\": %zu, \"xrefs\": %zu, \"traps\": %zu},\n",
		analysis.code_regions.size(),
		analysis.strings.size(),
		analysis.pointer_tables.size(),
		analysis.pointer_table_regions.size(),
		analysis.function_candidates.size(),
		analysis.data_regions.size(),
		analysis.resource_markers.size(),
		analysis.resource_maps.size(),
		analysis.resources.size(),
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
		const std::string endAddress = fn.end_address != 0 ? ("\"" + RomDasm::format_address(fn.end_address) + "\"") : "null";
		const std::string instructionCount = fn.instruction_count != 0 ? std::to_string(fn.instruction_count) : "null";
		fprintf(jsonFile,
			"    {\"id\":\"fn-%08X\",\"start\":\"%08X\",\"end\":%s,\"boundary_status\":\"%s\",\"kind\":\"candidate\",\"label\":\"%s\",\"instruction_count\":%s,\"inbound_calls\":%zu,\"outbound_calls\":%zu,\"references\":%zu,\"confidence\":%.2f,\"evidence\":\"%s\"}%s\n",
			static_cast<unsigned>(fn.address),
			static_cast<unsigned>(fn.address),
			endAddress.c_str(),
			json_escape(fn.boundary_status.empty() ? "unknown" : fn.boundary_status).c_str(),
			json_escape(fn.label.empty() ? ("sub_" + RomDasm::format_address(fn.address)) : fn.label).c_str(),
			instructionCount.c_str(),
			fn.calls,
			fn.jumps,
			fn.references,
			fn.confidence,
			json_escape(fn.evidence).c_str(),
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
			"    {\"id\":\"ptrtab-%08X\",\"address\":\"%08X\",\"entry_count\":%zu,\"decoded_target_count\":%zu,\"confidence\":%.2f,\"source\":\"%s\",\"targets\":[",
			static_cast<unsigned>(table.address),
			static_cast<unsigned>(table.address),
			table.entry_count,
			table.targets.size(),
			table.confidence,
			json_escape(table.evidence.empty() ? "scanner:aligned-absolute-addresses" : table.evidence).c_str());
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
			"    {\"id\":\"resmarker-%08X-%zu\",\"rom_id\":\"%s\",\"address\":\"%08X\",\"kind\":\"marker\",\"resource_type\":\"%s\",\"resource_id\":null,\"name\":\"\",\"media_type\":\"\",\"output_file\":\"\",\"decode_status\":\"marker_only\",\"confidence\":0.40,\"source\":\"marker-scan\",\"context\":\"%s\"}",
			static_cast<unsigned>(marker.address),
			i,
			json_escape(romId).c_str(),
			static_cast<unsigned>(marker.address),
			json_escape(marker.type).c_str(),
			json_escape(marker.context).c_str());
		if(i + 1 < analysis.resource_markers.size() || !analysis.resources.empty())
			fprintf(jsonFile, ",");
		fprintf(jsonFile, "\n");
	}
	for(size_t i = 0; i < analysis.resources.size(); i++)
	{
		const auto& resource = analysis.resources[i];
		const RomResourceAsset asset = primary_resource_asset(resource, buf, analysis, emitAssets);
		const std::string rawOutputFile = emitAssets ? resource_asset_relative_path(resource) : "";
		fprintf(jsonFile,
			"    {\"id\":\"res-%08X-%s-%d-%zu\",\"rom_id\":\"%s\",\"address\":\"%08X\",\"map_address\":\"%08X\",\"data_address\":\"%08X\",\"kind\":\"parsed\",\"resource_type\":\"%s\",\"resource_id\":%d,\"name\":\"%s\",\"flags\":%u,\"length\":%zu,\"stored_length\":%zu,\"expected_length\":%zu,\"wrapper_format\":\"%s\",\"media_type\":\"%s\",\"output_file\":\"%s\",\"raw_output_file\":\"%s\",\"decode_status\":\"%s\",\"converter\":\"%s\",\"variant_index\":%u,\"width\":%u,\"height\":%u,\"row_bytes\":%u,\"output_artifacts\":[",
			static_cast<unsigned>(resource.address),
			json_escape(resource.type).c_str(),
			resource.id,
			i,
			json_escape(romId).c_str(),
			static_cast<unsigned>(resource.address),
			static_cast<unsigned>(resource.map_address),
			static_cast<unsigned>(resource.data_address),
			json_escape(resource.type).c_str(),
			resource.id,
			json_escape(resource.name).c_str(),
			static_cast<unsigned>(resource.flags),
			resource.length,
			resource.stored_length,
			resource.expected_length,
			json_escape(resource.wrapper_format).c_str(),
			json_escape(asset.media_type).c_str(),
			json_escape(asset.relative_path).c_str(),
			json_escape(rawOutputFile).c_str(),
			json_escape(asset.decode_status).c_str(),
			json_escape(asset.converter).c_str(),
			static_cast<unsigned>(asset.variant_index),
			static_cast<unsigned>(asset.width),
			static_cast<unsigned>(asset.height),
			static_cast<unsigned>(asset.row_bytes));
		if(emitAssets)
		{
			RomResourceAsset rawAsset;
			rawAsset.relative_path = rawOutputFile;
			rawAsset.media_type = "application/octet-stream";
			rawAsset.decode_status = "preserved";
			rawAsset.converter = "";
			write_resource_asset_json(jsonFile, rawAsset);
			for(const RomResourceAsset& converted : converted_resource_assets(resource, buf, analysis))
			{
				fprintf(jsonFile, ",");
				write_resource_asset_json(jsonFile, converted);
			}
		}
		fprintf(jsonFile,
			"],\"confidence\":%.2f,\"source\":\"%s\"}%s\n",
			resource.confidence,
			json_escape(resource.source).c_str(),
			(i + 1 < analysis.resources.size()) ? "," : "");
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
			"    {\"id\":\"str-%08X\",\"address\":\"%08X\",\"kind\":\"%s\",\"length\":%zu,\"text\":\"%s\",\"confidence\":%.2f,\"source\":\"string-scanner\"}%s\n",
			static_cast<unsigned>(address),
			static_cast<unsigned>(address),
			s.is_pascal ? "pascal" : "ascii",
			s.length,
			json_escape(s.value).c_str(),
			s.confidence,
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
	fprintf(jsonFile, "  \"source_overlays\": [\n");
	for(size_t i = 0; i < sourceOverlaysData.size(); i++)
	{
		const auto& overlay = sourceOverlaysData[i];
		fprintf(jsonFile,
			"    {\"id\":\"%s\",\"rom_id\":\"%s\",\"address\":\"%08X\",\"source_path\":\"%s\",\"source_line\":%zu,\"symbol\":\"%s\",\"confidence\":%.2f,\"evidence\":\"%s\"}%s\n",
			json_escape(overlay.id).c_str(),
			json_escape(romId).c_str(),
			static_cast<unsigned>(overlay.address),
			json_escape(overlay.source_path).c_str(),
			overlay.source_line,
			json_escape(overlay.symbol).c_str(),
			overlay.confidence,
			json_escape(overlay.evidence).c_str(),
			(i + 1 < sourceOverlaysData.size()) ? "," : "");
	}
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"source_gaps\": [\n");
	size_t sourceGapIndex = 0;
	for(const auto& fn : analysis.function_candidates)
	{
		if(fn.confidence >= 0.75 && sourceGapIndex < 100)
		{
			if(sourceGapIndex > 0)
				fprintf(jsonFile, ",\n");
			const std::string address = RomDasm::format_address(fn.address);
			fprintf(jsonFile,
				"    {\"id\":\"gap-function-%s\",\"rom_id\":\"%s\",\"address\":\"%s\",\"kind\":\"function_candidate\",\"priority\":\"high\",\"confidence\":%.2f,\"evidence\":\"high-confidence function candidate has no source overlay; inbound_calls=%zu; references=%zu\"}",
				address.c_str(),
				json_escape(romId).c_str(),
				address.c_str(),
				fn.confidence,
				fn.calls,
				fn.references);
			sourceGapIndex++;
		}
	}
	for(size_t i = 0; i < analysis.resource_markers.size() && i < 100; i++)
	{
		if(sourceGapIndex > 0)
			fprintf(jsonFile, ",\n");
		const auto& marker = analysis.resource_markers[i];
		const std::string address = RomDasm::format_address(marker.address);
		fprintf(jsonFile,
			"    {\"id\":\"gap-resource-marker-%s-%zu\",\"rom_id\":\"%s\",\"address\":\"%s\",\"kind\":\"resource_marker\",\"priority\":\"medium\",\"confidence\":0.40,\"evidence\":\"marker-only resource candidate lacks parsed resource-map record; type=%s\"}",
			address.c_str(),
			i,
			json_escape(romId).c_str(),
			address.c_str(),
			json_escape(marker.type).c_str());
		sourceGapIndex++;
	}
	if(!hasSourceOverlays)
	{
		if(sourceGapIndex > 0)
			fprintf(jsonFile, ",\n");
		fprintf(jsonFile,
			"    {\"id\":\"gap-source-overlay-missing-%s\",\"rom_id\":\"%s\",\"address\":null,\"kind\":\"analysis_phase\",\"priority\":\"medium\",\"confidence\":0.50,\"evidence\":\"%s\"}",
			json_escape(romId).c_str(),
			json_escape(romId).c_str(),
			hasSourceRoot ? "source root supplied but no source overlays matched for this run" : "no source root overlay supplied or matched for this run");
		sourceGapIndex++;
	}
	if(sourceGapIndex > 0)
		fprintf(jsonFile, "\n");
	fprintf(jsonFile, "  ],\n");
	fprintf(jsonFile, "  \"warnings\": [\n");
	fprintf(jsonFile, "    \"whole-ROM linear disassembly is region-overlay input, not confirmed code coverage\"");
	if(hasMarkerOnlyResources)
		fprintf(jsonFile, ",\n    \"resource scan found markers but no parsed resource map records\"");
	if(!hasSourceOverlays)
		fprintf(jsonFile, ",\n    \"%s\"", hasSourceRoot ? "source root supplied but no source overlay matches were found" : "source overlay phase did not produce matches");
	fprintf(jsonFile, "\n  ]\n");
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

	const bool emitResourceAssets = options.emit_assets || options.emit_resource_index;
	if(emitResourceAssets && !analysis.resources.empty())
	{
		if(!write_resource_assets(outputDir, buf, analysis))
		{
			stackimport_quill_diagnosticf("Error: Failed to write ROM resource assets under '%s'.\n", outputDir.c_str());
			return 5;
		}
		stackimport_quill_diagnosticf("Wrote ROM resource assets: %s\n", path_join(outputDir, "assets/resources").c_str());
	}
	else if(emitResourceAssets)
	{
		stackimport_quill_diagnosticf("No parsed ROM resource payloads to preserve.\n");
	}

	if(options.emit_resource_index)
	{
		if(!write_resource_index(outputDir, buf, analysis))
		{
			stackimport_quill_diagnosticf("Error: Failed to write ROM resource index under '%s'.\n", outputDir.c_str());
			return 5;
		}
		stackimport_quill_diagnosticf("Wrote ROM resource index: %s\n", path_join(outputDir, "resource-index/index.json").c_str());
	}

	const std::string jsonPath = path_join(outputDir, "analysis.json");
	if(!write_analysis_json(jsonPath, filename, options.input_path, outputDir, options.source_root_path, buf, analysis, emitResourceAssets))
	{
		stackimport_quill_diagnosticf("Error: Failed to write '%s'.\n", jsonPath.c_str());
		return 5;
	}
	stackimport_quill_diagnosticf("Wrote: %s\n", jsonPath.c_str());

	if(options.emit_atlas || !options.atlas_output_path.empty())
	{
		const std::string atlasDir = !options.atlas_output_path.empty() ? options.atlas_output_path : path_join(outputDir, "atlas");
		if(!write_atlas_outputs(atlasDir, options.input_path, outputDir, options.source_root_path, buf, analysis, emitResourceAssets))
		{
			stackimport_quill_diagnosticf("Error: Failed to write atlas outputs under '%s'.\n", atlasDir.c_str());
			return 5;
		}
		stackimport_quill_diagnosticf("Wrote atlas outputs: %s\n", atlasDir.c_str());
	}

	(void)options.emit_json;
	stackimport_quill_diagnosticf("ROM analysis complete. Output: %s\n", outputDir.c_str());
	return 0;
}

} // namespace stackimport::cli
