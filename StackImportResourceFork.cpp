#include "CStackFile.h"
#include "StackImportPngWriter.h"
#include "StackImportResourceTransforms.h"
#include "StackImportResourceTypes.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <utility>

#include "stackimport_logging.h"
#include "stackimport_platform_internal.h"
#include "rsrcd.hpp"

namespace {

std::string output_path(const std::string& basePath, const char* fileName)
{
	std::string result = basePath;
	if(!result.empty() && result[result.size() - 1] != '/')
		result += '/';
	result += fileName;
	return result;
}

std::string sanitized_resource_file_name(const std::string& stackName, const std::string& type, int32_t id, const std::string& name, const char* extension)
{
	std::string result;
	result.reserve((stackName.size() + type.size() + name.size()) * 3 + 32);
	auto append_escaped = [&result](const std::string& text) {
		static const char hex[] = "0123456789ABCDEF";
		size_t emitted = 0;
		for(char rawCh : text)
		{
			const unsigned char ch = static_cast<unsigned char>(rawCh);
			if((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == ':')
			{
				result.push_back(static_cast<char>(ch));
				emitted++;
			}
			else
			{
				result.push_back('%');
				result.push_back(hex[(ch >> 4u) & 0x0Fu]);
				result.push_back(hex[ch & 0x0Fu]);
				emitted += 3;
			}
		}
		if(emitted == 0)
			result += "unnamed";
	};
	append_escaped(stackName);
	result.push_back('_');
	append_escaped(type);
	result.push_back('_');
	result += std::to_string(id);
	if(!name.empty())
	{
		result.push_back('_');
		append_escaped(name);
	}
	result += extension;
	return result;
}

bool write_json_file(const std::string& path, const char* data, size_t size)
{
	stackimport_file_handle file = stackimport_internal_open_file(path.c_str(), "wb");
	if(!file)
		return false;
	const bool ok = stackimport_internal_write_file(file, data, size) == size;
	const int closeStatus = stackimport_internal_close_file(file);
	return ok && closeStatus == 0;
}

bool write_text_file(const std::string& path, const std::string& text)
{
	stackimport_file_handle file = stackimport_internal_open_file(path.c_str(), "wb");
	if(!file)
		return false;
	const bool ok = stackimport_internal_write_file(file, text.data(), text.size()) == text.size();
	const int closeStatus = stackimport_internal_close_file(file);
	return ok && closeStatus == 0;
}

bool write_binary_file(const std::string& path, rsrcd::Bytes data)
{
	stackimport_file_handle file = stackimport_internal_open_file(path.c_str(), "wb");
	if(!file)
		return false;
	const bool ok = stackimport_internal_write_file(file, data.data, data.size) == data.size;
	const int closeStatus = stackimport_internal_close_file(file);
	return ok && closeStatus == 0;
}

bool read_binary_file(const std::string& path, std::vector<uint8_t>& data)
{
	data.clear();
	stackimport_file_handle file = stackimport_internal_open_file(path.c_str(), "rb");
	if(!file)
		return false;

	uint8_t buffer[16384];
	while(true)
	{
		const size_t count = stackimport_internal_read_file(file, buffer, sizeof(buffer));
		if(count == 0)
			break;
		data.insert(data.end(), buffer, buffer + count);
		if(count < sizeof(buffer))
			break;
	}

	const int closeStatus = stackimport_internal_close_file(file);
	return closeStatus == 0;
}

constexpr uint32_t fourcc(const char (&type)[5])
{
	return (static_cast<uint32_t>(static_cast<unsigned char>(type[0])) << 24u) |
		(static_cast<uint32_t>(static_cast<unsigned char>(type[1])) << 16u) |
		(static_cast<uint32_t>(static_cast<unsigned char>(type[2])) << 8u) |
		static_cast<uint32_t>(static_cast<unsigned char>(type[3]));
}

uint32_t resource_type_code(const rsrcd::ResRef& res)
{
	return res.type.size == 4 && res.type.data != nullptr ? rsrcd::read_u32be(res.type.data) : 0;
}

template<size_t N>
bool contains_fourcc(const std::array<uint32_t, N>& values, uint32_t type)
{
	for(uint32_t value : values)
	{
		if(value == type)
			return true;
	}
	return false;
}

constexpr std::array<uint32_t, 6> kIndexedIconTypes = {
	fourcc("icl4"),
	fourcc("icl8"),
	fourcc("icm4"),
	fourcc("icm8"),
	fourcc("ics4"),
	fourcc("ics8"),
};

constexpr std::array<uint32_t, 2> kMonochromeIconListTypes = {
	fourcc("icm#"),
	fourcc("ics#"),
};

constexpr std::array<uint32_t, 10> kDisassemblyTypes = {
	fourcc("XCMD"),
	fourcc("XFCN"),
	fourcc("xcmd"),
	fourcc("xfcn"),
	fourcc("CODE"),
	fourcc("DRVR"),
	fourcc("dcmp"),
	fourcc("PACK"),
	fourcc("boot"),
	fourcc("ptch"),
};

bool resource_type_is_indexed_icon(uint32_t type)
{
	return contains_fourcc(kIndexedIconTypes, type);
}

bool resource_type_is_monochrome_icon_list(uint32_t type)
{
	return contains_fourcc(kMonochromeIconListTypes, type);
}

bool resource_type_is_disassembly(uint32_t type)
{
	return contains_fourcc(kDisassemblyTypes, type);
}

bool resource_type_is_text(uint32_t type)
{
	return type == fourcc("STR ") || type == fourcc("TEXT");
}

enum class ResourceErrorKind : uint8_t {
	Parse,
	ConvertSound,
	ConvertPict,
	Disassemble,
	Other,
};

constexpr std::array<uint32_t, 49> kParseErrorTypes = {
	fourcc("PLTE"),
	fourcc("HCbg"),
	fourcc("HCcd"),
	fourcc("vers"),
	fourcc("cfrg"),
	fourcc("clut"),
	fourcc("CTBL"),
	fourcc("actb"),
	fourcc("cctb"),
	fourcc("dctb"),
	fourcc("fctb"),
	fourcc("wctb"),
	fourcc("pltt"),
	fourcc("ppat"),
	fourcc("ppt#"),
	fourcc("cicn"),
	fourcc("crsr"),
	fourcc("SIZE"),
	fourcc("finf"),
	fourcc("CNTL"),
	fourcc("DLOG"),
	fourcc("WIND"),
	fourcc("MENU"),
	fourcc("DITL"),
	fourcc("MBAR"),
	fourcc("ALRT"),
	fourcc("FREF"),
	fourcc("BNDL"),
	fourcc("ROv#"),
	fourcc("RSSC"),
	fourcc("TxSt"),
	fourcc("styl"),
	fourcc("KCHR"),
	fourcc("RECT"),
	fourcc("TOOL"),
	fourcc("PICK"),
	fourcc("KBDN"),
	fourcc("PAPA"),
	fourcc("LAYO"),
	fourcc("FONT"),
	fourcc("NFNT"),
	fourcc("FOND"),
	fourcc("decl"),
	fourcc("STR "),
	fourcc("STR#"),
	fourcc("TwCS"),
	fourcc("CODE"),
	fourcc("DRVR"),
	fourcc("dcmp"),
};

ResourceErrorKind resource_error_kind(uint32_t type)
{
	if(contains_fourcc(kParseErrorTypes, type) || type == fourcc("TEXT"))
		return ResourceErrorKind::Parse;
	if(type == fourcc("snd "))
		return ResourceErrorKind::ConvertSound;
	if(type == fourcc("PICT"))
		return ResourceErrorKind::ConvertPict;
	if(resource_type_is_disassembly(type))
		return ResourceErrorKind::Disassemble;
	return ResourceErrorKind::Other;
}

struct ResourceJsonName {
	uint32_t type;
	const char* stem;
};

constexpr std::array<ResourceJsonName, 49> kJsonResourceNames = {{
	{fourcc("PLTE"), "PLTE"},
	{fourcc("HCbg"), "HCbg"},
	{fourcc("HCcd"), "HCcd"},
	{fourcc("STR#"), "STR#"},
	{fourcc("TwCS"), "TwCS"},
	{fourcc("CURS"), "CURS"},
	{fourcc("vers"), "vers"},
	{fourcc("clut"), "clut"},
	{fourcc("CTBL"), "CTBL"},
	{fourcc("actb"), "actb"},
	{fourcc("cctb"), "cctb"},
	{fourcc("dctb"), "dctb"},
	{fourcc("fctb"), "fctb"},
	{fourcc("wctb"), "wctb"},
	{fourcc("pltt"), "pltt"},
	{fourcc("ppat"), "ppat"},
	{fourcc("ppt#"), "ppt#"},
	{fourcc("cicn"), "cicn"},
	{fourcc("crsr"), "crsr"},
	{fourcc("SIZE"), "SIZE"},
	{fourcc("finf"), "finf"},
	{fourcc("CNTL"), "CNTL"},
	{fourcc("DLOG"), "DLOG"},
	{fourcc("WIND"), "WIND"},
	{fourcc("MENU"), "MENU"},
	{fourcc("DITL"), "DITL"},
	{fourcc("cfrg"), "cfrg"},
	{fourcc("MBAR"), "MBAR"},
	{fourcc("ALRT"), "ALRT"},
	{fourcc("FREF"), "FREF"},
	{fourcc("BNDL"), "BNDL"},
	{fourcc("ROv#"), "ROv#"},
	{fourcc("RSSC"), "RSSC"},
	{fourcc("TxSt"), "TxSt"},
	{fourcc("styl"), "styl"},
	{fourcc("KCHR"), "KCHR"},
	{fourcc("RECT"), "RECT"},
	{fourcc("TOOL"), "TOOL"},
	{fourcc("PICK"), "PICK"},
	{fourcc("KBDN"), "KBDN"},
	{fourcc("PAPA"), "PAPA"},
	{fourcc("LAYO"), "LAYO"},
	{fourcc("CODE"), "CODE"},
	{fourcc("DRVR"), "DRVR"},
	{fourcc("dcmp"), "dcmp"},
	{fourcc("FONT"), "FONT"},
	{fourcc("NFNT"), "NFNT"},
	{fourcc("FOND"), "FOND"},
	{fourcc("decl"), "decl"},
}};

const char* json_name_stem(uint32_t type)
{
	for(const ResourceJsonName& name : kJsonResourceNames)
	{
		if(name.type == type)
			return name.stem;
	}
	return nullptr;
}

void format_fourcc_file_name(char* dst, size_t dstSize, uint32_t type, int32_t id, const char* suffix)
{
	snprintf(
		dst,
		dstSize,
		"%c%c%c%c_%d%s",
		static_cast<char>((type >> 24u) & 0xFFu),
		static_cast<char>((type >> 16u) & 0xFFu),
		static_cast<char>((type >> 8u) & 0xFFu),
		static_cast<char>(type & 0xFFu),
		id,
		suffix);
}

class PackageBuiltinTransformOutput final : public stackimport::IResourceOutput {
public:
	PackageBuiltinTransformOutput(
		const rsrcd::ResRef& res,
		const std::string& basePath,
		const std::string& stackFileName,
		stackimport::IResourceOutput* downstream,
		CResourceSummary& summary,
		bool& downstreamStopped)
		: res_(res)
		, basePath_(basePath)
		, stackFileName_(stackFileName)
		, downstream_(downstream)
		, summary_(summary)
		, downstreamStopped_(downstreamStopped)
	{}

	auto wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		if(payload.format == stackimport::ResourcePayloadFormat::Rgba32 ||
			payload.format == stackimport::ResourcePayloadFormat::JsonUtf8 ||
			payload.format == stackimport::ResourcePayloadFormat::Binary ||
			payload.format == stackimport::ResourcePayloadFormat::TextUtf8)
			return true;
		if(downstream_ == nullptr || downstreamStopped_)
			return false;
		return downstream_->wants_resource_payload(payload);
	}

	auto on_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		if(payload.format == stackimport::ResourcePayloadFormat::Rgba32)
			write_png_payload(payload);
		else if(payload.format == stackimport::ResourcePayloadFormat::JsonUtf8)
			write_json_payload(payload);
		else if(payload.format == stackimport::ResourcePayloadFormat::Binary)
			write_binary_payload(payload);
		else if(payload.format == stackimport::ResourcePayloadFormat::TextUtf8)
			write_text_payload(payload);
		if(downstream_ != nullptr && !downstreamStopped_ && !stackimport::emit_resource_payload(*downstream_, payload))
			downstreamStopped_ = true;
		return true;
	}

	auto on_resource_error(const stackimport::ResourceRef& resource, const char* msg) -> bool override
	{
		(void)resource;
		switch(resource_error_kind(resource_type_code(res_)))
		{
			case ResourceErrorKind::Parse:
				summary_.status = "parse_failed";
				break;
			case ResourceErrorKind::ConvertSound:
				summary_.status = "convert_failed";
				stackimport_emit_diagnosticf("Warning: Couldn't convert snd #%d: %s.\n", res_.id, msg);
				break;
			case ResourceErrorKind::ConvertPict:
				summary_.status = "convert_failed";
				stackimport_emit_diagnosticf("Warning: Couldn't render PICT #%d: %s.\n", res_.id, msg);
				break;
			case ResourceErrorKind::Disassemble:
				summary_.status = "disassembly_failed";
				stackimport_emit_diagnosticf("Warning: Couldn't disassemble '%s' #%d: %s.\n", summary_.type.c_str(), summary_.id, msg);
				break;
			case ResourceErrorKind::Other:
				break;
		}
		return true;
	}

	auto exported_count() const -> int { return exportedCount_; }

private:
	static auto artifact_format_for_payload(const stackimport::ResourcePayload& payload) -> const char*
	{
		if(payload.format == stackimport::ResourcePayloadFormat::Rgba32)
			return "png";
		if(payload.format == stackimport::ResourcePayloadFormat::JsonUtf8)
			return "json";
		if(payload.format == stackimport::ResourcePayloadFormat::TextUtf8)
			return "text";
		if(payload.format == stackimport::ResourcePayloadFormat::Binary && payload.media_type != nullptr)
		{
			if(std::strcmp(payload.media_type, "image/png") == 0)
				return "png";
			if(std::strcmp(payload.media_type, "audio/wav") == 0)
				return "wav";
		}
		return "binary";
	}

	static auto media_type_for_payload(const stackimport::ResourcePayload& payload) -> const char*
	{
		if(payload.format == stackimport::ResourcePayloadFormat::Rgba32)
			return "image/png";
		if(payload.media_type != nullptr)
			return payload.media_type;
		if(payload.format == stackimport::ResourcePayloadFormat::JsonUtf8)
			return "application/json";
		if(payload.format == stackimport::ResourcePayloadFormat::TextUtf8)
			return "text/plain; charset=utf-8";
		return "application/octet-stream";
	}

	void record_output_artifact(const std::string& relativePath, const stackimport::ResourcePayload& payload, bool primaryOutput)
	{
		if(primaryOutput && summary_.outputFile.empty())
			summary_.outputFile = relativePath;

		CResourceOutputArtifact artifact;
		artifact.path = relativePath;
		artifact.format = artifact_format_for_payload(payload);
		artifact.mediaType = media_type_for_payload(payload);
		artifact.variantIndex = payload.variant_index;
		if(payload.description != nullptr)
			artifact.description = payload.description;
		summary_.outputArtifacts.push_back(std::move(artifact));
	}

	void write_png_payload(const stackimport::ResourcePayload& payload)
	{
		if(payload.data.data == nullptr || payload.width == 0 || payload.height == 0 || payload.row_bytes == 0)
			return;

		char fname[64];
		const uint32_t type = resource_type_code(res_);
		if(type == fourcc("ICON"))
		{
			snprintf(fname, sizeof(fname), "ICON_%d.png", res_.id);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote ICON #%d as PNG.\n", res_.id);
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("ICN#"))
		{
			snprintf(fname, sizeof(fname), "ICN#_%d.png", res_.id);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote ICN# #%d as PNG.\n", res_.id);
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("CURS"))
		{
			snprintf(fname, sizeof(fname), "CURS_%d.png", res_.id);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote CURS #%d as PNG (hotspot %u,%u).\n", res_.id, static_cast<unsigned>(payload.hotspot_x), static_cast<unsigned>(payload.hotspot_y));
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("PAT#"))
		{
			snprintf(fname, sizeof(fname), "PAT#_%d_%02u.png", res_.id, static_cast<unsigned>(payload.variant_index));
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("PAT "))
		{
			snprintf(fname, sizeof(fname), "PAT_%d.png", res_.id);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote PAT #%d as PNG.\n", res_.id);
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("SICN"))
		{
			snprintf(fname, sizeof(fname), "SICN_%d_%02u.png", res_.id, static_cast<unsigned>(payload.variant_index));
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("ppat") || type == fourcc("ppt#"))
		{
			const char* suffixes[] = {"color", "color_tiled", "bitmap", "bitmap_tiled"};
			const uint32_t pattern_index = payload.variant_index / 4u;
			const uint32_t image_kind = payload.variant_index % 4u;
			if(type == fourcc("ppat"))
				snprintf(fname, sizeof(fname), "ppat_%d_%s.png", res_.id, suffixes[image_kind]);
			else
				snprintf(fname, sizeof(fname), "ppt#_%d_%02u_%s.png", res_.id, static_cast<unsigned>(pattern_index), suffixes[image_kind]);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("cicn"))
		{
			snprintf(fname, sizeof(fname), "cicn_%d.png", res_.id);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote cicn #%d as PNG.\n", res_.id);
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("crsr"))
		{
			if(payload.variant_index == 0)
				snprintf(fname, sizeof(fname), "crsr_%d.png", res_.id);
			else
				snprintf(fname, sizeof(fname), "crsr_%d_bitmap.png", res_.id);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(resource_type_is_indexed_icon(type))
		{
			format_fourcc_file_name(fname, sizeof(fname), type, res_.id, ".png");
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(resource_type_is_monochrome_icon_list(type))
		{
			char suffix[16];
			snprintf(suffix, sizeof(suffix), "_%02u.png", static_cast<unsigned>(payload.variant_index));
			format_fourcc_file_name(fname, sizeof(fname), type, res_.id, suffix);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type == fourcc("FONT") || type == fourcc("NFNT"))
		{
			format_fourcc_file_name(fname, sizeof(fname), type, res_.id, ".png");
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote %s #%d bitmap strike as PNG.\n", summary_.type.c_str(), res_.id);
			}
			else
				summary_.status = "export_failed";
			return;
		}
	}

	void write_binary_payload(const stackimport::ResourcePayload& payload)
	{
		if(payload.data.data == nullptr)
			return;

		const uint32_t type = resource_type_code(res_);
		if(type == fourcc("PICT") && payload.media_type != nullptr && std::strcmp(payload.media_type, "image/png") == 0)
		{
			char fname[64];
			snprintf(fname, sizeof(fname), "PICT_%d.png", res_.id);
			if(write_binary_file(output_path(basePath_, fname), payload.data))
			{
				summary_.status = "exported";
				record_output_artifact(fname, payload, true);
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote PICT #%d as PNG.\n", res_.id);
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(type != fourcc("snd "))
			return;

		const std::string soundsDir = output_path(basePath_, "sounds");
		if(stackimport_internal_make_directory(soundsDir.c_str()) == 0 || errno == EEXIST)
		{
			const std::string wavFileName = sanitized_resource_file_name(stackFileName_, "snd", res_.id, summary_.name, ".wav");
			if(write_binary_file(soundsDir + "/" + wavFileName, payload.data))
			{
				summary_.status = "exported";
				record_output_artifact(std::string("sounds/") + wavFileName, payload, true);
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote snd #%d as WAV.\n", res_.id);
			}
			else
				summary_.status = "export_failed";
		}
		else
			summary_.status = "output_directory_failed";
	}

	void write_json_payload(const stackimport::ResourcePayload& payload)
	{
		if(payload.data.data == nullptr)
			return;
		char fname[64];
		const char* stem = json_name_stem(resource_type_code(res_));
		if(stem == nullptr)
			return;
		snprintf(fname, sizeof(fname), "%s_%d.json", stem, res_.id);

		if(write_json_file(output_path(basePath_, fname), reinterpret_cast<const char*>(payload.data.data), payload.data.size))
		{
			summary_.status = "exported";
			record_output_artifact(fname, payload, true);
			exportedCount_++;
			stackimport_emit_infof("Status: Parsed %s #%d.\n", summary_.type.c_str(), res_.id);
		}
		else
			summary_.status = "export_failed";
	}

	void write_text_payload(const stackimport::ResourcePayload& payload)
	{
		if(payload.data.data == nullptr)
			return;
		const std::string text(reinterpret_cast<const char*>(payload.data.data), payload.data.size);
		std::string relativePath;
		const uint32_t type = resource_type_code(res_);
		const bool isDisassembly = resource_type_is_disassembly(type);
		if(isDisassembly)
		{
			const std::string architecture = summary_.architecture.empty() ? "mac68k" : summary_.architecture;
			const std::string typeAndArchitecture = summary_.type + "_" + architecture;
			relativePath = std::string("resource-disassembly/") + sanitized_resource_file_name(stackFileName_, typeAndArchitecture, summary_.id, summary_.name, ".s");
		}
		else if(resource_type_is_text(type))
		{
			const std::string textDir = output_path(basePath_, "resource-text");
			if(stackimport_internal_make_directory(textDir.c_str()) != 0 && errno != EEXIST)
			{
				summary_.status = "output_directory_failed";
				return;
			}
			relativePath = std::string("resource-text/") + sanitized_resource_file_name(stackFileName_, summary_.type, summary_.id, summary_.name, ".txt");
		}
		else
			return;

		if(write_text_file(output_path(basePath_, relativePath.c_str()), text))
		{
			if(isDisassembly)
			{
				summary_.status = "disassembled";
				summary_.disassemblyFile = relativePath;
				record_output_artifact(relativePath, payload, false);
				const std::string architecture = summary_.architecture.empty() ? "mac68k" : summary_.architecture;
				stackimport_emit_infof("Status: Wrote %s disassembly for '%s' #%d.\n", architecture.c_str(), summary_.type.c_str(), summary_.id);
			}
			else
			{
				summary_.status = "exported";
				record_output_artifact(relativePath, payload, true);
				stackimport_emit_infof("Status: Wrote %s #%d as UTF-8 text.\n", summary_.type.c_str(), summary_.id);
			}
			exportedCount_++;
		}
		else
			summary_.status = isDisassembly ? "disassembly_write_failed" : "export_failed";
	}

	const rsrcd::ResRef& res_;
	const std::string& basePath_;
	const std::string& stackFileName_;
	stackimport::IResourceOutput* downstream_;
	CResourceSummary& summary_;
	bool& downstreamStopped_;
	int exportedCount_ = 0;
};

} // namespace

bool stackimport_load_resource_fork(
	const std::string& fpath,
	const std::string& basePath,
	const std::string& stackFileName,
	stackimport::IResourceOutput* resourceOutput,
	std::vector<CResourceSummary>& resourceSummaries,
	std::string& resourceForkStatus,
	uint64_t& resourceForkBytes)
{
	resourceSummaries.clear();
	resourceForkBytes = 0;

	const std::string resourceForkPath = fpath + "/..namedfork/rsrc";
	std::vector<uint8_t> resourceForkData;
	if(!read_binary_file(resourceForkPath, resourceForkData))
	{
		resourceForkStatus = "missing_or_empty";
		return true;
	}
	resourceForkBytes = static_cast<uint64_t>(resourceForkData.size());
	if(resourceForkData.empty())
	{
		resourceForkStatus = "missing_or_empty";
		return true;
	}

	rsrcd::VecParserOutput<256> parsed;
	auto parseResult = rsrcd::Parser{}.parse_fork(
		rsrcd::Bytes{resourceForkData.data(), resourceForkData.size()},
		parsed);
	if(!parseResult || parsed.count() == 0)
	{
		resourceForkStatus = parsed.count() == 0 ? "empty" : "parse_failed";
		return true;
	}

	const std::string disassemblyDir = output_path(basePath, "resource-disassembly");
	if(stackimport_internal_make_directory(disassemblyDir.c_str()) != 0 && errno != EEXIST)
	{
		stackimport_emit_diagnosticf("Warning: Couldn't create resource disassembly directory '%s'.\n", disassemblyDir.c_str());
		resourceForkStatus = "output_directory_failed";
		return true;
	}

	bool resourceForkHadInvalidResources = false;
	bool resourceStreamingStopped = false;
	for(size_t i = 0; i < parsed.count(); i++)
	{
		const rsrcd::ResRef& res = parsed.at(i);

		CResourceSummary summary;
		summary.typeCode = 0;
		summary.id = res.id;
		summary.flags = res.flags;
		summary.bytes = res.data.size;
		summary.status = "preserved";
		if(res.type.size != 4 || res.type.data == nullptr)
		{
			summary.type = "????";
			summary.status = "invalid_type";
			resourceForkHadInvalidResources = true;
			resourceSummaries.push_back(summary);
			stackimport_emit_diagnosticf("Warning: Skipping resource with invalid type view at index %zu.\n", i);
			continue;
		}
		summary.type.assign(reinterpret_cast<const char*>(res.type.data), 4);
		summary.typeCode = rsrcd::read_u32be(res.type.data);
		if(!res.name.empty() && res.name.data != nullptr)
			summary.name.assign(reinterpret_cast<const char*>(res.name.data), res.name.size);

		const stackimport::ResourceRef resourceRef = stackimport::resource_ref_from_resref(res);
		if(!resourceStreamingStopped && !stackimport::emit_resource_payload(resourceOutput, stackimport::make_native_resource_payload(resourceRef, res.data)))
			resourceStreamingStopped = true;

		if(summary.typeCode == fourcc("XCMD") || summary.typeCode == fourcc("XFCN"))
			summary.architecture = "mac68k";
		else if(summary.typeCode == fourcc("xcmd") || summary.typeCode == fourcc("xfcn"))
			summary.architecture = "macppc";

		PackageBuiltinTransformOutput transformOutput(res, basePath, stackFileName, resourceOutput, summary, resourceStreamingStopped);
		stackimport::emit_builtin_resource_transforms(res, resourceRef, transformOutput);
		resourceSummaries.push_back(summary);
	}

	resourceForkStatus = resourceForkHadInvalidResources ? "partial" : "ok";
	return true;
}
