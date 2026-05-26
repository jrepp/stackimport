#include "CStackFile.h"
#include "Mac68kDisassembly.h"
#include "StackImportSoundConverter.h"

#include <cerrno>
#include <cstdio>

#include "stackimport_logging.h"
#include "stackimport_platform_internal.h"
#include "stackimport_rapidjson_allocator.h"
#include "vendor/rsrcd/include/rsrcd.hpp"
#include "stb_image_write.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

using JsonPoolAllocator = rapidjson::MemoryPoolAllocator<StackImportRapidJsonAllocator>;
using JsonDocument = rapidjson::GenericDocument<rapidjson::UTF8<>, JsonPoolAllocator, StackImportRapidJsonAllocator>;
using JsonValue = rapidjson::GenericValue<rapidjson::UTF8<>, JsonPoolAllocator>;
using JsonStringBuffer = rapidjson::GenericStringBuffer<rapidjson::UTF8<>, StackImportRapidJsonAllocator>;
using JsonWriter = rapidjson::PrettyWriter<JsonStringBuffer, rapidjson::UTF8<>, rapidjson::UTF8<>, StackImportRapidJsonAllocator>;

extern unsigned char sMacRomanToUTF8Table[128][5];

namespace {

std::string output_path(const std::string& basePath, const char* fileName)
{
	std::string result = basePath;
	if(!result.empty() && result[result.size() - 1] != '/')
		result += '/';
	result += fileName;
	return result;
}

JsonValue json_string(const std::string& value, JsonPoolAllocator& allocator)
{
	JsonValue result;
	result.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
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

struct PlatformPngWriter {
	stackimport_file_handle file;
	bool ok;
};

void write_png_chunk(void* context, void* data, int size)
{
	auto* writer = static_cast<PlatformPngWriter*>(context);
	if(!writer || !writer->ok || size < 0)
		return;
	const size_t bytes = static_cast<size_t>(size);
	writer->ok = stackimport_internal_write_file(writer->file, data, bytes) == bytes;
}

bool write_png_file(const std::string& path, int width, int height, int components, const void* data, int strideBytes)
{
	stackimport_file_handle file = stackimport_internal_open_file(path.c_str(), "wb");
	if(!file)
		return false;

	PlatformPngWriter writer{file, true};
	const int encoded = stbi_write_png_to_func(write_png_chunk, &writer, width, height, components, data, strideBytes);
	const int closeStatus = stackimport_internal_close_file(file);
	return encoded != 0 && writer.ok && closeStatus == 0;
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

static void swap_bgra_to_rgba(uint8_t* data, int pixel_count)
{
	for(int i = 0; i < pixel_count; i++)
	{
		uint8_t tmp = data[i * 4];
		data[i * 4] = data[i * 4 + 2];
		data[i * 4 + 2] = tmp;
	}
}

std::string mac_roman_from_bytes(const uint8_t* data, size_t len)
{
	std::string result;
	for(size_t i = 0; i < len; i++)
	{
		unsigned char ch = data[i];
		if(ch >= 128)
			result.append(reinterpret_cast<const char*>(sMacRomanToUTF8Table[ch - 128]));
		else if(ch == 0x11)
		{
			static unsigned char commandKey[4] = {0xe2, 0x8c, 0x98, 0};
			result.append(reinterpret_cast<const char*>(commandKey));
		}
		else
			result.push_back(static_cast<char>(ch));
	}
	return result;
}

stackimport::ResourceRef resource_ref_from_rsrcd(const rsrcd::ResRef& res)
{
	stackimport::ResourceRef ref{};
	ref.type = rsrcd::FourCC::from_bytes(res.type.data);
	ref.id = res.id;
	ref.name = res.name;
	ref.flags = res.flags;
	ref.order = res.order;
	ref.native_size = res.data.size;
	return ref;
}

bool emit_resource_payload(stackimport::IResourceOutput* output, const stackimport::ResourcePayload& payload)
{
	if(!output)
		return true;
	stackimport::ResourcePayload descriptor = payload;
	descriptor.data.data = nullptr;
	if(!output->wants_resource_payload(descriptor))
		return true;
	return output->on_resource_payload(payload);
}

stackimport::ResourcePayload native_resource_payload(const stackimport::ResourceRef& ref, rsrcd::Bytes data)
{
	stackimport::ResourcePayload payload{};
	payload.resource = ref;
	payload.format = stackimport::ResourcePayloadFormat::Native;
	payload.data = data;
	payload.media_type = "application/octet-stream";
	payload.description = "native resource data";
	return payload;
}

stackimport::ResourcePayload converted_resource_payload(
	const stackimport::ResourceRef& ref,
	stackimport::ResourcePayloadFormat format,
	rsrcd::Bytes data,
	const char* mediaType,
	const char* description)
{
	stackimport::ResourcePayload payload{};
	payload.resource = ref;
	payload.format = format;
	payload.data = data;
	payload.media_type = mediaType;
	payload.description = description;
	return payload;
}

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

		const stackimport::ResourceRef resourceRef = resource_ref_from_rsrcd(res);
		if(!resourceStreamingStopped && !emit_resource_payload(resourceOutput, native_resource_payload(resourceRef, res.data)))
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
			if(res.data.size == 128)
			{
				uint8_t bgra[32 * 32 * 4];
				rsrcd::MutableBytes dst{bgra, sizeof(bgra)};
				if(rsrcd::img::decode_icon_bw(res.data, dst))
				{
					swap_bgra_to_rgba(bgra, 32 * 32);
					if(!resourceStreamingStopped)
					{
						auto payload = converted_resource_payload(resourceRef, stackimport::ResourcePayloadFormat::Rgba32, rsrcd::Bytes{bgra, sizeof(bgra)}, "image/x-rgba32", "decoded 32x32 ICON pixels");
						payload.width = 32;
						payload.height = 32;
						payload.row_bytes = 32 * 4;
						if(!emit_resource_payload(resourceOutput, payload))
							resourceStreamingStopped = true;
					}
					char fname[64];
					snprintf(fname, sizeof(fname), "ICON_%d.png", res.id);
					if(write_png_file(output_path(basePath, fname), 32, 32, 4, bgra, 32 * 4))
					{
						summary.status = "exported";
						summary.outputFile = fname;
						stackimport_emit_infof("Status: Wrote ICON #%d as PNG.\n", res.id);
					}
					else
						summary.status = "export_failed";
				}
			}
			resourceSummaries.push_back(summary);
			continue;
		}
		else if(std::memcmp(res.type.data, "CURS", 4) == 0)
		{
			if(res.data.size >= 68)
			{
				uint8_t bgra[16 * 16 * 4];
				rsrcd::MutableBytes dst{bgra, sizeof(bgra)};
				int16_t hot_x = 0, hot_y = 0;
				if(rsrcd::img::decode_curs(res.data, dst, hot_x, hot_y))
				{
					swap_bgra_to_rgba(bgra, 16 * 16);
					if(!resourceStreamingStopped)
					{
						auto payload = converted_resource_payload(resourceRef, stackimport::ResourcePayloadFormat::Rgba32, rsrcd::Bytes{bgra, sizeof(bgra)}, "image/x-rgba32", "decoded 16x16 CURS pixels");
						payload.width = 16;
						payload.height = 16;
						payload.row_bytes = 16 * 4;
						payload.hotspot_x = hot_x;
						payload.hotspot_y = hot_y;
						if(!emit_resource_payload(resourceOutput, payload))
							resourceStreamingStopped = true;
					}
					char fname[64];
					snprintf(fname, sizeof(fname), "CURS_%d.png", res.id);
					if(write_png_file(output_path(basePath, fname), 16, 16, 4, bgra, 16 * 4))
					{
						summary.status = "exported";
						summary.outputFile = fname;
						stackimport_emit_infof("Status: Wrote CURS #%d as PNG (hotspot %u,%u).\n", res.id, static_cast<unsigned>(hot_x), static_cast<unsigned>(hot_y));
					}
					else
						summary.status = "export_failed";
				}
			}
			resourceSummaries.push_back(summary);
			continue;
		}
		else if(std::memcmp(res.type.data, "PAT#", 4) == 0)
		{
			size_t patCount = rsrcd::patlist::count(res.data);
			int exported = 0;
			for(size_t pi = 0; pi < patCount; pi++)
			{
				rsrcd::Bytes pat = rsrcd::patlist::pattern_at(res.data, pi);
				if(pat.size == 8)
				{
					uint8_t bgra[8 * 8 * 4];
					rsrcd::MutableBytes dst{bgra, sizeof(bgra)};
					rsrcd::img::decode_pat(pat, dst);
					swap_bgra_to_rgba(bgra, 8 * 8);
					if(!resourceStreamingStopped)
					{
						auto payload = converted_resource_payload(resourceRef, stackimport::ResourcePayloadFormat::Rgba32, rsrcd::Bytes{bgra, sizeof(bgra)}, "image/x-rgba32", "decoded 8x8 PAT# pattern pixels");
						payload.variant_index = static_cast<uint32_t>(pi);
						payload.width = 8;
						payload.height = 8;
						payload.row_bytes = 8 * 4;
						if(!emit_resource_payload(resourceOutput, payload))
							resourceStreamingStopped = true;
					}
					char fname[64];
					snprintf(fname, sizeof(fname), "PAT#_%d_%02zu.png", res.id, pi);
					if(write_png_file(output_path(basePath, fname), 8, 8, 4, bgra, 8 * 4))
						exported++;
				}
			}
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
			stackimport::PlatformByteVector wavData;
			std::string error;
			if(stackimport::ConvertSndResourceToWav(res.data, wavData, error))
			{
				if(!resourceStreamingStopped)
				{
					auto payload = converted_resource_payload(resourceRef, stackimport::ResourcePayloadFormat::Binary, rsrcd::Bytes{wavData.data(), wavData.size()}, "audio/wav", "converted 'snd ' resource audio");
					if(!emit_resource_payload(resourceOutput, payload))
						resourceStreamingStopped = true;
				}
				const std::string soundsDir = output_path(basePath, "sounds");
				if(stackimport_internal_make_directory(soundsDir.c_str()) == 0 || errno == EEXIST)
				{
					const std::string wavFileName = sanitized_resource_file_name(stackFileName, "snd", res.id, summary.name, ".wav");
					if(write_binary_file(soundsDir + "/" + wavFileName, rsrcd::Bytes{wavData.data(), wavData.size()}))
					{
						summary.status = "exported";
						summary.outputFile = std::string("sounds/") + wavFileName;
						stackimport_emit_infof("Status: Wrote snd #%d as WAV.\n", res.id);
					}
					else
						summary.status = "export_failed";
				}
				else
					summary.status = "output_directory_failed";
			}
			else
			{
				summary.status = "convert_failed";
				stackimport_emit_diagnosticf("Warning: Couldn't convert snd #%d: %s.\n", res.id, error.c_str());
			}
			resourceSummaries.push_back(summary);
			continue;
		}
		else if(std::memcmp(res.type.data, "PLTE", 4) == 0)
		{
			rsrcd::plte::Palette<64> pal;
			auto plteResult = rsrcd::plte::parse(res.data, pal);
			if(plteResult)
			{
				StackImportRapidJsonAllocator baseAlloc;
				JsonPoolAllocator pool(1024, &baseAlloc);
				JsonDocument doc(&pool, 1024, &baseAlloc);
				doc.SetObject();
				JsonPoolAllocator& a = doc.GetAllocator();
				doc.AddMember("wdef", pal.wdef, a);
				doc.AddMember("showName", pal.show_name, a);
				doc.AddMember("selection", pal.selection, a);
				doc.AddMember("frame", pal.frame, a);
				doc.AddMember("pictRef", pal.pict_ref, a);
				doc.AddMember("top", pal.top, a);
				doc.AddMember("left", pal.left, a);

				JsonValue buttons(rapidjson::kArrayType);
				for(size_t bi = 0; bi < pal.button_count; bi++)
				{
					JsonValue btn(rapidjson::kObjectType);
					const auto& b = pal.buttons[bi];
					btn.AddMember("top", b.top, a);
					btn.AddMember("left", b.left, a);
					btn.AddMember("bottom", b.bottom, a);
					btn.AddMember("right", b.right, a);
					if(!b.message.empty())
					{
						std::string msg = mac_roman_from_bytes(b.message.data, b.message.size);
						btn.AddMember("message", json_string(msg, a), a);
					}
					buttons.PushBack(btn, a);
				}
				doc.AddMember("buttons", buttons, a);

				JsonStringBuffer jsonBuffer(&baseAlloc);
				JsonWriter writer(jsonBuffer, &baseAlloc);
				doc.Accept(writer);
				if(!resourceStreamingStopped)
				{
					auto payload = converted_resource_payload(resourceRef, stackimport::ResourcePayloadFormat::JsonUtf8, rsrcd::Bytes{reinterpret_cast<const uint8_t*>(jsonBuffer.GetString()), jsonBuffer.GetSize()}, "application/json", "parsed PLTE palette metadata");
					if(!emit_resource_payload(resourceOutput, payload))
						resourceStreamingStopped = true;
				}

				char fname[64];
				snprintf(fname, sizeof(fname), "PLTE_%d.json", res.id);
				if(write_json_file(output_path(basePath, fname), jsonBuffer.GetString(), jsonBuffer.GetSize()))
				{
					summary.status = "exported";
					summary.outputFile = fname;
					stackimport_emit_infof("Status: Parsed PLTE #%d (%zu buttons).\n", res.id, pal.button_count);
				}
				else
					summary.status = "export_failed";
			}
			else
				summary.status = "parse_failed";
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
				auto payload = converted_resource_payload(resourceRef, stackimport::ResourcePayloadFormat::TextUtf8, rsrcd::Bytes{reinterpret_cast<const uint8_t*>(disassembly.text.data()), disassembly.text.size()}, "text/x-asm; charset=utf-8", is68K ? "Mac 68K disassembly" : "PowerPC disassembly");
				if(!emit_resource_payload(resourceOutput, payload))
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
