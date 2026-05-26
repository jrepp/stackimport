#include "CStackFile.h"
#include "Mac68kDisassembly.h"
#include "StackImportPngWriter.h"
#include "StackImportResourceTransforms.h"
#include "StackImportResourceTypes.h"

#include <cerrno>
#include <cstdio>

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

bool resource_type_is(const rsrcd::ResRef& res, const char* type)
{
	return res.type.size == 4 && res.type.data != nullptr && std::memcmp(res.type.data, type, 4) == 0;
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
			payload.format == stackimport::ResourcePayloadFormat::Binary)
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
		if(downstream_ != nullptr && !downstreamStopped_ && !stackimport::emit_resource_payload(*downstream_, payload))
			downstreamStopped_ = true;
		return true;
	}

	auto on_resource_error(const stackimport::ResourceRef& resource, const char* msg) -> bool override
	{
		(void)resource;
		(void)msg;
		if(resource_type_is(res_, "PLTE"))
			summary_.status = "parse_failed";
		else if(resource_type_is(res_, "snd "))
		{
			summary_.status = "convert_failed";
			stackimport_emit_diagnosticf("Warning: Couldn't convert snd #%d: %s.\n", res_.id, msg);
		}
		return true;
	}

	auto exported_count() const -> int { return exportedCount_; }

private:
	void write_png_payload(const stackimport::ResourcePayload& payload)
	{
		if(payload.data.data == nullptr || payload.width == 0 || payload.height == 0 || payload.row_bytes == 0)
			return;

		char fname[64];
		if(resource_type_is(res_, "ICON"))
		{
			snprintf(fname, sizeof(fname), "ICON_%d.png", res_.id);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				summary_.outputFile = fname;
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote ICON #%d as PNG.\n", res_.id);
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(resource_type_is(res_, "CURS"))
		{
			snprintf(fname, sizeof(fname), "CURS_%d.png", res_.id);
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
			{
				summary_.status = "exported";
				summary_.outputFile = fname;
				exportedCount_++;
				stackimport_emit_infof("Status: Wrote CURS #%d as PNG (hotspot %u,%u).\n", res_.id, static_cast<unsigned>(payload.hotspot_x), static_cast<unsigned>(payload.hotspot_y));
			}
			else
				summary_.status = "export_failed";
			return;
		}

		if(resource_type_is(res_, "PAT#"))
		{
			snprintf(fname, sizeof(fname), "PAT#_%d_%02u.png", res_.id, static_cast<unsigned>(payload.variant_index));
			if(stackimport::WritePngFile(output_path(basePath_, fname), static_cast<int>(payload.width), static_cast<int>(payload.height), 4, payload.data.data, static_cast<int>(payload.row_bytes)))
				exportedCount_++;
		}
	}

	void write_binary_payload(const stackimport::ResourcePayload& payload)
	{
		if(!resource_type_is(res_, "snd ") || payload.data.data == nullptr)
			return;

		const std::string soundsDir = output_path(basePath_, "sounds");
		if(stackimport_internal_make_directory(soundsDir.c_str()) == 0 || errno == EEXIST)
		{
			const std::string wavFileName = sanitized_resource_file_name(stackFileName_, "snd", res_.id, summary_.name, ".wav");
			if(write_binary_file(soundsDir + "/" + wavFileName, payload.data))
			{
				summary_.status = "exported";
				summary_.outputFile = std::string("sounds/") + wavFileName;
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
		if(!resource_type_is(res_, "PLTE") || payload.data.data == nullptr)
			return;
		char fname[64];
		snprintf(fname, sizeof(fname), "PLTE_%d.json", res_.id);
		if(write_json_file(output_path(basePath_, fname), reinterpret_cast<const char*>(payload.data.data), payload.data.size))
		{
			summary_.status = "exported";
			summary_.outputFile = fname;
			exportedCount_++;
			stackimport_emit_infof("Status: Parsed PLTE #%d.\n", res_.id);
		}
		else
			summary_.status = "export_failed";
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

		const bool is68K = std::memcmp(res.type.data, "XCMD", 4) == 0 || std::memcmp(res.type.data, "XFCN", 4) == 0;
		const bool isPPC = std::memcmp(res.type.data, "xcmd", 4) == 0 || std::memcmp(res.type.data, "xfcn", 4) == 0;

		stackimport::Mac68kDisassemblyResult disassembly;
		if(is68K)
		{
			summary.architecture = "mac68k";
			disassembly = stackimport::DisassembleMac68kCodeResource(std::span<const uint8_t>(res.data.data, res.data.size), 0, res.data.size, 0);
		}
		else if(isPPC)
		{
			summary.architecture = "macppc";
			disassembly = stackimport::DisassemblePowerPCCodeResource(std::span<const uint8_t>(res.data.data, res.data.size), 0, res.data.size, 0);
		}
		else if(std::memcmp(res.type.data, "ICON", 4) == 0)
		{
			PackageBuiltinTransformOutput transformOutput(res, basePath, stackFileName, resourceOutput, summary, resourceStreamingStopped);
			stackimport::emit_builtin_resource_transforms(res, resourceRef, transformOutput);
			resourceSummaries.push_back(summary);
			continue;
		}
		else if(std::memcmp(res.type.data, "CURS", 4) == 0)
		{
			PackageBuiltinTransformOutput transformOutput(res, basePath, stackFileName, resourceOutput, summary, resourceStreamingStopped);
			stackimport::emit_builtin_resource_transforms(res, resourceRef, transformOutput);
			resourceSummaries.push_back(summary);
			continue;
		}
		else if(std::memcmp(res.type.data, "PAT#", 4) == 0)
		{
			size_t patCount = rsrcd::patlist::count(res.data);
			PackageBuiltinTransformOutput transformOutput(res, basePath, stackFileName, resourceOutput, summary, resourceStreamingStopped);
			stackimport::emit_builtin_resource_transforms(res, resourceRef, transformOutput);
			int exported = transformOutput.exported_count();
			if(exported > 0)
			{
				summary.status = "exported";
				stackimport_emit_infof("Status: Wrote %d/%zu patterns from PAT# #%d as PNG.\n", exported, patCount, res.id);
			}
			resourceSummaries.push_back(summary);
			continue;
		}
		else if(std::memcmp(res.type.data, "snd ", 4) == 0)
		{
			PackageBuiltinTransformOutput transformOutput(res, basePath, stackFileName, resourceOutput, summary, resourceStreamingStopped);
			stackimport::emit_builtin_resource_transforms(res, resourceRef, transformOutput);
			resourceSummaries.push_back(summary);
			continue;
		}
		else if(std::memcmp(res.type.data, "PLTE", 4) == 0)
		{
			PackageBuiltinTransformOutput transformOutput(res, basePath, stackFileName, resourceOutput, summary, resourceStreamingStopped);
			stackimport::emit_builtin_resource_transforms(res, resourceRef, transformOutput);
			resourceSummaries.push_back(summary);
			continue;
		}
		else
		{
			resourceSummaries.push_back(summary);
			continue;
		}

		if(disassembly.ok)
		{
			if(!resourceStreamingStopped)
			{
				auto payload = stackimport::make_converted_resource_payload(resourceRef, stackimport::ResourcePayloadFormat::TextUtf8, rsrcd::Bytes{reinterpret_cast<const uint8_t*>(disassembly.text.data()), disassembly.text.size()}, "text/x-asm; charset=utf-8", is68K ? "Mac 68K disassembly" : "PowerPC disassembly");
				if(!stackimport::emit_resource_payload(resourceOutput, payload))
					resourceStreamingStopped = true;
			}
			const std::string typeAndArchitecture = summary.type + "_" + summary.architecture;
			const std::string relativePath = std::string("resource-disassembly/") + sanitized_resource_file_name(stackFileName, typeAndArchitecture, summary.id, summary.name, ".s");
			if(write_text_file(output_path(basePath, relativePath.c_str()), disassembly.text))
			{
				summary.status = "disassembled";
				summary.disassemblyFile = relativePath;
				stackimport_emit_infof("Status: Wrote %s disassembly for '%s' #%d.\n", summary.architecture.c_str(), summary.type.c_str(), summary.id);
			}
			else
				summary.status = "disassembly_write_failed";
		}
		else
		{
			summary.status = is68K || isPPC ? "disassembly_failed" : summary.status;
			if(!disassembly.error.empty())
				stackimport_emit_diagnosticf("Warning: Couldn't disassemble '%s' #%d: %s.\n", summary.type.c_str(), summary.id, disassembly.error.c_str());
		}
		resourceSummaries.push_back(summary);
	}

	resourceForkStatus = resourceForkHadInvalidResources ? "partial" : "ok";
	return true;
}
