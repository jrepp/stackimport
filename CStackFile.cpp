/*
 *  CStackFile.cpp
 *  stackimport
 *
 *  Created by Mr. Z. on 10/06/06.
 *  Copyright 2006 Mr Z. All rights reserved.
 *
 */


#include "CStackFile.h"
#include <cerrno>
#include <iostream>
#include <fstream>
#include <memory>
#include <span>
#include <cstdio>
#include <cstdarg>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include "picture.h"
#include "woba.h"
#include "CBuf.h"
#include "stackimport_logging.h"
#include "stackimport_platform_internal.h"
#include "stackimport_rapidjson_allocator.h"
#include "include/stackimport_sax.hpp"

#include "RomDasm.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <cctype>
#include <set>
#if defined(__APPLE__)
#include <sys/xattr.h>
#endif

// Table of C-strings for converting the non-ASCII MacRoman characters (above 127)
//	into the requisite UTF8 byte sequences:
unsigned char	sMacRomanToUTF8Table[128][5] =
{
	{ 0xc3, 0x84, 0x00, 0x00 }, { 0xc3, 0x85, 0x00, 0x00 }, { 0xc3, 0x87, 0x00, 0x00 }, { 0xc3, 0x89, 0x00, 0x00 },
	{ 0xc3, 0x91, 0x00, 0x00 }, { 0xc3, 0x96, 0x00, 0x00 }, { 0xc3, 0x9c, 0x00, 0x00 }, { 0xc3, 0xa1, 0x00, 0x00 },
	{ 0xc3, 0xa0, 0x00, 0x00 }, { 0xc3, 0xa2, 0x00, 0x00 }, { 0xc3, 0xa4, 0x00, 0x00 }, { 0xc3, 0xa3, 0x00, 0x00 },
	{ 0xc3, 0xa5, 0x00, 0x00 }, { 0xc3, 0xa7, 0x00, 0x00 }, { 0xc3, 0xa9, 0x00, 0x00 }, { 0xc3, 0xa8, 0x00, 0x00 },
	{ 0xc3, 0xaa, 0x00, 0x00 }, { 0xc3, 0xab, 0x00, 0x00 }, { 0xc3, 0xad, 0x00, 0x00 }, { 0xc3, 0xac, 0x00, 0x00 },
	{ 0xc3, 0xae, 0x00, 0x00 }, { 0xc3, 0xaf, 0x00, 0x00 }, { 0xc3, 0xb1, 0x00, 0x00 }, { 0xc3, 0xb3, 0x00, 0x00 },
	{ 0xc3, 0xb2, 0x00, 0x00 }, { 0xc3, 0xb4, 0x00, 0x00 }, { 0xc3, 0xb6, 0x00, 0x00 }, { 0xc3, 0xb5, 0x00, 0x00 },
	{ 0xc3, 0xba, 0x00, 0x00 }, { 0xc3, 0xb9, 0x00, 0x00 }, { 0xc3, 0xbb, 0x00, 0x00 }, { 0xc3, 0xbc, 0x00, 0x00 },
	{ 0xe2, 0x80, 0xa0, 0x00 }, { 0xc2, 0xb0, 0x00, 0x00 }, { 0xc2, 0xa2, 0x00, 0x00 }, { 0xc2, 0xa3, 0x00, 0x00 },
	{ 0xc2, 0xa7, 0x00, 0x00 }, { 0xe2, 0x80, 0xa2, 0x00 }, { 0xc2, 0xb6, 0x00, 0x00 }, { 0xc3, 0x9f, 0x00, 0x00 },
	{ 0xc2, 0xae, 0x00, 0x00 }, { 0xc2, 0xa9, 0x00, 0x00 }, { 0xe2, 0x84, 0xa2, 0x00 }, { 0xc2, 0xb4, 0x00, 0x00 },
	{ 0xc2, 0xa8, 0x00, 0x00 }, { 0xe2, 0x89, 0xa0, 0x00 }, { 0xc3, 0x86, 0x00, 0x00 }, { 0xc3, 0x98, 0x00, 0x00 },
	{ 0xe2, 0x88, 0x9e, 0x00 }, { 0xc2, 0xb1, 0x00, 0x00 }, { 0xe2, 0x89, 0xa4, 0x00 }, { 0xe2, 0x89, 0xa5, 0x00 },
	{ 0xc2, 0xa5, 0x00, 0x00 }, { 0xc2, 0xb5, 0x00, 0x00 }, { 0xe2, 0x88, 0x82, 0x00 }, { 0xe2, 0x88, 0x91, 0x00 },
	{ 0xe2, 0x88, 0x8f, 0x00 }, { 0xcf, 0x80, 0x00, 0x00 }, { 0xe2, 0x88, 0xab, 0x00 }, { 0xc2, 0xaa, 0x00, 0x00 },
	{ 0xc2, 0xba, 0x00, 0x00 }, { 0xce, 0xa9, 0x00, 0x00 }, { 0xc3, 0xa6, 0x00, 0x00 }, { 0xc3, 0xb8, 0x00, 0x00 },
	{ 0xc2, 0xbf, 0x00, 0x00 }, { 0xc2, 0xa1, 0x00, 0x00 }, { 0xc2, 0xac, 0x00, 0x00 }, { 0xe2, 0x88, 0x9a, 0x00 },
	{ 0xc6, 0x92, 0x00, 0x00 }, { 0xe2, 0x89, 0x88, 0x00 }, { 0xe2, 0x88, 0x86, 0x00 }, { 0xc2, 0xab, 0x00, 0x00 },
	{ 0xc2, 0xbb, 0x00, 0x00 }, { 0xe2, 0x80, 0xa6, 0x00 }, { 0xc2, 0xa0, 0x00, 0x00 }, { 0xc3, 0x80, 0x00, 0x00 },
	{ 0xc3, 0x83, 0x00, 0x00 }, { 0xc3, 0x95, 0x00, 0x00 }, { 0xc5, 0x92, 0x00, 0x00 }, { 0xc5, 0x93, 0x00, 0x00 },
	{ 0xe2, 0x80, 0x93, 0x00 }, { 0xe2, 0x80, 0x94, 0x00 }, { 0xe2, 0x80, 0x9c, 0x00 }, { 0xe2, 0x80, 0x9d, 0x00 },
	{ 0xe2, 0x80, 0x98, 0x00 }, { 0xe2, 0x80, 0x99, 0x00 }, { 0xc3, 0xb7, 0x00, 0x00 }, { 0xe2, 0x97, 0x8a, 0x00 },
	{ 0xc3, 0xbf, 0x00, 0x00 }, { 0xc5, 0xb8, 0x00, 0x00 }, { 0xe2, 0x81, 0x84, 0x00 }, { 0xe2, 0x82, 0xac, 0x00 },
	{ 0xe2, 0x80, 0xb9, 0x00 }, { 0xe2, 0x80, 0xba, 0x00 }, { 0xef, 0xac, 0x81, 0x00 }, { 0xef, 0xac, 0x82, 0x00 },
	{ 0xe2, 0x80, 0xa1, 0x00 }, { 0xc2, 0xb7, 0x00, 0x00 }, { 0xe2, 0x80, 0x9a, 0x00 }, { 0xe2, 0x80, 0x9e, 0x00 },
	{ 0xe2, 0x80, 0xb0, 0x00 }, { 0xc3, 0x82, 0x00, 0x00 }, { 0xc3, 0x8a, 0x00, 0x00 }, { 0xc3, 0x81, 0x00, 0x00 },
	{ 0xc3, 0x8b, 0x00, 0x00 }, { 0xc3, 0x88, 0x00, 0x00 }, { 0xc3, 0x8d, 0x00, 0x00 }, { 0xc3, 0x8e, 0x00, 0x00 },
	{ 0xc3, 0x8f, 0x00, 0x00 }, { 0xc3, 0x8c, 0x00, 0x00 }, { 0xc3, 0x93, 0x00, 0x00 }, { 0xc3, 0x94, 0x00, 0x00 },
	{ 0xef, 0xa3, 0xbf, 0x00 }, { 0xc3, 0x92, 0x00, 0x00 }, { 0xc3, 0x9a, 0x00, 0x00 }, { 0xc3, 0x9b, 0x00, 0x00 },
	{ 0xc3, 0x99, 0x00, 0x00 }, { 0xc4, 0xb1, 0x00, 0x00 }, { 0xcb, 0x86, 0x00, 0x00 }, { 0xcb, 0x9c, 0x00, 0x00 },
	{ 0xc2, 0xaf, 0x00, 0x00 }, { 0xcb, 0x98, 0x00, 0x00 }, { 0xcb, 0x99, 0x00, 0x00 }, { 0xcb, 0x9a, 0x00, 0x00 },
	{ 0xc2, 0xb8, 0x00, 0x00 }, { 0xcb, 0x9d, 0x00, 0x00 }, { 0xcb, 0x9b, 0x00, 0x00 }, { 0xcb, 0x87, 0x00, 0x00 }
};

size_t MacRomanStringEnd( const CBuf& data, size_t startOffs );

bool stackimport_load_resource_fork(
	const std::string& fpath,
	const std::string& basePath,
	const std::string& stackFileName,
	stackimport::IResourceOutput* resourceOutput,
	std::vector<CResourceSummary>& resourceSummaries,
	std::string& resourceForkStatus,
	uint64_t& resourceForkBytes);

namespace {

using JsonPoolAllocator = rapidjson::MemoryPoolAllocator<StackImportRapidJsonAllocator>;
using JsonDocument = rapidjson::GenericDocument<rapidjson::UTF8<>, JsonPoolAllocator, StackImportRapidJsonAllocator>;
using JsonValue = rapidjson::GenericValue<rapidjson::UTF8<>, JsonPoolAllocator>;
using JsonStringBuffer = rapidjson::GenericStringBuffer<rapidjson::UTF8<>, StackImportRapidJsonAllocator>;
using JsonWriter = rapidjson::PrettyWriter<JsonStringBuffer, rapidjson::UTF8<>, rapidjson::UTF8<>, StackImportRapidJsonAllocator>;

JsonValue json_string(const std::string& value, JsonPoolAllocator& allocator)
{
	JsonValue result;
	result.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
	return result;
}

JsonValue json_string(const char* value, JsonPoolAllocator& allocator)
{
	const char* safeValue = value ? value : "";
	JsonValue result;
	result.SetString(safeValue, static_cast<rapidjson::SizeType>(std::strlen(safeValue)), allocator);
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
	return write_json_file(path, text.data(), text.size());
}

bool write_json_document(const std::string& path, JsonDocument& document, StackImportRapidJsonAllocator& baseAllocator)
{
	JsonStringBuffer jsonBuffer(&baseAllocator);
	JsonWriter writer(jsonBuffer, &baseAllocator);
	document.Accept(writer);
	return write_json_file(path, jsonBuffer.GetString(), jsonBuffer.GetSize());
}

void append_format(std::string& text, const char* format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	const int written = vsnprintf(buffer, sizeof(buffer), format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
	va_end(args);
	if(written <= 0)
		return;
	if(static_cast<size_t>(written) < sizeof(buffer))
	{
		text.append(buffer, static_cast<size_t>(written));
		return;
	}

	std::vector<char> dynamicBuffer(static_cast<size_t>(written) + 1u);
	va_start(args, format);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	vsnprintf(dynamicBuffer.data(), dynamicBuffer.size(), format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
	va_end(args);
	text.append(dynamicBuffer.data(), static_cast<size_t>(written));
}

std::string lowercase_ascii(std::string text)
{
	for(char& ch : text)
	{
		if(ch >= 'A' && ch <= 'Z')
			ch = static_cast<char>(ch - 'A' + 'a');
	}
	return text;
}

std::string trim_ascii(const std::string& text)
{
	size_t start = 0;
	while(start < text.size() && static_cast<unsigned char>(text[start]) <= 0x20)
		start++;
	size_t end = text.size();
	while(end > start && static_cast<unsigned char>(text[end - 1]) <= 0x20)
		end--;
	return text.substr(start, end - start);
}

std::string first_word(const std::string& text)
{
	size_t start = 0;
	while(start < text.size() && !std::isalpha(static_cast<unsigned char>(text[start])) && text[start] != '_')
		start++;
	size_t end = start;
	while(end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
		end++;
	return text.substr(start, end - start);
}

bool starts_with_case_insensitive(const std::string& text, const char* prefix)
{
	const std::string lowerText = lowercase_ascii(text);
	const std::string lowerPrefix = lowercase_ascii(prefix);
	return lowerText.rfind(lowerPrefix, 0) == 0;
}

std::string fourcc_string(const char* bytes)
{
	std::string text;
	for(size_t i = 0; i < 4; i++)
	{
		if(bytes[i] == '\0')
			break;
		text.push_back(bytes[i]);
	}
	return text;
}

struct ClassicMacFileInfo
{
	std::string finderType;
	std::string creator;
	std::string classification;
	std::string dataForkSha256;
	std::string resourceForkSha256;
	uint64_t resourceForkBytes = 0;
};

std::string sha256_for_path(const std::string& path)
{
	std::vector<uint8_t> data;
	stackimport_file_handle file = stackimport_internal_open_file(path.c_str(), "rb");
	if(!file)
		return std::string();
	uint8_t buffer[65536];
	while(true)
	{
		const size_t count = stackimport_internal_read_file(file, buffer, sizeof(buffer));
		if(count == 0)
			break;
		data.insert(data.end(), buffer, buffer + count);
		if(count < sizeof(buffer))
			break;
	}
	stackimport_internal_close_file(file);
	return stackimport::RomDasm::compute_sha256(std::span<const uint8_t>(data.data(), data.size()));
}

ClassicMacFileInfo classic_mac_file_info(const std::string& path, uint64_t resourceForkBytes)
{
	ClassicMacFileInfo info;
	info.resourceForkBytes = resourceForkBytes;
	info.dataForkSha256 = sha256_for_path(path);
#if defined(__APPLE__)
	char finderInfo[32] = {};
	const ssize_t finderSize = getxattr(path.c_str(), "com.apple.FinderInfo", finderInfo, sizeof(finderInfo), 0, 0);
	if(finderSize >= 8)
	{
		info.finderType = fourcc_string(finderInfo);
		info.creator = fourcc_string(finderInfo + 4);
	}
	if(resourceForkBytes > 0)
		info.resourceForkSha256 = sha256_for_path(path + "/..namedfork/rsrc");
#else
	(void)resourceForkBytes;
#endif

	const bool hasStackDataFork = !info.dataForkSha256.empty();
	const bool hasHyperCardCode = resourceForkBytes > 0;
	if(info.finderType == "APPL" && hasStackDataFork && hasHyperCardCode)
		info.classification = "hypercard_standalone_application";
	else if(info.finderType == "APPL")
		info.classification = "classic_mac_application";
	else if(info.finderType == "STAK" || info.finderType == "MYag")
		info.classification = "hypercard_stack";
	else if(hasStackDataFork)
		info.classification = "hypercard_stack_data_fork";
	else
		info.classification = "unknown";
	return info;
}

std::vector<std::string> split_script_lines(const std::string& script)
{
	std::vector<std::string> lines;
	size_t start = 0;
	for(size_t i = 0; i <= script.size(); i++)
	{
		if(i == script.size() || script[i] == '\r' || script[i] == '\n')
		{
			lines.push_back(script.substr(start, i - start));
			if(i + 1 < script.size() && script[i] == '\r' && script[i + 1] == '\n')
				i++;
			start = i + 1;
		}
	}
	return lines;
}

std::vector<std::string> script_string_literals(const std::string& line)
{
	std::vector<std::string> literals;
	for(size_t i = 0; i < line.size(); i++)
	{
		if(line[i] != '"')
			continue;
		std::string literal;
		i++;
		for(; i < line.size(); i++)
		{
			if(line[i] == '"')
			{
				if(i + 1 < line.size() && line[i + 1] == '"')
				{
					literal.push_back('"');
					i++;
					continue;
				}
				break;
			}
			literal.push_back(line[i]);
		}
		literals.push_back(literal);
	}
	return literals;
}

std::string inferred_reference_kind(const std::string& lowerLine, const std::string& lowerLiteral)
{
	if(lowerLine.find("playqt") != std::string::npos || lowerLine.find("movie") != std::string::npos ||
		lowerLiteral.find(".mov") != std::string::npos || lowerLiteral.find(" moov") != std::string::npos ||
		lowerLiteral.find(" mov") != std::string::npos)
		return "movie";
	if(lowerLine.find("start using stack") != std::string::npos || lowerLine.find(" in stack ") != std::string::npos)
		return "stack";
	if(lowerLine.find("go card") != std::string::npos || lowerLine.find("go to card") != std::string::npos)
		return "card";
	if(lowerLine.find("sound") != std::string::npos)
		return "sound";
	if(lowerLine.find("htaddpict") != std::string::npos || lowerLine.find("htchangepict") != std::string::npos ||
		lowerLine.find("picture") != std::string::npos)
		return "picture";
	return std::string();
}

bool is_control_command(const std::string& lowerCommand)
{
	static const char* controls[] = {
		"if", "else", "repeat", "end", "put", "set", "get", "global", "local",
		"return", "exit", "pass", "then", "case", "switch", "answer", "ask",
		"wait", "send", "do", "hide", "show", "on", "function"
	};
	for(const char* control : controls)
	{
		if(lowerCommand == control)
			return true;
	}
	return false;
}

class PlatformStackReader final : public stackimport::IStackReader {
public:
	PlatformStackReader() : file_(nullptr), pos_(0) {}
	~PlatformStackReader() override { close(); }

	bool open(const char* path)
	{
		file_ = stackimport_internal_open_file(path, "rb");
		pos_ = 0;
		return file_ != nullptr;
	}

	void close()
	{
		if(file_)
		{
			stackimport_internal_close_file(file_);
			file_ = nullptr;
		}
		pos_ = 0;
	}

	auto read(uint8_t* dst, size_t len) -> size_t override
	{
		if(!file_)
			return 0;
		const size_t count = stackimport_internal_read_file(file_, dst, len);
		pos_ += count;
		return count;
	}

	auto seek(size_t) -> bool override { return false; }
	auto position() const -> size_t override { return pos_; }
	auto size() const -> size_t override { return 0; }
	auto bytes_read() const -> uint64_t { return static_cast<uint64_t>(pos_); }

private:
	stackimport_file_handle file_;
	size_t pos_;
};

JsonValue json_output_ref(
	const char* kind,
	int32_t id,
	const char* file,
	const char* sourceBlockType,
	int32_t sourceBlockId,
	JsonPoolAllocator& allocator)
{
	JsonValue media(rapidjson::kObjectType);
	media.AddMember("kind", json_string(kind, allocator), allocator);
	if(id >= 0)
		media.AddMember("id", id, allocator);
	media.AddMember("file", json_string(file, allocator), allocator);
	if(sourceBlockType)
	{
		media.AddMember("sourceBlockType", json_string(sourceBlockType, allocator), allocator);
		media.AddMember("sourceBlockId", sourceBlockId, allocator);
	}
	return media;
}

std::string mac_roman_string(const CBuf& data, size_t startOffs, size_t endOffs)
{
	std::string result;
	for( size_t x = startOffs; x < endOffs && data[static_cast<int>(x)] != 0; x++ )
	{
		unsigned char currCh = static_cast<unsigned char>(data[static_cast<int>(x)]);
		const unsigned char* utf8 = nullptr;
		if( currCh >= 128 )
			utf8 = sMacRomanToUTF8Table[ currCh -128 ];
		else if( currCh == 0x11 )
		{
			static unsigned char commandKey[4] = { 0xe2, 0x8c, 0x98, 0 };
			utf8 = commandKey;
		}
		else
		{
			char ascii[2] = { static_cast<char>(currCh), 0 };
			result.append( ascii );
			continue;
		}
		result.append( reinterpret_cast<const char*>(utf8) );
	}
	return result;
}

std::string mac_roman_string(const CBuf& data, size_t startOffs)
{
	return mac_roman_string(data, startOffs, MacRomanStringEnd(data, startOffs));
}

JsonValue json_rect(int16_t left, int16_t top, int16_t right, int16_t bottom, JsonPoolAllocator& allocator)
{
	JsonValue rect(rapidjson::kObjectType);
	rect.AddMember("left", left, allocator);
	rect.AddMember("top", top, allocator);
	rect.AddMember("right", right, allocator);
	rect.AddMember("bottom", bottom, allocator);
	return rect;
}

}

size_t	MacRomanStringEnd( const CBuf& data, size_t startOffs )
{
	size_t x = startOffs;
	for( ; x < data.size() && data[static_cast<int>(x)] != 0; x++ )
	{
	}
	return x;
}

size_t	EvenAlign( size_t offs )
{
	return offs + (offs % 2);
}

int16_t	ReadBEInt16( const CBuf& data, size_t offs )
{
	const auto* bytes = reinterpret_cast<const unsigned char*>(data.checked_buf( offs, sizeof(uint16_t) ));
	if( !bytes )
		return 0;
	return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) << 8 | static_cast<uint16_t>(bytes[1]));
}

int32_t	ReadBEInt32( const CBuf& data, size_t offs )
{
	const auto* bytes = reinterpret_cast<const unsigned char*>(data.checked_buf( offs, sizeof(uint32_t) ));
	if( !bytes )
		return 0;
	return static_cast<int32_t>(static_cast<uint32_t>(bytes[0]) << 24
		| static_cast<uint32_t>(bytes[1]) << 16
		| static_cast<uint32_t>(bytes[2]) << 8
		| static_cast<uint32_t>(bytes[3]));
}

uint16_t	ReadBEUInt16( const CBuf& data, size_t offs )
{
	const auto* bytes = reinterpret_cast<const unsigned char*>(data.checked_buf( offs, sizeof(uint16_t) ));
	if( !bytes )
		return 0;
	return static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) << 8 | static_cast<uint16_t>(bytes[1]));
}

uint32_t	ReadBEUInt32( const CBuf& data, size_t offs )
{
	const auto* bytes = reinterpret_cast<const unsigned char*>(data.checked_buf( offs, sizeof(uint32_t) ));
	if( !bytes )
		return 0;
	return static_cast<uint32_t>(static_cast<uint32_t>(bytes[0]) << 24
		| static_cast<uint32_t>(bytes[1]) << 16
		| static_cast<uint32_t>(bytes[2]) << 8
		| static_cast<uint32_t>(bytes[3]));
}

uint32_t	ReadBEUInt32Bytes( const char bytes[4] )
{
	const unsigned char* unsignedBytes = reinterpret_cast<const unsigned char*>(bytes);
	return static_cast<uint32_t>(static_cast<uint32_t>(unsignedBytes[0]) << 24
		| static_cast<uint32_t>(unsignedBytes[1]) << 16
		| static_cast<uint32_t>(unsignedBytes[2]) << 8
		| static_cast<uint32_t>(unsignedBytes[3]));
}

void	NumVersionToStr( const unsigned char numVersion[4], char outStr[16] )
{
	char	theCh = 'v';
	
	switch( numVersion[2] )
	{
		case 0x20:
			theCh = 'd';
			break;
		case 0x40:
			theCh = 'a';
			break;
		case 0x60:
			theCh = 'b';
			break;
		case 0x80:
			theCh = 'v';
			break;
	}
	
	// NumVersion is Binary-coded decimal, i.e. 0x10 is displayed as 10, not 16 decimal:
	if( numVersion[3] == 0 && (numVersion[1] & 0x0F) == 0 )	// N.N version
		snprintf( outStr, 16, "%x.%x", numVersion[0], (numVersion[1] >> 4) );
	else if( (numVersion[1] & 0x0F) == 0 )	// N.NxN version
		snprintf( outStr, 16, "%x.%x%c%d", numVersion[0], (numVersion[1] >> 4), theCh, numVersion[3] );
	else if( numVersion[3] == 0 )	// N.N.N version
		snprintf( outStr, 16, "%x.%x.%x", numVersion[0], (numVersion[1] >> 4), (numVersion[1] & 0x0F) );
	else	// N.N.NxN version
		snprintf( outStr, 16, "%x.%x.%x%c%d", numVersion[0], (numVersion[1] >> 4), (numVersion[1] & 0x0F), theCh, numVersion[3] );
}



CStackFile::CStackFile()
	: mDumpRawBlockData(false), mStatusMessages(true), mProgressMessages(true), mDecodeGraphics(true),
	mResourceOutput(nullptr),
	mListBlockID(-1), mFontTableBlockID(-1), mStyleTableBlockID(-1), mStackID(-1),
	mStackCardCount(0), mFirstCardID(0), mStackPatternCount(0), mUserLevel(0), mCardWidth(512), mCardHeight(342),
	mStackCantModify(false), mStackCantDelete(false), mStackPrivateAccess(false),
	mStackCantAbort(false), mStackCantPeek(false), mCardBlockSize(-1),
	mResourceForkStatus("not_checked"), mResourceForkBytes(0),
	mCurrentProgress(0), mMaxProgress(0)
{
	
}

std::string CStackFile::OutputPath( const char* fileName ) const
{
	std::string result = mBasePath;
	if( !result.empty() && result[result.size() -1] != '/' )
		result += '/';
	result += fileName;
	return result;
}

bool	CStackFile::WriteSourceManifest( uint64_t dataForkBytes, const char* streamStatus ) const
{
	StackImportRapidJsonAllocator baseAllocator;
	JsonPoolAllocator pool(1024, &baseAllocator);
	JsonDocument document(&pool, 1024, &baseAllocator);
	document.SetObject();
	JsonPoolAllocator& allocator = document.GetAllocator();
	document.AddMember("format", json_string("stackimport.sourceStackManifest", allocator), allocator);
	document.AddMember("generator", json_string("stackimport.core", allocator), allocator);
	document.AddMember("sourcePath", json_string(mFileName, allocator), allocator);
	document.AddMember("outputPackage", json_string(mBasePath, allocator), allocator);
	const ClassicMacFileInfo fileInfo = classic_mac_file_info(mSourcePath, mResourceForkBytes);
	JsonValue sourceFile(rapidjson::kObjectType);
	sourceFile.AddMember("path", json_string(mSourcePath, allocator), allocator);
	sourceFile.AddMember("name", json_string(mFileName, allocator), allocator);
	sourceFile.AddMember("classification", json_string(fileInfo.classification, allocator), allocator);
	sourceFile.AddMember("finderType", json_string(fileInfo.finderType, allocator), allocator);
	sourceFile.AddMember("creator", json_string(fileInfo.creator, allocator), allocator);
	sourceFile.AddMember("dataForkSha256", json_string(fileInfo.dataForkSha256, allocator), allocator);
	sourceFile.AddMember("resourceForkSha256", json_string(fileInfo.resourceForkSha256, allocator), allocator);
	sourceFile.AddMember("resourceForkBytes", JsonValue().SetUint64(fileInfo.resourceForkBytes), allocator);
	document.AddMember("sourceFile", sourceFile, allocator);

	JsonValue dataFork(rapidjson::kObjectType);
	dataFork.AddMember("bytes", JsonValue().SetUint64(dataForkBytes), allocator);
	dataFork.AddMember("sha256", json_string(fileInfo.dataForkSha256, allocator), allocator);
	dataFork.AddMember("streamStatus", json_string(streamStatus ? streamStatus : "ok", allocator), allocator);

	JsonValue blocks(rapidjson::kArrayType);
	std::map<std::string,int32_t> blockTypeCounts;
	for( const CSourceBlockSummary& block : mSourceBlocks )
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("type", json_string(block.type, allocator), allocator);
		item.AddMember("typeCode", block.typeCode, allocator);
		item.AddMember("id", block.id, allocator);
		item.AddMember("offset", JsonValue().SetUint64(block.offset), allocator);
		item.AddMember("size", block.size, allocator);
		item.AddMember("payloadOffset", JsonValue().SetUint64(block.payloadOffset), allocator);
		item.AddMember("payloadBytes", block.payloadBytes, allocator);
		item.AddMember("status", json_string(block.status, allocator), allocator);
		blocks.PushBack(item, allocator);
		blockTypeCounts[block.type]++;
	}
	dataFork.AddMember("blocks", blocks, allocator);

	JsonValue counts(rapidjson::kObjectType);
	for( const auto& count : blockTypeCounts )
	{
		JsonValue name = json_string(count.first, allocator);
		counts.AddMember(name, count.second, allocator);
	}
	dataFork.AddMember("blockTypeCounts", counts, allocator);

	JsonValue stakReferences(rapidjson::kObjectType);
	if( mStackID != -1 )
	{
		stakReferences.AddMember("cardCount", mStackCardCount, allocator);
		stakReferences.AddMember("firstCardId", mFirstCardID, allocator);
		stakReferences.AddMember("listBlockId", mListBlockID, allocator);
		stakReferences.AddMember("fontTableBlockId", mFontTableBlockID, allocator);
		stakReferences.AddMember("styleTableBlockId", mStyleTableBlockID, allocator);
		stakReferences.AddMember("cardHeight", mCardHeight, allocator);
		stakReferences.AddMember("cardWidth", mCardWidth, allocator);
		stakReferences.AddMember("patternCount", mStackPatternCount, allocator);
	}
	dataFork.AddMember("stakReferences", stakReferences, allocator);

	JsonValue missing(rapidjson::kArrayType);
	struct ReferencedBlock
	{
		const char* key;
		const char* type;
		int32_t id;
	};
	const ReferencedBlock references[] = {
		{ "listBlockId", "LIST", mListBlockID },
		{ "fontTableBlockId", "FTBL", mFontTableBlockID },
		{ "styleTableBlockId", "STBL", mStyleTableBlockID },
	};
	for( const ReferencedBlock& reference : references )
	{
		if( reference.id < 0 )
			continue;
		if( mBlockMap.find(CStackBlockIdentifier(reference.type, reference.id)) != mBlockMap.end() )
			continue;
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("type", json_string(reference.type, allocator), allocator);
		item.AddMember("id", reference.id, allocator);
		std::string referencedBy = "STAK.";
		referencedBy += reference.key;
		item.AddMember("referencedBy", json_string(referencedBy, allocator), allocator);
		missing.PushBack(item, allocator);
	}
	dataFork.AddMember("missingReferencedBlocks", missing, allocator);
	document.AddMember("dataFork", dataFork, allocator);

	JsonValue resourceFork(rapidjson::kObjectType);
	resourceFork.AddMember("status", json_string(mResourceForkStatus, allocator), allocator);
	resourceFork.AddMember("bytes", JsonValue().SetUint64(mResourceForkBytes), allocator);
	resourceFork.AddMember("sha256", json_string(fileInfo.resourceForkSha256, allocator), allocator);
	JsonValue resources(rapidjson::kArrayType);
	std::map<std::string,int32_t> resourceTypeCounts;
	for( const CResourceSummary& resource : mResourceSummaries )
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("type", json_string(resource.type, allocator), allocator);
		item.AddMember("typeCode", resource.typeCode, allocator);
		item.AddMember("id", resource.id, allocator);
		item.AddMember("flags", resource.flags, allocator);
		item.AddMember("name", json_string(resource.name, allocator), allocator);
		item.AddMember("bytes", JsonValue().SetUint64(resource.bytes), allocator);
		item.AddMember("status", json_string(resource.status, allocator), allocator);
		item.AddMember("architecture", json_string(resource.architecture, allocator), allocator);
		if(!resource.disassemblyFile.empty())
			item.AddMember("disassemblyFile", json_string(resource.disassemblyFile, allocator), allocator);
		if(!resource.outputFile.empty())
			item.AddMember("outputFile", json_string(resource.outputFile, allocator), allocator);
		if(!resource.outputArtifacts.empty())
		{
			JsonValue artifacts(rapidjson::kArrayType);
			for(const CResourceOutputArtifact& artifact : resource.outputArtifacts)
			{
				JsonValue artifactItem(rapidjson::kObjectType);
				artifactItem.AddMember("path", json_string(artifact.path, allocator), allocator);
				artifactItem.AddMember("format", json_string(artifact.format, allocator), allocator);
				artifactItem.AddMember("mediaType", json_string(artifact.mediaType, allocator), allocator);
				artifactItem.AddMember("description", json_string(artifact.description, allocator), allocator);
				artifactItem.AddMember("variantIndex", artifact.variantIndex, allocator);
				artifacts.PushBack(artifactItem, allocator);
			}
			item.AddMember("outputArtifacts", artifacts, allocator);
		}
		resources.PushBack(item, allocator);
		resourceTypeCounts[resource.type]++;
	}
	resourceFork.AddMember("resources", resources, allocator);

	JsonValue resourceCounts(rapidjson::kObjectType);
	for( const auto& count : resourceTypeCounts )
	{
		JsonValue name = json_string(count.first, allocator);
		resourceCounts.AddMember(name, count.second, allocator);
	}
	resourceFork.AddMember("resourceTypeCounts", resourceCounts, allocator);
	document.AddMember("resourceFork", resourceFork, allocator);

	if( !write_json_document(OutputPath("source-manifest.json"), document, baseAllocator) )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't create source manifest JSON at '%s'\n", OutputPath("source-manifest.json").c_str() );
		return false;
	}
	return true;
}


bool	CStackFile::LoadStackBlock( int32_t stackID, CBuf& blockData )
{
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'STAK' #-1 (%lu bytes)\n", blockData.size() );

	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "STAK_%d.data", stackID );
		blockData.tofile( OutputPath(sfn) );
	}
	
	mStackID = stackID;
	if( !blockData.hasdata( 0, 68 ) )
		stackimport_emit_diagnosticf( "Warning: 'STAK' #%d is too short for the fixed header (%lu bytes); missing fields will keep defaults.\n", stackID, blockData.size() );
	if( blockData.hasdata( 32, sizeof(int32_t) ) )
		mStackCardCount = ReadBEInt32(blockData, 32);
	if( blockData.hasdata( 36, sizeof(int32_t) ) )
		mFirstCardID = ReadBEInt32(blockData, 36);
	if( blockData.hasdata( 40, sizeof(int32_t) ) )
		mListBlockID = ReadBEInt32(blockData, 40);
	if( blockData.hasdata( 60, sizeof(int16_t) ) )
		mUserLevel = ReadBEInt16(blockData, 60);
	if( blockData.hasdata( 64, sizeof(int16_t) ) )
	{
		int16_t	flags = ReadBEInt16(blockData, 64);
		mStackCantModify = (flags & (1 << 15)) != 0;
		mStackCantDelete = (flags & (1 << 14)) != 0;
		mStackPrivateAccess = (flags & (1 << 13)) != 0;
		mStackCantAbort = (flags & (1 << 11)) != 0;
		mStackCantPeek = (flags & (1 << 10)) != 0;
	}
	char		versStr[16] = { 0 };
	if( blockData.hasdata( 84, 4 ) )
	{
		NumVersionToStr( reinterpret_cast<const unsigned char*>(blockData.buf( 84, 4 )), versStr );
		mCreatedByVersion = "HyperCard ";
		mCreatedByVersion += versStr;
	}
	if( blockData.hasdata( 88, 4 ) )
	{
		NumVersionToStr( reinterpret_cast<const unsigned char*>(blockData.buf( 88, 4 )), versStr );
		mLastCompactedVersion = "HyperCard ";
		mLastCompactedVersion += versStr;
	}
	if( blockData.hasdata( 92, 4 ) )
	{
		NumVersionToStr( reinterpret_cast<const unsigned char*>(blockData.buf( 92, 4 )), versStr );
		mLastEditedVersion = "HyperCard ";
		mLastEditedVersion += versStr;
	}
	if( blockData.hasdata( 96, 4 ) )
	{
		NumVersionToStr( reinterpret_cast<const unsigned char*>(blockData.buf( 96, 4 )), versStr );
		mFirstEditedVersion = "HyperCard ";
		mFirstEditedVersion += versStr;
	}
	if( blockData.hasdata( 420, sizeof(int32_t) ) )
		mFontTableBlockID = ReadBEInt32(blockData, 420);
	if( blockData.hasdata( 424, sizeof(int32_t) ) )
		mStyleTableBlockID = ReadBEInt32(blockData, 424);
	if( blockData.hasdata( 428, sizeof(int16_t) ) )
		mCardHeight = ReadBEInt16(blockData, 428);
	if( mCardHeight == 0 )
		mCardHeight = 342;
	if( blockData.hasdata( 430, sizeof(int16_t) ) )
		mCardWidth = ReadBEInt16(blockData, 430);
	if( mCardWidth == 0 )
		mCardWidth = 512;

	char			pattern[8] = { 0 };
	size_t			offs = 692;
	if( blockData.hasdata( offs, 40 * 8 ) )
	{
		for( int n = 0; n < 40; n++ )
		{
			memmove( pattern, blockData.buf( offs, 8 ), 8 );
			char		fname[256] = { 0 };
			snprintf( fname, sizeof(fname), "PAT_%u.pbm", static_cast<unsigned>(n +1) );
			picture		thePicture( 8, 8, 1, false );
			thePicture.memcopyin( pattern, 0, 8 );
			thePicture.writebitmaptopbm( OutputPath(fname).c_str() );
			offs += 8;
		}
		mStackPatternCount = 40;
	}
	else
		stackimport_emit_diagnosticf( "Warning: 'STAK' #%d has no complete pattern table; pattern files were not exported.\n", stackID );
	
	if( blockData.hasdata( 1524, 1 ) )
		mStackScript = mac_roman_string( blockData, 1524 );
	if( !mStackScript.empty() )
	{
		mScriptSummaries.push_back(CScriptSummary{
			"stack",
			stackID,
			"stack_-1.json",
			mFileName,
			std::string(),
			-1,
			std::string(),
			-1,
			mStackScript
		});
	}
	
	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
	
	return true;
}


bool	CStackFile::LoadStyleTable( int32_t blockID, CBuf& blockData )
{
	int32_t	vBlockSize = static_cast<int32_t>(blockData.size());
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'STBL' #%d %X (%d bytes)\n", blockID, blockID, vBlockSize );
	
	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "STBL_%d.data", blockID );
		blockData.tofile( OutputPath(sfn) );
	}
	
	std::vector<CStyleEntry>	styles;

	if( blockData.size() < 12 )
	{
		stackimport_emit_diagnosticf( "Warning: 'STBL' #%d is too short (%lu bytes); using an empty style table.\n", blockID, blockData.size() );
		char vFileName[256] = { 0 };
		snprintf( vFileName, sizeof(vFileName), "stylesheet_%d.css", blockID );
		mStyleSheetName = vFileName;
		std::string stylesheet;
		append_format( stylesheet, "/* Missing or empty HyperCard style table %d. */\n", blockID );
		write_text_file( OutputPath(vFileName), stylesheet );
		if( mProgressMessages )
			stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
		return true;
	}
	
	size_t		currOffs = 4;
	int32_t		styleCount = ReadBEInt32(blockData, currOffs);
	currOffs += 4;

	std::string	vLayerFilePath = mBasePath;
	char		vFileName[256] = { 0 };
	snprintf( vFileName, 255, "stylesheet_%d.css", blockID );
	mStyleSheetName = vFileName;
	vLayerFilePath.append( 1, '/' );
	vLayerFilePath.append( vFileName );
	std::string stylesheet;
	
	currOffs += 2;
	int16_t	nextStyleID = ReadBEInt16(blockData, currOffs);
	(void)nextStyleID;
	currOffs += 2;
	currOffs += 2;
	
	for( int s = 0; s < styleCount; s++ )
	{
		CStyleEntry		style;
		
		style.mStyleID = ReadBEInt16(blockData, currOffs);
		append_format( stylesheet, "\t\t.style%d\n\t\t{\n", style.mStyleID );
		currOffs += 2;
		currOffs += 8;
		
		style.mFontID = ReadBEInt16(blockData, currOffs);
		if( style.mFontID != -1 )
		{
			style.mFontName = mFontTable[style.mFontID];
			append_format( stylesheet, "\t\t\tfont-family: \"%s\";\n", style.mFontName.c_str() );
		}
		currOffs += 2;
		
		int16_t	textStyleFlags = ReadBEInt16(blockData, currOffs);
		currOffs += 2;
		
		if( textStyleFlags == 0 )
			append_format( stylesheet, "\t\t\tfont-style: normal;\n" );
		else if( textStyleFlags != -1 )	// -1 means use field style.
		{
			if( textStyleFlags & (1 << 15) )
			{
				append_format( stylesheet, "\t\t\t/* group text style */\n" );
				style.mGroup = true;
			}
			if( textStyleFlags & (1 << 14) )
			{
				append_format( stylesheet, "\t\t\tletter-spacing: 0.1em;\n" );
				style.mExtend = true;
			}
			if( textStyleFlags & (1 << 13) )
			{
				append_format( stylesheet, "\t\t\tletter-spacing: -0.1em;\n" );
				style.mCondense = true;
			}
			if( textStyleFlags & (1 << 12) )
			{
				append_format( stylesheet, "\t\t\ttext-shadow: 1px 1px #000000;\n" );
				style.mShadow = true;
			}
			if( textStyleFlags & (1 << 11) )
			{
				append_format( stylesheet, "\t\t\tcolor: white; -webkit-text-stroke-width: 1pt; -webkit-text-stroke-color: #000;\n" );
				style.mOutline = true;
			}
			if( textStyleFlags & (1 << 10) )
			{
				append_format( stylesheet, "\t\t\ttext-decoration: underline;\n" );
				style.mUnderline = true;
			}
			if( textStyleFlags & (1 << 9) )
			{
				append_format( stylesheet, "\t\t\tfont-style: italic;\n" );
				style.mItalic = true;
			}
			if( textStyleFlags & (1 << 8) )
			{
				append_format( stylesheet, "\t\t\tfont-weight: bold;\n" );
				style.mBold = true;
			}
		}
		int16_t	fontSize = ReadBEInt16(blockData, currOffs);
		if( fontSize != -1 )
		{
			append_format( stylesheet, "\t\t\tfont-size: %dpt;\n", fontSize );
			style.mFontSize = fontSize;
		}
		currOffs += 2;
		currOffs += 8;	// 2 bytes padding?
		
		append_format( stylesheet, "\t\t}\n" );
		
		mStyles[style.mStyleID] = style;
	}
	
	if( !write_text_file( vLayerFilePath, stylesheet ) )
		stackimport_emit_diagnosticf( "Error: Couldn't create stylesheet '%s'.\n", vLayerFilePath.c_str() );
	
	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
	
	return true;
}

bool	CStackFile::LoadFontTable( int32_t blockID, CBuf& blockData )
{
	uint32_t	vBlockSize = static_cast<uint32_t>(blockData.size());
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'FTBL' #%d %X (%d bytes)\n", blockID, blockID, vBlockSize );

	if( blockData.size() < 8 )
	{
		stackimport_emit_diagnosticf( "Warning: 'FTBL' #%d is too short (%lu bytes); using an empty font table.\n", blockID, blockData.size() );
		if( mProgressMessages )
			stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
		return true;
	}

	int16_t	numFonts = ReadBEInt16(blockData, 6);
	size_t	currOffsIntoData = 8;
	currOffsIntoData += 4;	// Reserved?
	for( int n = 0; n < numFonts; n++ )
	{
		std::string		fontName;
		
		int16_t	fontID = ReadBEInt16(blockData, currOffsIntoData);
		
		size_t x = 0;
		size_t startOffs = currOffsIntoData +2;
		fontName = mac_roman_string( blockData, startOffs );
		x = MacRomanStringEnd( blockData, startOffs );
		
		mFontTable[fontID] = fontName;
	
		currOffsIntoData = x +1;
		currOffsIntoData += currOffsIntoData %2;	// Align on even byte.
		
	}
	
	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
	
	return true;
}

bool	CStackFile::LoadMasterBlock( int32_t blockID, CBuf& blockData )
{
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'MAST' #%d (%lu bytes)\n", blockID, blockData.size() );

	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "MAST_%d.data", blockID );
		blockData.tofile( OutputPath(sfn) );
	}

	std::string	filePath = mBasePath;
	filePath.append( "/master_-1.json" );
	StackImportRapidJsonAllocator baseAllocator;
	JsonPoolAllocator pool(1024, &baseAllocator);
	JsonDocument document(&pool, 1024, &baseAllocator);
	document.SetObject();
	JsonPoolAllocator& allocator = document.GetAllocator();
	document.AddMember("format", json_string("stackimport.master", allocator), allocator);
	document.AddMember("id", blockID, allocator);
	JsonValue references(rapidjson::kArrayType);

	for( size_t currOffs = 20; blockData.hasdata( currOffs, sizeof(uint32_t)); currOffs += sizeof(uint32_t) )
	{
		uint32_t	entry = ReadBEUInt32(blockData, currOffs);
		if( entry == 0 )
			continue;
		uint32_t	fileOffset = (entry >> 8) << 5;
		uint8_t		idLowByte = entry & 0xff;
		JsonValue reference(rapidjson::kObjectType);
		reference.AddMember("fileOffset", fileOffset, allocator);
		reference.AddMember("idLowByte", idLowByte, allocator);
		reference.AddMember("raw", entry, allocator);
		references.PushBack(reference, allocator);
	}

	document.AddMember("references", references, allocator);
	if( !write_json_document(filePath, document, baseAllocator) )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't create master JSON at '%s'\n", filePath.c_str() );
		return false;
	}

	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );

	return true;
}

bool	CStackFile::LoadPrintBlock( int32_t blockID, CBuf& blockData )
{
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'PRNT' #%d (%lu bytes)\n", blockID, blockData.size() );

	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "PRNT_%d.data", blockID );
		blockData.tofile( OutputPath(sfn) );
	}

	std::string	filePath = mBasePath;
	filePath.append( "/printsettings.json" );
	StackImportRapidJsonAllocator baseAllocator;
	JsonPoolAllocator pool(1024, &baseAllocator);
	JsonDocument document(&pool, 1024, &baseAllocator);
	document.SetObject();
	JsonPoolAllocator& allocator = document.GetAllocator();
	document.AddMember("format", json_string("stackimport.printSettings", allocator), allocator);
	document.AddMember("id", blockID, allocator);

	if( blockData.hasdata( 0x24, sizeof(int16_t) ) )
		document.AddMember("pageSetupId", ReadBEInt16( blockData, 0x24 ), allocator);

	if( blockData.hasdata( 0x128, sizeof(int16_t) ) )
	{
		int16_t	templateCount = ReadBEInt16( blockData, 0x128 );
		document.AddMember("reportTemplateCount", templateCount, allocator);
		JsonValue templates(rapidjson::kArrayType);
		size_t	currOffs = 0x12a;
		for( int n = 0; n < templateCount && blockData.hasdata( currOffs, 36 ); n++, currOffs += 36 )
		{
			int32_t	templateID = ReadBEInt32( blockData, currOffs );
			uint8_t	nameLen = static_cast<uint8_t>(blockData[static_cast<int>(currOffs +4)]);
			if( nameLen > 31 )
				nameLen = 31;
			JsonValue item(rapidjson::kObjectType);
			item.AddMember("id", templateID, allocator);
			item.AddMember("name", json_string(mac_roman_string(blockData, currOffs +5, currOffs +5 +nameLen), allocator), allocator);
			templates.PushBack(item, allocator);
		}
		document.AddMember("reportTemplates", templates, allocator);
	}

	if( !write_json_document(filePath, document, baseAllocator) )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't create print settings JSON at '%s'\n", filePath.c_str() );
		return false;
	}

	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );

	return true;
}

bool	CStackFile::LoadPageSetupBlock( int32_t blockID, CBuf& blockData )
{
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'PRST' #%d (%lu bytes)\n", blockID, blockData.size() );

	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "PRST_%d.data", blockID );
		blockData.tofile( OutputPath(sfn) );
	}

	char		fileName[256] = { 0 };
	snprintf( fileName, sizeof(fileName), "pagesetup_%d.json", blockID );
	std::string	filePath = mBasePath;
	filePath.append( "/" );
	filePath.append( fileName );
	StackImportRapidJsonAllocator baseAllocator;
	JsonPoolAllocator pool(1024, &baseAllocator);
	JsonDocument document(&pool, 1024, &baseAllocator);
	document.SetObject();
	JsonPoolAllocator& allocator = document.GetAllocator();
	document.AddMember("format", json_string("stackimport.pageSetup", allocator), allocator);
	document.AddMember("id", blockID, allocator);

	size_t	currOffs = 4;
	const char*	shortNames[] = {
		"printerDriverVersion", "iDev", "vertResol", "horizResol",
		"pageTop", "pageLeft", "pageBottom", "pageRight",
		"paperTop", "paperLeft", "paperBottom", "paperRight",
		"printerDeviceNumber", "pageV", "pageH"
	};
	for( size_t n = 0; n < sizeof(shortNames) / sizeof(shortNames[0]) && blockData.hasdata( currOffs, 2 ); n++, currOffs += 2 )
		document.AddMember(rapidjson::StringRef(shortNames[n]), ReadBEInt16( blockData, currOffs ), allocator);

	if( blockData.hasdata( currOffs, 2 ) )
	{
		document.AddMember("port", static_cast<uint8_t>(blockData[static_cast<int>(currOffs++)]), allocator);
		document.AddMember("feedType", static_cast<uint8_t>(blockData[static_cast<int>(currOffs++)]), allocator);
	}

	const char*	printRecordNames[] = {
		"iDev2", "vertResol2", "horizResol2", "pageTop2",
		"pageLeft2", "pageBottom2", "pageRight2"
	};
	for( size_t n = 0; n < sizeof(printRecordNames) / sizeof(printRecordNames[0]) && blockData.hasdata( currOffs, 2 ); n++, currOffs += 2 )
		document.AddMember(rapidjson::StringRef(printRecordNames[n]), ReadBEInt16( blockData, currOffs ), allocator);

	currOffs += 16;	// Reserved.
	if( blockData.hasdata( currOffs, 8 ) )
	{
		document.AddMember("firstPage", ReadBEInt16( blockData, currOffs ), allocator);
		currOffs += 2;
		document.AddMember("lastPage", ReadBEInt16( blockData, currOffs ), allocator);
		currOffs += 2;
		document.AddMember("numCopies", ReadBEInt16( blockData, currOffs ), allocator);
		currOffs += 2;
		uint8_t	printingMethod = static_cast<uint8_t>(blockData[static_cast<int>(currOffs++)]);
		document.AddMember("printingMethod", json_string(printingMethod == 0 ? "draft" : (printingMethod == 1 ? "deferred" : "unknown"), allocator), allocator);
		currOffs++;	// Reserved.
	}

	if( !write_json_document(filePath, document, baseAllocator) )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't create page setup JSON at '%s'\n", filePath.c_str() );
		return false;
	}

	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );

	return true;
}

bool	CStackFile::LoadReportTemplateBlock( int32_t blockID, CBuf& blockData )
{
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'PRFT' #%d (%lu bytes)\n", blockID, blockData.size() );

	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "PRFT_%d.data", blockID );
		blockData.tofile( OutputPath(sfn) );
	}

	char		fileName[256] = { 0 };
	snprintf( fileName, sizeof(fileName), "reporttemplate_%d.json", blockID );
	std::string	filePath = mBasePath;
	filePath.append( "/" );
	filePath.append( fileName );
	StackImportRapidJsonAllocator baseAllocator;
	JsonPoolAllocator pool(1024, &baseAllocator);
	JsonDocument document(&pool, 1024, &baseAllocator);
	document.SetObject();
	JsonPoolAllocator& allocator = document.GetAllocator();
	document.AddMember("format", json_string("stackimport.reportTemplate", allocator), allocator);
	document.AddMember("id", blockID, allocator);

	if( blockData.hasdata( 24, 1 ) )
	{
		const char* units = "unknown";
		switch( static_cast<uint8_t>(blockData[4]) )
		{
			case 0: units = "centimeters"; break;
			case 1: units = "millimeters"; break;
			case 2: units = "inches"; break;
			case 3: units = "points"; break;
		}
		document.AddMember("units", json_string(units, allocator), allocator);
		document.AddMember("margins", json_rect(ReadBEInt16( blockData, 8 ), ReadBEInt16( blockData, 6 ), ReadBEInt16( blockData, 12 ), ReadBEInt16( blockData, 10 ), allocator), allocator);
		JsonValue spacing(rapidjson::kObjectType);
		spacing.AddMember("height", ReadBEInt16( blockData, 14 ), allocator);
		spacing.AddMember("width", ReadBEInt16( blockData, 16 ), allocator);
		document.AddMember("spacing", spacing, allocator);
		JsonValue cellSize(rapidjson::kObjectType);
		cellSize.AddMember("height", ReadBEInt16( blockData, 18 ), allocator);
		cellSize.AddMember("width", ReadBEInt16( blockData, 20 ), allocator);
		document.AddMember("cellSize", cellSize, allocator);
		int16_t	flags = ReadBEInt16( blockData, 22 );
		document.AddMember("leftToRight", (flags & (1 << 8)) != 0, allocator);
		document.AddMember("dynamicHeight", (flags & (1 << 0)) != 0, allocator);
		uint8_t	headerLen = static_cast<uint8_t>(blockData[24]);
		document.AddMember("header", json_string(mac_roman_string(blockData, 25, 25 +headerLen), allocator), allocator);
	}

	if( blockData.hasdata( 0x118, sizeof(int16_t) ) )
	{
		int16_t	itemCount = ReadBEInt16(blockData, 0x118);
		document.AddMember("itemCount", itemCount, allocator);
		JsonValue items(rapidjson::kArrayType);
		size_t	currOffs = 0x11a;
		for( int n = 0; n < itemCount && blockData.hasdata( currOffs, 22 ); n++ )
		{
			int16_t	itemSize = ReadBEInt16( blockData, currOffs );
			if( itemSize <= 0 || !blockData.hasdata( currOffs, static_cast<size_t>(itemSize) ) )
				break;

			JsonValue item(rapidjson::kObjectType);
			item.AddMember("rect", json_rect(ReadBEInt16( blockData, currOffs +4 ), ReadBEInt16( blockData, currOffs +2 ), ReadBEInt16( blockData, currOffs +8 ), ReadBEInt16( blockData, currOffs +6 ), allocator), allocator);
			item.AddMember("columns", ReadBEInt16( blockData, currOffs +10 ), allocator);
			int16_t	flags = ReadBEInt16( blockData, currOffs +12 );
			item.AddMember("changeHeight", (flags & (1 << 13)) != 0, allocator);
			item.AddMember("changeStyle", (flags & (1 << 12)) != 0, allocator);
			item.AddMember("changeSize", (flags & (1 << 11)) != 0, allocator);
			item.AddMember("changeFont", (flags & (1 << 10)) != 0, allocator);
			item.AddMember("invert", (flags & (1 << 4)) != 0, allocator);
			JsonValue frame(rapidjson::kObjectType);
			frame.AddMember("top", (flags & (1 << 0)) != 0, allocator);
			frame.AddMember("left", (flags & (1 << 1)) != 0, allocator);
			frame.AddMember("bottom", (flags & (1 << 2)) != 0, allocator);
			frame.AddMember("right", (flags & (1 << 3)) != 0, allocator);
			item.AddMember("frame", frame, allocator);
			item.AddMember("textSize", ReadBEInt16( blockData, currOffs +14 ), allocator);
			item.AddMember("textHeight", ReadBEInt16( blockData, currOffs +16 ), allocator);
			item.AddMember("textStyle", static_cast<uint8_t>(blockData[static_cast<int>(currOffs +18)]), allocator);
			int16_t	textAlign = ReadBEInt16( blockData, currOffs +20 );
			item.AddMember("textAlign", textAlign, allocator);

			size_t	contentStart = currOffs +22;
			size_t	contentEnd = MacRomanStringEnd( blockData, contentStart );
			item.AddMember("contents", json_string(mac_roman_string(blockData, contentStart, contentEnd), allocator), allocator);

			size_t	fontStart = contentEnd +1;
			size_t	fontEnd = MacRomanStringEnd( blockData, fontStart );
			item.AddMember("font", json_string(mac_roman_string(blockData, fontStart, fontEnd), allocator), allocator);
			items.PushBack(item, allocator);

			currOffs += static_cast<size_t>(itemSize);
			currOffs = EvenAlign( currOffs );
		}
		document.AddMember("items", items, allocator);
	}

	if( !write_json_document(filePath, document, baseAllocator) )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't create report template JSON at '%s'\n", filePath.c_str() );
		return false;
	}

	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );

	return true;
}


struct	CStyleRun { int16_t startOffset; int16_t styleID; };


bool	CStackFile::LoadLayerBlock( const char* vBlockType, int32_t blockID, CBuf& blockData, uint8_t inFlags )
{
	int32_t		vBlockSize = static_cast<int32_t>(blockData.size());
	const bool	isCard = strcmp( "CARD", vBlockType ) == 0;
	char		vFileName[256] = { 0 };
	if( !isCard )
		snprintf( vFileName, 255, "background_%d.json", blockID );
	else
		snprintf( vFileName, 255, "card_%d.json", blockID );
	std::string	vLayerFilePath = OutputPath( vFileName );
	
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing '%4s' #%d %X (%d bytes)\n", vBlockType, blockID, blockID, vBlockSize );
	
	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "%s_%d.data", vBlockType, blockID );
		blockData.tofile( OutputPath(sfn) );
	}

	StackImportRapidJsonAllocator baseAllocator;
	JsonPoolAllocator pool(4096, &baseAllocator);
	JsonDocument document(&pool, 4096, &baseAllocator);
	document.SetObject();
	JsonPoolAllocator& allocator = document.GetAllocator();
	document.AddMember("format", json_string(isCard ? "stackimport.card" : "stackimport.background", allocator), allocator);
	document.AddMember("id", blockID, allocator);
	document.AddMember("blockType", json_string(vBlockType, allocator), allocator);
	document.AddMember("blockSize", vBlockSize, allocator);
	document.AddMember("file", json_string(vFileName, allocator), allocator);
	
	size_t	currOffsIntoData = 0;
	int32_t	unknownFiller = ReadBEInt32(blockData, currOffsIntoData);
	currOffsIntoData += 4;
	document.AddMember("filler1", unknownFiller, allocator);
	int32_t	bitmapID = ReadBEInt32(blockData, currOffsIntoData);
	currOffsIntoData += 4;
	if( bitmapID != 0 )
	{
		char bitmapFile[256] = { 0 };
		snprintf( bitmapFile, sizeof(bitmapFile), "BMAP_%u.pbm", bitmapID );
		document.AddMember("bitmapId", bitmapID, allocator);
		document.AddMember("bitmap", json_string(bitmapFile, allocator), allocator);
	}
	int16_t	flags = ReadBEInt16(blockData, currOffsIntoData);
	currOffsIntoData += 2;
	document.AddMember("cantDelete", (flags & (1 << 14)) != 0, allocator);
	document.AddMember("showPict", (flags & (1 << 13)) == 0, allocator);
	document.AddMember("dontSearch", (flags & (1 << 11)) != 0, allocator);
	currOffsIntoData += 14;
	int32_t	owner = -1;
	if( isCard )
	{
		owner = ReadBEInt32(blockData, currOffsIntoData);
		document.AddMember("owner", owner, allocator);
		currOffsIntoData += 4;
		document.AddMember("marked", (inFlags & 16) != 0, allocator);
	}
	if( !mStyleSheetName.empty() )
		document.AddMember("styleSheet", json_string(mStyleSheetName, allocator), allocator);

	int16_t	numParts = ReadBEInt16(blockData, currOffsIntoData);
	currOffsIntoData += 2;
	currOffsIntoData += 6;
	int16_t	numContents = ReadBEInt16(blockData, currOffsIntoData);
	currOffsIntoData += 2;
	currOffsIntoData += 4;
	document.AddMember("partCount", numParts, allocator);
	document.AddMember("contentCount", numContents, allocator);
	std::vector<int32_t>	buttonIDs;
	JsonValue parts(rapidjson::kArrayType);
	if( numParts < 0 )
	{
		stackimport_emit_diagnosticf( "Warning: '%4s' #%d has invalid part count %d; treating as 0.\n", vBlockType, blockID, numParts );
		numParts = 0;
	}
	for( int n = 0; n < numParts; n++ )
	{
		if( !blockData.hasdata(currOffsIntoData, 30) )
		{
			stackimport_emit_diagnosticf( "Warning: Premature end of '%4s' #%d part records.\n", vBlockType, blockID );
			break;
		}
		int16_t	partLength = ReadBEInt16(blockData, currOffsIntoData);
		if( partLength < 30 || !blockData.hasdata(currOffsIntoData, static_cast<size_t>(partLength)) )
		{
			stackimport_emit_diagnosticf( "Warning: '%4s' #%d has invalid part length %d at part index %d.\n", vBlockType, blockID, partLength, n );
			break;
		}
		size_t	partRecordLength = static_cast<size_t>(partLength);
		int16_t	partID = ReadBEInt16(blockData, currOffsIntoData +2);
		int16_t	flagsAndType = ReadBEInt16(blockData, currOffsIntoData +4);
		int16_t	partType = flagsAndType >> 8;
		bool	isButton = partType == 1;
		if( isButton && !isCard )
			buttonIDs.push_back( partID );
		JsonValue part(rapidjson::kObjectType);
		part.AddMember("id", partID, allocator);
		part.AddMember("type", json_string(isButton ? "button" : "field", allocator), allocator);
		part.AddMember("visible", (flagsAndType & (1 << 7)) == 0, allocator);
		if( !isButton )
		{
			part.AddMember("dontWrap", (flagsAndType & (1 << 5)) != 0, allocator);
			part.AddMember("dontSearch", (flagsAndType & (1 << 4)) != 0, allocator);
			part.AddMember("sharedText", (flagsAndType & (1 << 3)) != 0, allocator);
			part.AddMember("fixedLineHeight", (flagsAndType & (1 << 2)) == 0, allocator);
			part.AddMember("autoTab", (flagsAndType & (1 << 1)) != 0, allocator);
			part.AddMember("lockText", (flagsAndType & (1 << 0)) != 0, allocator);
		}
		else
		{
			part.AddMember("enabled", (flagsAndType & (1 << 0)) == 0, allocator);
			part.AddMember("reserved5", (flagsAndType & (1 << 5)) >> 5, allocator);
			part.AddMember("reserved4", (flagsAndType & (1 << 4)) >> 4, allocator);
			part.AddMember("reserved3", (flagsAndType & (1 << 3)) >> 3, allocator);
			part.AddMember("reserved2", (flagsAndType & (1 << 2)) >> 2, allocator);
			part.AddMember("reserved1", (flagsAndType & (1 << 1)) >> 1, allocator);
		}
		part.AddMember("rect", json_rect(
			ReadBEInt16(blockData, currOffsIntoData +8),
			ReadBEInt16(blockData, currOffsIntoData +6),
			ReadBEInt16(blockData, currOffsIntoData +12),
			ReadBEInt16(blockData, currOffsIntoData +10), allocator), allocator);
		int16_t	moreFlags = ReadBEInt16(blockData, currOffsIntoData +14);
		int8_t	styleFromLowNibble = moreFlags & 15;
		const char*	styleStr = "unknown";
		if( isButton )
		{
			switch( styleFromLowNibble )
			{
				case 0: styleStr = "transparent"; break;
				case 1: styleStr = "opaque"; break;
				case 2: styleStr = "rectangle"; break;
				case 3: styleStr = "roundrect"; break;
				case 4: styleStr = "shadow"; break;
				case 5: styleStr = "checkbox"; break;
				case 6: styleStr = "radiobutton"; break;
				case 8: styleStr = "standard"; break;
				case 9: styleStr = "default"; break;
				case 10: styleStr = "oval"; break;
				case 11: styleStr = "popup"; break;
			}
		}
		else
		{
			switch( styleFromLowNibble )
			{
				case 0: styleStr = "transparent"; break;
				case 1: styleStr = "opaque"; break;
				case 2: styleStr = "rectangle"; break;
				case 4: styleStr = "shadow"; break;
				case 7: styleStr = "scrolling"; break;
			}
		}
		part.AddMember("style", json_string(styleStr, allocator), allocator);
		moreFlags = moreFlags >> 8;
		int8_t	family = moreFlags & 15;
		if( isButton )
		{
			part.AddMember("showName", (moreFlags & (1 << 7)) != 0, allocator);
			part.AddMember("highlight", (moreFlags & (1 << 6)) != 0, allocator);
			part.AddMember("autoHighlight", (moreFlags & (1 << 5) || family != 0), allocator);
			part.AddMember("sharedHighlight", (moreFlags & (1 << 4)) == 0, allocator);
			part.AddMember("family", family, allocator);
		}
		else
		{
			part.AddMember("autoSelect", (moreFlags & (1 << 7)) != 0, allocator);
			part.AddMember("showLines", (moreFlags & (1 << 6)) != 0, allocator);
			part.AddMember("wideMargins", (moreFlags & (1 << 5)) != 0, allocator);
			part.AddMember("multipleLines", (moreFlags & (1 << 4)) != 0, allocator);
			part.AddMember("reservedFamily", family, allocator);
		}
		int16_t	titleWidth = ReadBEInt16(blockData, currOffsIntoData +16);
		int16_t	iconID = ReadBEInt16(blockData, currOffsIntoData +18);
		part.AddMember("titleWidth", titleWidth, allocator);
		part.AddMember("icon", iconID, allocator);
		if( (!isButton && iconID > 0) || (isButton && styleFromLowNibble == 11 && iconID != 0) )
		{
			JsonValue selectedLines(rapidjson::kArrayType);
			if( !isButton )
			{
				if( titleWidth <= 0 )
					titleWidth = iconID;
				for( int d = iconID; d <= titleWidth; d++ )
					selectedLines.PushBack(d, allocator);
			}
			else
				selectedLines.PushBack(iconID, allocator);
			part.AddMember("selectedLines", selectedLines, allocator);
		}
		int16_t	textAlign = ReadBEInt16(blockData, currOffsIntoData +20);
		const char*	textAlignStr = "unknown";
		switch( textAlign )
		{
			case 0: textAlignStr = "left"; break;
			case 1: textAlignStr = "center"; break;
			case -1: textAlignStr = "right"; break;
			case -2: textAlignStr = "forceLeft"; break;
		}
		part.AddMember("textAlign", json_string(textAlignStr, allocator), allocator);
		int16_t	textFontID = ReadBEInt16(blockData, currOffsIntoData +22);
		part.AddMember("fontId", textFontID, allocator);
		part.AddMember("font", json_string(mFontTable[textFontID], allocator), allocator);
		int16_t	textSize = ReadBEInt16(blockData, currOffsIntoData +24);
		part.AddMember("textSize", textSize, allocator);
		int16_t	textStyleFlags = ReadBEInt16(blockData, currOffsIntoData +26);
		JsonValue textStyles(rapidjson::kArrayType);
		if( textStyleFlags & (1 << 15) ) textStyles.PushBack(json_string("group", allocator), allocator);
		if( textStyleFlags & (1 << 14) ) textStyles.PushBack(json_string("extend", allocator), allocator);
		if( textStyleFlags & (1 << 13) ) textStyles.PushBack(json_string("condense", allocator), allocator);
		if( textStyleFlags & (1 << 12) ) textStyles.PushBack(json_string("shadow", allocator), allocator);
		if( textStyleFlags & (1 << 11) ) textStyles.PushBack(json_string("outline", allocator), allocator);
		if( textStyleFlags & (1 << 10) ) textStyles.PushBack(json_string("underline", allocator), allocator);
		if( textStyleFlags & (1 << 9) ) textStyles.PushBack(json_string("italic", allocator), allocator);
		if( textStyleFlags & (1 << 8) ) textStyles.PushBack(json_string("bold", allocator), allocator);
		if( textStyleFlags == 0 ) textStyles.PushBack(json_string("plain", allocator), allocator);
		part.AddMember("textStyles", textStyles, allocator);
		int16_t	textHeight = ReadBEInt16(blockData, currOffsIntoData +28);
		if( !isButton )
			part.AddMember("textHeight", textHeight, allocator);
		size_t x = 0;
		size_t startOffs = currOffsIntoData +30;
		std::string partName = mac_roman_string(blockData, startOffs);
		part.AddMember("name", json_string(partName, allocator), allocator);
		x = MacRomanStringEnd(blockData, startOffs);
		startOffs = x +2;
		std::string partScript = mac_roman_string(blockData, startOffs);
		part.AddMember("script", json_string(partScript, allocator), allocator);
		if( !partScript.empty() )
		{
			mScriptSummaries.push_back(CScriptSummary{
				"part",
				partID,
				vFileName,
				partName,
				isButton ? "button" : "field",
				partID,
				isCard ? "card" : "background",
				blockID,
				partScript
			});
		}
		parts.PushBack(part, allocator);
		currOffsIntoData += partRecordLength;
		currOffsIntoData += (currOffsIntoData % 2);
	}
	if( !isCard )
		mButtonIDsPerBg[blockID] = buttonIDs;
	document.AddMember("parts", parts, allocator);

	JsonValue contents(rapidjson::kArrayType);
	size_t totalTextBytes = 0;
	if( numContents < 0 )
	{
		stackimport_emit_diagnosticf( "Warning: '%4s' #%d has invalid content count %d; treating as 0.\n", vBlockType, blockID, numContents );
		numContents = 0;
	}
	for( int n = 0; n < numContents; n++ )
	{
		if( !blockData.hasdata(currOffsIntoData, 4) )
		{
			stackimport_emit_diagnosticf( "Warning: Premature end of '%4s' #%d content records.\n", vBlockType, blockID );
			break;
		}
		int16_t	partID = ReadBEInt16(blockData, currOffsIntoData);
		int16_t	partLength = ReadBEInt16(blockData, currOffsIntoData +2);
		if( partLength < 0 || !blockData.hasdata(currOffsIntoData +4, static_cast<size_t>(partLength)) )
		{
			stackimport_emit_diagnosticf( "Warning: '%4s' #%d has invalid content length %d at content index %d.\n", vBlockType, blockID, partLength, n );
			break;
		}
		bool	isBgButtonContents = false;
		JsonValue content(rapidjson::kObjectType);
		CBuf	theText, theStyles;
		if( partID < 0 )
		{
			partID = static_cast<int16_t>(-static_cast<int>(partID));
			content.AddMember("layer", json_string("card", allocator), allocator);
			content.AddMember("id", partID, allocator);
			uint16_t stylesLength = ReadBEUInt16(blockData, currOffsIntoData +4);
			if( stylesLength > 32767 )
			{
				stylesLength = stylesLength -32768;
				if( stylesLength < 2 || stylesLength > static_cast<uint16_t>(partLength) )
				{
					stackimport_emit_diagnosticf( "Warning: '%4s' #%d has invalid style length %u at content index %d.\n", vBlockType, blockID, stylesLength, n );
					break;
				}
				theStyles.resize( stylesLength -2 );
				theStyles.memcpy( 0, blockData, currOffsIntoData +6, stylesLength -2 );
			}
			else
				stylesLength = 0;
			int textLength = partLength -static_cast<int>(stylesLength);
			if( textLength < 0 )
			{
				stackimport_emit_diagnosticf( "Warning: '%4s' #%d has negative text length %d at content index %d.\n", vBlockType, blockID, textLength, n );
				break;
			}
			theText.resize( static_cast<size_t>(textLength +1) );
			theText.memcpy( 0, blockData, currOffsIntoData +4 +stylesLength, static_cast<size_t>(textLength) );
			theText[theText.size()-1] = 0;
		}
		else
		{
			content.AddMember("layer", json_string("background", allocator), allocator);
			content.AddMember("id", partID, allocator);
			uint16_t stylesLength = ReadBEUInt16(blockData, currOffsIntoData +4);
			if( stylesLength > 32767 )
			{
				stylesLength = stylesLength -32768;
				if( stylesLength < 2 || stylesLength > static_cast<uint16_t>(partLength) )
				{
					stackimport_emit_diagnosticf( "Warning: '%4s' #%d has invalid style length %u at content index %d.\n", vBlockType, blockID, stylesLength, n );
					break;
				}
				theStyles.resize( stylesLength -2 );
				theStyles.memcpy( 0, blockData, currOffsIntoData +6, stylesLength -2 );
			}
			else
				stylesLength = 0;
			int textLength = partLength -static_cast<int>(stylesLength);
			if( textLength < 0 )
			{
				stackimport_emit_diagnosticf( "Warning: '%4s' #%d has negative text length %d at content index %d.\n", vBlockType, blockID, textLength, n );
				break;
			}
			theText.resize( static_cast<size_t>(textLength +1) );
			theText.memcpy( 0, blockData, currOffsIntoData +4 +stylesLength, static_cast<size_t>(textLength) );
			theText[theText.size()-1] = 0;
			if( owner != -1 )
			{
				std::vector<int32_t>& bgButtonIDs = mButtonIDsPerBg[owner];
				for( size_t x = 0; x < bgButtonIDs.size(); x++ )
				{
					if( bgButtonIDs[x] == partID )
					{
						isBgButtonContents = true;
						break;
					}
				}
			}
		}
		if( !isBgButtonContents )
		{
			JsonValue styleRuns(rapidjson::kArrayType);
			if( theStyles.size() > 0 )
			{
				for( size_t x = 0; x < theStyles.size(); )
				{
					int16_t startOffset = ReadBEInt16(theStyles, x);
					x += sizeof(int16_t);
					int16_t styleID = ReadBEInt16(theStyles, x);
					x += sizeof(int16_t);
					JsonValue styleRun(rapidjson::kObjectType);
					styleRun.AddMember("startOffset", startOffset, allocator);
					styleRun.AddMember("styleId", styleID, allocator);
					styleRuns.PushBack(styleRun, allocator);
				}
			}
			content.AddMember("styleRuns", styleRuns, allocator);
			std::string contentText = mac_roman_string(theText, 0, theText.size());
			totalTextBytes += contentText.size();
			content.AddMember("text", json_string(contentText, allocator), allocator);
		}
		else
		{
			const bool highlighted = theText.size() == 3 && theText[0] == 0 && theText[1] == '1' && theText[2] == 0;
			content.AddMember("highlight", highlighted, allocator);
		}
		contents.PushBack(content, allocator);
		currOffsIntoData += static_cast<size_t>(partLength +4 +(partLength % 2));
	}
	document.AddMember("contents", contents, allocator);
	
	size_t startOffs = currOffsIntoData;
	std::string layerName = mac_roman_string(blockData, startOffs);
	document.AddMember("name", json_string(layerName, allocator), allocator);
	size_t nameEnd = MacRomanStringEnd(blockData, startOffs);
	startOffs = nameEnd +1;
	std::string script = mac_roman_string(blockData, startOffs);
	document.AddMember("script", json_string(script, allocator), allocator);
	if( !script.empty() )
	{
		mScriptSummaries.push_back(CScriptSummary{
			isCard ? "card" : "background",
			blockID,
			vFileName,
			layerName,
			std::string(),
			-1,
			std::string(),
			-1,
			script
		});
	}
	mLayerSummaries.push_back(CLayerSummary{
		isCard,
		blockID,
		owner,
		inFlags,
		vFileName,
		layerName,
		numParts,
		numContents,
		script.size(),
		totalTextBytes
	});
	
	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );

	return write_json_document(vLayerFilePath, document, baseAllocator);
}


bool	CStackFile::LoadPageTable( int32_t blockID, CBuf& blockData, int16_t pageEntryCount )
{
	bool	success = true;
	
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'PAGE' #%d (%lu bytes)\n", blockID, blockData.size() );

	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "PAGE_%d.data", blockID );
		blockData.tofile( OutputPath(sfn) );
	}
	
	if( mCardBlockSize > 0 )
	{
		size_t		currDataOffs = 12;
		CPageSummary page;
		page.id = blockID;
		page.entryCount = pageEntryCount;
		for( int16_t entryIndex = 0; entryIndex < pageEntryCount; entryIndex++ )
		{
			if( !blockData.hasdata( currDataOffs, sizeof(int32_t) ) )
			{
				stackimport_emit_diagnosticf( "Warning: Premature end of 'PAGE' #%d (%lu bytes)\n", blockID, blockData.size() );
				break;
			}
			
			int32_t		currCardID = ReadBEInt32( blockData, currDataOffs );
			if( currCardID == 0 )
				break;	// End of page list. (Sentinel)
			uint8_t		cardFlags = static_cast<uint8_t>(blockData[currDataOffs +4]);
			page.cardIDs.push_back(currCardID);
			
			CBlockMap::iterator cardItty = mBlockMap.find(CStackBlockIdentifier("CARD",currCardID));
			if( cardItty != mBlockMap.end() )
				success = LoadLayerBlock( "CARD", currCardID, cardItty->second, cardFlags );
			else
				stackimport_emit_diagnosticf( "Warning: 'PAGE' #%d references missing 'CARD' #%d.\n", blockID, currCardID );
			
			currDataOffs += static_cast<size_t>(mCardBlockSize);
		}
		mPageSummaries.push_back(page);
	}
	else
		stackimport_emit_diagnosticf( "Warning: Couldn't parse 'PAGE' #%d (%lu bytes) because it preceded the page table list.\n", blockID, blockData.size() );

	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
	
	return success;
}


bool	CStackFile::LoadListBlock( CBuf& blockData )
{
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Processing 'LIST' #%d (%lu bytes)\n", mListBlockID, blockData.size() );
	
	if( mDumpRawBlockData )
	{
		char sfn[256] = { 0 };
		snprintf( sfn, sizeof(sfn), "LIST_%d.data", mListBlockID );
		blockData.tofile( OutputPath(sfn) );
	}

	if( blockData.size() < 42 )
	{
		stackimport_emit_diagnosticf( "Warning: 'LIST' #%d is too short (%lu bytes).\n", mListBlockID, blockData.size() );
		if( mProgressMessages )
			stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
		return true;
	}

	const int32_t maxPossiblePageTables = static_cast<int32_t>(blockData.size() / 6);
	int32_t		numPageTables = ReadBEInt32(blockData, 4);
	size_t		currDataOffs = 34;
	mCardBlockSize = ReadBEInt16(blockData, 16);
	if( (numPageTables <= 0 || numPageTables > maxPossiblePageTables)
		&& ReadBEInt32(blockData, 0) > 0
		&& ReadBEInt32(blockData, 0) <= maxPossiblePageTables )
	{
		numPageTables = ReadBEInt32(blockData, 0);
		mCardBlockSize = ReadBEInt16(blockData, 12);
		stackimport_emit_diagnosticf( "Warning: 'LIST' #%d uses compact page table layout; inferred %d page table(s) and card entry size %d.\n", mListBlockID, numPageTables, mCardBlockSize );
	}
	if( mCardBlockSize <= 0 )
	{
		stackimport_emit_diagnosticf( "Warning: 'LIST' #%d has invalid card entry size %d; page table cards will not be expanded.\n", mListBlockID, mCardBlockSize );
		mCardBlockSize = -1;
	}
	for( int32_t r = 0; r < numPageTables; r++ )
	{
		currDataOffs += 2;
		if( !blockData.hasdata( currDataOffs, sizeof(int32_t) ) )
		{
			stackimport_emit_diagnosticf( "Warning: Premature end of 'LIST' #%d (%lu bytes)\n", mListBlockID, blockData.size() );
			break;
		}
		
		int32_t		currPagetableID = ReadBEInt32( blockData, currDataOffs );
		int16_t		pageEntryCount = 0;
		if( blockData.hasdata( currDataOffs +4, sizeof(int16_t) ) )
			pageEntryCount = ReadBEInt16( blockData, currDataOffs +4 );
		
		CBlockMap::iterator pageItty = mBlockMap.find(CStackBlockIdentifier("PAGE",currPagetableID));
		if( pageItty != mBlockMap.end() )
			LoadPageTable( currPagetableID, pageItty->second, pageEntryCount );
		else
			stackimport_emit_diagnosticf( "Warning: 'LIST' #%d references missing 'PAGE' #%d.\n", mListBlockID, currPagetableID );
		
		currDataOffs += 4;
	}

	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
	
	return true;
}


#if MAC_CODE
bool	CStackFile::LoadBWIcons()
{
	stackimport_emit_diagnosticf( "Warning: Mac resource import is not implemented in the RapidJSON output path.\n" );
	return true;
}

bool	CStackFile::LoadPictures()
{
	stackimport_emit_diagnosticf( "Warning: PICT resource import is not implemented in the RapidJSON output path.\n" );
	return true;
}

bool	CStackFile::LoadCursors()
{
	stackimport_emit_diagnosticf( "Warning: Cursor resource import is not implemented in the RapidJSON output path.\n" );
	return true;
}

bool	CStackFile::LoadSounds()
{
	stackimport_emit_diagnosticf( "Warning: Sound resource import is not implemented in the RapidJSON output path.\n" );
	return true;
}

bool	CStackFile::Load68000Resources()
{
	stackimport_emit_diagnosticf( "Warning: 68K code resource import is not implemented in the RapidJSON output path.\n" );
	return true;
}

bool	CStackFile::LoadPowerPCResources()
{
	stackimport_emit_diagnosticf( "Warning: PowerPC code resource import is not implemented in the RapidJSON output path.\n" );
	return true;
}
#endif //MAC_CODE

bool	CStackFile::LoadResourceFork( const std::string& fpath )
{
	return stackimport_load_resource_fork(
		fpath,
		mBasePath,
		mFileName,
		mResourceOutput,
		mResourceSummaries,
		mResourceForkStatus,
		mResourceForkBytes);
}

bool	CStackFile::WriteScriptIndex() const
{
	StackImportRapidJsonAllocator baseAllocator;
	JsonPoolAllocator pool(4096, &baseAllocator);
	JsonDocument document(&pool, 4096, &baseAllocator);
	document.SetObject();
	JsonPoolAllocator& allocator = document.GetAllocator();
	document.AddMember("format", json_string("stackimport.scriptIndex", allocator), allocator);
	document.AddMember("schemaVersion", 1, allocator);
	document.AddMember("sourceFileName", json_string(mFileName, allocator), allocator);

	std::set<std::string> externalNames;
	for(const CResourceSummary& resource : mResourceSummaries)
	{
		if((resource.type == "XCMD" || resource.type == "XFCN" || resource.type == "xcmd" || resource.type == "xfcn") && !resource.name.empty())
			externalNames.insert(lowercase_ascii(resource.name));
	}

	JsonValue scripts(rapidjson::kArrayType);
	std::map<std::string, int> aggregateCallCounts;
	std::map<std::string, int> aggregateReferenceCounts;
	for(const CScriptSummary& source : mScriptSummaries)
	{
		JsonValue script(rapidjson::kObjectType);
		script.AddMember("ownerKind", json_string(source.ownerKind, allocator), allocator);
		script.AddMember("ownerId", source.ownerId, allocator);
		script.AddMember("file", json_string(source.file, allocator), allocator);
		script.AddMember("name", json_string(source.name, allocator), allocator);
		if(!source.partType.empty())
		{
			script.AddMember("partType", json_string(source.partType, allocator), allocator);
			script.AddMember("partId", source.partId, allocator);
		}
		if(!source.parentKind.empty())
		{
			script.AddMember("parentKind", json_string(source.parentKind, allocator), allocator);
			script.AddMember("parentId", source.parentId, allocator);
		}
		script.AddMember("scriptBytes", static_cast<uint64_t>(source.script.size()), allocator);

		JsonValue handlers(rapidjson::kArrayType);
		JsonValue calls(rapidjson::kArrayType);
		JsonValue externalCalls(rapidjson::kArrayType);
		JsonValue stringLiterals(rapidjson::kArrayType);
		JsonValue references(rapidjson::kArrayType);
		std::set<std::string> seenCalls;
		std::set<std::string> seenExternalCalls;
		std::set<std::string> seenStrings;
		std::set<std::string> seenReferences;

		const std::vector<std::string> lines = split_script_lines(source.script);
		for(size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++)
		{
			const std::string trimmed = trim_ascii(lines[lineIndex]);
			if(trimmed.empty() || starts_with_case_insensitive(trimmed, "--"))
				continue;
			const std::string lowerLine = lowercase_ascii(trimmed);

			if(starts_with_case_insensitive(trimmed, "on ") || starts_with_case_insensitive(trimmed, "function "))
			{
				const bool functionHandler = starts_with_case_insensitive(trimmed, "function ");
				const size_t nameStart = functionHandler ? 9u : 3u;
				const std::string handlerName = first_word(trimmed.substr(nameStart));
				if(!handlerName.empty())
				{
					JsonValue handler(rapidjson::kObjectType);
					handler.AddMember("name", json_string(handlerName, allocator), allocator);
					handler.AddMember("kind", json_string(functionHandler ? "function" : "message", allocator), allocator);
					handler.AddMember("line", static_cast<uint64_t>(lineIndex + 1), allocator);
					handlers.PushBack(handler, allocator);
				}
			}

			const std::string command = first_word(trimmed);
			const std::string lowerCommand = lowercase_ascii(command);
			if(!command.empty() && !is_control_command(lowerCommand))
			{
				if(seenCalls.insert(lowerCommand).second)
				{
					JsonValue call(rapidjson::kObjectType);
					call.AddMember("name", json_string(command, allocator), allocator);
					call.AddMember("line", static_cast<uint64_t>(lineIndex + 1), allocator);
					calls.PushBack(call, allocator);
				}
				aggregateCallCounts[lowerCommand]++;

				const bool looksExternal = externalNames.find(lowerCommand) != externalNames.end() ||
					(!command.empty() && (command[0] == 'x' || command[0] == 'X')) ||
					lowerCommand.rfind("ht", 0) == 0 ||
					lowerCommand == "hypertint" ||
					lowerCommand == "playqt";
				if(looksExternal && seenExternalCalls.insert(lowerCommand).second)
				{
					JsonValue externalCall(rapidjson::kObjectType);
					externalCall.AddMember("name", json_string(command, allocator), allocator);
					externalCall.AddMember("line", static_cast<uint64_t>(lineIndex + 1), allocator);
					externalCall.AddMember("resourceBacked", externalNames.find(lowerCommand) != externalNames.end(), allocator);
					externalCalls.PushBack(externalCall, allocator);
				}
			}

			for(const std::string& literal : script_string_literals(trimmed))
			{
				const std::string lowerLiteral = lowercase_ascii(literal);
				if(seenStrings.insert(literal).second)
				{
					JsonValue literalJson(rapidjson::kObjectType);
					literalJson.AddMember("value", json_string(literal, allocator), allocator);
					literalJson.AddMember("line", static_cast<uint64_t>(lineIndex + 1), allocator);
					stringLiterals.PushBack(literalJson, allocator);
				}
				const std::string referenceKind = inferred_reference_kind(lowerLine, lowerLiteral);
				if(!referenceKind.empty())
				{
					const std::string referenceKey = referenceKind + "\n" + literal;
					if(seenReferences.insert(referenceKey).second)
					{
						JsonValue reference(rapidjson::kObjectType);
						reference.AddMember("kind", json_string(referenceKind, allocator), allocator);
						reference.AddMember("value", json_string(literal, allocator), allocator);
						reference.AddMember("line", static_cast<uint64_t>(lineIndex + 1), allocator);
						references.PushBack(reference, allocator);
					}
					aggregateReferenceCounts[referenceKind]++;
				}
			}
		}

		script.AddMember("handlers", handlers, allocator);
		script.AddMember("calls", calls, allocator);
		script.AddMember("externalCalls", externalCalls, allocator);
		script.AddMember("stringLiterals", stringLiterals, allocator);
		script.AddMember("references", references, allocator);
		scripts.PushBack(script, allocator);
	}
	document.AddMember("scripts", scripts, allocator);

	JsonValue summary(rapidjson::kObjectType);
	summary.AddMember("scriptCount", static_cast<uint64_t>(mScriptSummaries.size()), allocator);
	JsonValue callCounts(rapidjson::kObjectType);
	for(const auto& count : aggregateCallCounts)
	{
		JsonValue key = json_string(count.first, allocator);
		callCounts.AddMember(key, count.second, allocator);
	}
	summary.AddMember("callCounts", callCounts, allocator);
	JsonValue referenceCounts(rapidjson::kObjectType);
	for(const auto& count : aggregateReferenceCounts)
	{
		JsonValue key = json_string(count.first, allocator);
		referenceCounts.AddMember(key, count.second, allocator);
	}
	summary.AddMember("referenceCounts", referenceCounts, allocator);
	document.AddMember("summary", summary, allocator);

	return write_json_document(OutputPath("script-index.json"), document, baseAllocator);
}


bool	CStackFile::WriteJsonIndexes() const
{
	StackImportRapidJsonAllocator baseAllocator;
	JsonPoolAllocator projectPool(1024, &baseAllocator);
	JsonDocument project(&projectPool, 1024, &baseAllocator);
	project.SetObject();
	JsonPoolAllocator& projectAllocator = project.GetAllocator();

	project.AddMember("format", json_string("stackimport.project", projectAllocator), projectAllocator);
	project.AddMember("sourceFileName", json_string(mFileName, projectAllocator), projectAllocator);
	project.AddMember("stackFile", json_string("stack_-1.json", projectAllocator), projectAllocator);
	project.AddMember("userLevel", mUserLevel, projectAllocator);
	project.AddMember("privateAccess", mStackPrivateAccess, projectAllocator);
	project.AddMember("cantPeek", mStackCantPeek, projectAllocator);
	project.AddMember("createdByVersion", json_string(mCreatedByVersion, projectAllocator), projectAllocator);
	project.AddMember("lastCompactedVersion", json_string(mLastCompactedVersion, projectAllocator), projectAllocator);
	project.AddMember("lastEditedVersion", json_string(mLastEditedVersion, projectAllocator), projectAllocator);
	project.AddMember("firstEditedVersion", json_string(mFirstEditedVersion, projectAllocator), projectAllocator);
	project.AddMember("patternCount", mStackPatternCount, projectAllocator);

	JsonValue outputs(rapidjson::kArrayType);
	for(int n = 0; n < mStackPatternCount; n++)
	{
		char patternFile[256] = { 0 };
		snprintf(patternFile, sizeof(patternFile), "PAT_%d.pbm", n + 1);
		outputs.PushBack(json_output_ref("pattern", n + 1, patternFile, "STAK", mStackID, projectAllocator), projectAllocator);
	}

	JsonValue blockArray(rapidjson::kArrayType);
	for(const auto& block : mBlockMap)
	{
		JsonValue blockObject(rapidjson::kObjectType);
		blockObject.AddMember("type", json_string(block.first.mType, projectAllocator), projectAllocator);
		blockObject.AddMember("typeCode", ReadBEUInt32Bytes(block.first.mType), projectAllocator);
		blockObject.AddMember("id", block.first.mID, projectAllocator);
		blockObject.AddMember("size", static_cast<uint64_t>(block.second.size()), projectAllocator);
		blockObject.AddMember("understood", block.first == CStackBlockIdentifier("BKGD")
			|| block.first == CStackBlockIdentifier("BMAP")
			|| block.first == CStackBlockIdentifier("CARD")
			|| block.first == CStackBlockIdentifier("FTBL")
			|| block.first == CStackBlockIdentifier("LIST")
			|| block.first == CStackBlockIdentifier("MAST")
			|| block.first == CStackBlockIdentifier("PAGE")
			|| block.first == CStackBlockIdentifier("PRFT")
			|| block.first == CStackBlockIdentifier("PRNT")
			|| block.first == CStackBlockIdentifier("PRST")
			|| block.first == CStackBlockIdentifier("STAK")
			|| block.first == CStackBlockIdentifier("STBL"),
			projectAllocator);
		if(block.first == CStackBlockIdentifier("BMAP"))
		{
			char bitmapFile[256] = { 0 };
			snprintf(bitmapFile, sizeof(bitmapFile), "BMAP_%d.%s", block.first.mID, mDecodeGraphics ? "pbm" : "raw");
			blockObject.AddMember("outputFile", json_string(bitmapFile, projectAllocator), projectAllocator);
			outputs.PushBack(json_output_ref(mDecodeGraphics ? "bitmap" : "rawBitmap", block.first.mID, bitmapFile, "BMAP", block.first.mID, projectAllocator), projectAllocator);
		}
		else if(block.first == CStackBlockIdentifier("MAST"))
			outputs.PushBack(json_output_ref("master", block.first.mID, "master_-1.json", "MAST", block.first.mID, projectAllocator), projectAllocator);
		else if(block.first == CStackBlockIdentifier("PRNT"))
			outputs.PushBack(json_output_ref("printSettings", block.first.mID, "printsettings.json", "PRNT", block.first.mID, projectAllocator), projectAllocator);
		else if(block.first == CStackBlockIdentifier("PRST"))
		{
			char pageSetupFile[256] = { 0 };
			snprintf(pageSetupFile, sizeof(pageSetupFile), "pagesetup_%d.json", block.first.mID);
			outputs.PushBack(json_output_ref("pageSetup", block.first.mID, pageSetupFile, "PRST", block.first.mID, projectAllocator), projectAllocator);
		}
		else if(block.first == CStackBlockIdentifier("PRFT"))
		{
			char reportTemplateFile[256] = { 0 };
			snprintf(reportTemplateFile, sizeof(reportTemplateFile), "reporttemplate_%d.json", block.first.mID);
			outputs.PushBack(json_output_ref("reportTemplate", block.first.mID, reportTemplateFile, "PRFT", block.first.mID, projectAllocator), projectAllocator);
		}
		blockArray.PushBack(blockObject, projectAllocator);
	}
	project.AddMember("blocks", blockArray, projectAllocator);

	JsonValue fontArray(rapidjson::kArrayType);
	for(const auto& font : mFontTable)
	{
		JsonValue fontObject(rapidjson::kObjectType);
		fontObject.AddMember("id", font.first, projectAllocator);
		fontObject.AddMember("name", json_string(font.second, projectAllocator), projectAllocator);
		fontArray.PushBack(fontObject, projectAllocator);
	}
	project.AddMember("fonts", fontArray, projectAllocator);

	JsonValue styleArray(rapidjson::kArrayType);
	for(const auto& styleEntry : mStyles)
	{
		const CStyleEntry& style = styleEntry.second;
		JsonValue styleObject(rapidjson::kObjectType);
		styleObject.AddMember("id", styleEntry.first, projectAllocator);
		styleObject.AddMember("fontId", style.mFontID, projectAllocator);
		styleObject.AddMember("fontName", json_string(style.mFontName, projectAllocator), projectAllocator);
		styleObject.AddMember("fontSize", style.mFontSize, projectAllocator);
		styleObject.AddMember("bold", style.mBold, projectAllocator);
		styleObject.AddMember("italic", style.mItalic, projectAllocator);
		styleObject.AddMember("underline", style.mUnderline, projectAllocator);
		styleObject.AddMember("outline", style.mOutline, projectAllocator);
		styleObject.AddMember("shadow", style.mShadow, projectAllocator);
		styleObject.AddMember("condense", style.mCondense, projectAllocator);
		styleObject.AddMember("extend", style.mExtend, projectAllocator);
		styleObject.AddMember("group", style.mGroup, projectAllocator);
		styleArray.PushBack(styleObject, projectAllocator);
	}
	project.AddMember("styles", styleArray, projectAllocator);
	if(!mStyleSheetName.empty())
		outputs.PushBack(json_output_ref("stylesheet", mStyleTableBlockID, mStyleSheetName.c_str(), "STBL", mStyleTableBlockID, projectAllocator), projectAllocator);
	outputs.PushBack(json_output_ref("project", -1, "project.json", nullptr, -1, projectAllocator), projectAllocator);
	outputs.PushBack(json_output_ref("stack", mStackID, "stack_-1.json", "STAK", mStackID, projectAllocator), projectAllocator);
	outputs.PushBack(json_output_ref("scriptIndex", -1, "script-index.json", nullptr, -1, projectAllocator), projectAllocator);
	for(const auto& layer : mLayerSummaries)
		outputs.PushBack(json_output_ref(layer.isCard ? "card" : "background", layer.id, layer.file.c_str(), layer.isCard ? "CARD" : "BKGD", layer.id, projectAllocator), projectAllocator);
	project.AddMember("outputs", outputs, projectAllocator);

	JsonStringBuffer projectBuffer(&baseAllocator);
	JsonWriter projectWriter(projectBuffer, &baseAllocator);
	project.Accept(projectWriter);
	const std::string projectPath = OutputPath("project.json");
	if(!write_json_file(projectPath, projectBuffer.GetString(), projectBuffer.GetSize()))
		return false;

	JsonPoolAllocator stackPool(1024, &baseAllocator);
	JsonDocument stack(&stackPool, 1024, &baseAllocator);
	stack.SetObject();
	JsonPoolAllocator& stackAllocator = stack.GetAllocator();
	stack.AddMember("format", json_string("stackimport.stack", stackAllocator), stackAllocator);
	stack.AddMember("id", mStackID, stackAllocator);
	stack.AddMember("name", json_string(mFileName, stackAllocator), stackAllocator);
	stack.AddMember("firstCardId", mFirstCardID, stackAllocator);
	stack.AddMember("listBlockId", mListBlockID, stackAllocator);
	stack.AddMember("fontTableBlockId", mFontTableBlockID, stackAllocator);
	stack.AddMember("styleTableBlockId", mStyleTableBlockID, stackAllocator);
	stack.AddMember("cardBlockSize", mCardBlockSize, stackAllocator);
	stack.AddMember("cardCount", mStackCardCount, stackAllocator);
	stack.AddMember("cardWidth", mCardWidth, stackAllocator);
	stack.AddMember("cardHeight", mCardHeight, stackAllocator);
	stack.AddMember("patternCount", mStackPatternCount, stackAllocator);
	stack.AddMember("cantModify", mStackCantModify, stackAllocator);
	stack.AddMember("cantDelete", mStackCantDelete, stackAllocator);
	stack.AddMember("cantAbort", mStackCantAbort, stackAllocator);
	stack.AddMember("script", json_string(mStackScript, stackAllocator), stackAllocator);
	JsonValue pages(rapidjson::kArrayType);
	for(const auto& pageSummary : mPageSummaries)
	{
		JsonValue page(rapidjson::kObjectType);
		page.AddMember("id", pageSummary.id, stackAllocator);
		page.AddMember("entryCount", pageSummary.entryCount, stackAllocator);
		JsonValue cards(rapidjson::kArrayType);
		for(int32_t cardID : pageSummary.cardIDs)
			cards.PushBack(cardID, stackAllocator);
		page.AddMember("cardIds", cards, stackAllocator);
		pages.PushBack(page, stackAllocator);
	}
	stack.AddMember("pages", pages, stackAllocator);
	JsonValue layers(rapidjson::kArrayType);
	for(const auto& layer : mLayerSummaries)
	{
		JsonValue layerObject(rapidjson::kObjectType);
		layerObject.AddMember("kind", json_string(layer.isCard ? "card" : "background", stackAllocator), stackAllocator);
		layerObject.AddMember("id", layer.id, stackAllocator);
		layerObject.AddMember("file", json_string(layer.file, stackAllocator), stackAllocator);
		layerObject.AddMember("name", json_string(layer.name, stackAllocator), stackAllocator);
		layerObject.AddMember("partCount", layer.partCount, stackAllocator);
		layerObject.AddMember("contentCount", layer.contentCount, stackAllocator);
		layerObject.AddMember("scriptBytes", static_cast<uint64_t>(layer.scriptBytes), stackAllocator);
		layerObject.AddMember("textBytes", static_cast<uint64_t>(layer.textBytes), stackAllocator);
		if(layer.isCard)
		{
			layerObject.AddMember("owner", layer.owner, stackAllocator);
			layerObject.AddMember("marked", (layer.flags & 16) != 0, stackAllocator);
		}
		layers.PushBack(layerObject, stackAllocator);
	}
	stack.AddMember("layers", layers, stackAllocator);

	JsonStringBuffer stackBuffer(&baseAllocator);
	JsonWriter stackWriter(stackBuffer, &baseAllocator);
	stack.Accept(stackWriter);
	const std::string stackPath = OutputPath("stack_-1.json");
	return write_json_file(stackPath, stackBuffer.GetString(), stackBuffer.GetSize()) && WriteScriptIndex();
}


bool	CStackFile::LoadFile( const std::string& fpath, const std::string& outputPackagePath )
{
	if( outputPackagePath.empty() )
	{
		stackimport_emit_diagnosticf( "Error: Missing output package path.\n" );
		return false;
	}

	size_t		slashPos = fpath.rfind('/');
	if( slashPos == std::string::npos )
		slashPos = 0;
	else
		slashPos += 1;
	mFileName = fpath.substr(slashPos, std::string::npos);

	std::string				packagePath( outputPackagePath );
	if( stackimport_internal_make_directory( packagePath.c_str() ) != 0 && errno != EEXIST )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't create output package directory '%s'\n", packagePath.c_str() );
		return false;
	}

	mBasePath = packagePath;
	mSourcePath = fpath;
		
  #if MAC_CODE
	FSRef		fileRef;
	mResRefNum = -1;
	
		OSStatus	resErr = FSPathMakeRef( reinterpret_cast<const UInt8*>(fpath.c_str()), &fileRef, NULL );
	if( resErr == noErr )
	{
		mResRefNum = FSOpenResFile( &fileRef, fsRdPerm );
		if( mResRefNum < 0 )
		{
			stackimport_emit_diagnosticf( "Warning: No Mac resource fork to import.\n" );
			resErr = fnfErr;
		}
	}
	else
	{
		stackimport_emit_diagnosticf( "Error: Error %d locating input file's resource fork.\n", static_cast<int>(resErr) );
		mResRefNum = -1;
	}
  #endif //MAC_CODE
	
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Output package name is '%s'\n", mBasePath.c_str() );

	std::string	sourceStreamStatus("ok");

	// Use streaming BlockParser instead of manual block reading loop
	PlatformStackReader fileReader;
	if (!fileReader.open(fpath.c_str())) {
		stackimport_emit_diagnosticf("Error: Couldn't open file '%s'\n", fpath.c_str());
		return false;
	}

	CStackBlockOutput blockOutput(mBlockMap, mSourceBlocks, sourceStreamStatus);
	stackimport::BlockParser parser;

	auto parseResult = parser.parse(fileReader, blockOutput);
	if (parseResult != stackimport::BlockErr::None) {
		stackimport_emit_diagnosticf("Error: Block parsing failed\n");
		return false;
	}

	int numBlocks = blockOutput.num_blocks();
	const uint64_t dataForkBytes = fileReader.bytes_read();
	bool success = true;
	
	if( mStatusMessages )
		stackimport_emit_infof( "Status: Found %d blocks in file.\n", numBlocks);
	if( !success )
		return false;
	
	mMaxProgress = static_cast<int>(mBlockMap.size());
	mCurrentProgress = 0;
	
  #if MAC_CODE
	if( mResRefNum > 0 )
	{
		int		numResources = Count1Resources( 'ICON' ) +Count1Resources( 'PICT' ) +Count1Resources( 'CURS' )
						+Count1Resources( 'snd ' ) +Count1Resources( 'HCbg' ) +Count1Resources( 'HCcd' )
						+Count1Resources( 'XCMD' ) +Count1Resources( 'XFCN' ) +Count1Resources( 'xcmd' )
						+Count1Resources( 'xfcn' );
		mMaxProgress += numResources;
		if( mStatusMessages )
			stackimport_emit_infof( "Status: Found %d resources in file.\n", numResources);
	}
  #endif // MAC_CODE
	if( mProgressMessages )
		stackimport_emit_infof( "Progress: %d of %d\n", mCurrentProgress, mMaxProgress );
	
	// Load some "table of contents"-style blocks other blocks need to refer to:
	CBlockMap::iterator		stackItty = mBlockMap.find(CStackBlockIdentifier("STAK"));
	if( stackItty == mBlockMap.end() )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't find stack block.\n" );
		return false;
	}
	success = LoadStackBlock( -1, stackItty->second );
	if( success && !LoadResourceFork(fpath) )
		return false;
	if( success && !WriteSourceManifest(dataForkBytes, sourceStreamStatus.c_str()) )
		return false;
	if( success )
	{
		CBlockMap::iterator fontTableItty = mBlockMap.find(CStackBlockIdentifier("FTBL",mFontTableBlockID));
		if( fontTableItty != mBlockMap.end() )
			success = LoadFontTable( mFontTableBlockID, fontTableItty->second );
		else
		{
			stackimport_emit_diagnosticf( "Warning: Referenced 'FTBL' #%d is missing; using an empty font table.\n", mFontTableBlockID );
			CBuf emptyFontTable;
			success = LoadFontTable( mFontTableBlockID, emptyFontTable );
		}
	}
	if( success )
	{
		CBlockMap::iterator styleTableItty = mBlockMap.find(CStackBlockIdentifier("STBL",mStyleTableBlockID));
		if( styleTableItty != mBlockMap.end() )
			success = LoadStyleTable( mStyleTableBlockID, styleTableItty->second );
		else
		{
			stackimport_emit_diagnosticf( "Warning: Referenced 'STBL' #%d is missing; using an empty style table.\n", mStyleTableBlockID );
			CBuf emptyStyleTable;
			success = LoadStyleTable( mStyleTableBlockID, emptyStyleTable );
		}
	}
	
	// Now load all backgrounds and bitmaps:
	if( success )
	{
		CBlockMap::iterator	currBlockItty = mBlockMap.begin();
		for( ; currBlockItty != mBlockMap.end(); currBlockItty++ )
		{
			if( currBlockItty->first == CStackBlockIdentifier("BMAP") )
			{
				int32_t		blockID = currBlockItty->first.mID;
				CBuf&		blockData = currBlockItty->second;
				if( mStatusMessages )
					stackimport_emit_infof( "Status: Processing 'BMAP' #%d %X (%lu bytes)\n", blockID, blockID, blockData.size() );
				
				char		fname[256] = { 0 };
				
				if( mDecodeGraphics )
				{
					const bool hasHeader = blockData.size() >= 52;
					const int16_t totalTop = hasHeader ? ReadBEInt16(blockData, 12) : 0;
					const int16_t totalLeft = hasHeader ? ReadBEInt16(blockData, 14) : 0;
					const int16_t totalBottom = hasHeader ? ReadBEInt16(blockData, 16) : 0;
					const int16_t totalRight = hasHeader ? ReadBEInt16(blockData, 18) : 0;
					const int32_t maskDataLength = hasHeader ? ReadBEInt32(blockData, 44) : -1;
					const int32_t pictureDataLength = hasHeader ? ReadBEInt32(blockData, 48) : -1;
					const bool validWobaHeader = hasHeader
						&& totalRight > totalLeft
						&& totalBottom > totalTop
						&& maskDataLength >= 0
						&& pictureDataLength >= 0
						&& static_cast<size_t>(maskDataLength) <= blockData.size() - 52
						&& static_cast<size_t>(pictureDataLength) <= blockData.size() - 52 - static_cast<size_t>(maskDataLength);
					if( validWobaHeader )
					{
						snprintf( fname, sizeof(fname), "BMAP_%u.pbm", blockID );
						
						picture		thePicture;
						woba_decode( thePicture, blockData.buf() );
						
						thePicture.writebitmapandmasktopbm( OutputPath(fname).c_str() );
					}
					else
					{
						stackimport_emit_diagnosticf( "Warning: 'BMAP' #%d has unsupported WOBA header; writing raw data instead.\n", blockID );
						snprintf( fname, sizeof(fname), "BMAP_%u.raw", blockID );
						blockData.tofile( OutputPath(fname) );
					}
				}
				else
				{
					snprintf( fname, sizeof(fname), "BMAP_%u.raw", blockID );
					blockData.tofile( OutputPath(fname) );
				}
				
				if( mProgressMessages )
					stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
			}
			else if( currBlockItty->first == CStackBlockIdentifier("BKGD") )
			{
			  #if 1
				success = LoadLayerBlock( "BKGD", currBlockItty->first.mID, currBlockItty->second, 0 );
			  #else
				success = LoadBackgroundBlock( currBlockItty->first.mID, currBlockItty->second );
			  #endif
			}
			else if( currBlockItty->first == CStackBlockIdentifier("MAST") )
			{
				success = LoadMasterBlock( currBlockItty->first.mID, currBlockItty->second );
			}
			else if( currBlockItty->first == CStackBlockIdentifier("PRNT") )
			{
				success = LoadPrintBlock( currBlockItty->first.mID, currBlockItty->second );
			}
			else if( currBlockItty->first == CStackBlockIdentifier("PRST") )
			{
				success = LoadPageSetupBlock( currBlockItty->first.mID, currBlockItty->second );
			}
			else if( currBlockItty->first == CStackBlockIdentifier("PRFT") )
			{
				success = LoadReportTemplateBlock( currBlockItty->first.mID, currBlockItty->second );
			}
			else if( currBlockItty->first != CStackBlockIdentifier("CARD")
					&& currBlockItty->first != CStackBlockIdentifier("LIST")
					&& currBlockItty->first != CStackBlockIdentifier("PAGE")
					&& currBlockItty->first != CStackBlockIdentifier("STAK")
					&& currBlockItty->first != CStackBlockIdentifier("FTBL")
					&& currBlockItty->first != CStackBlockIdentifier("STBL") )
			{
				stackimport_emit_diagnosticf( "Warning: Skipping block %4s #%d,\n", currBlockItty->first.mType, currBlockItty->first.mID );
				if( mProgressMessages )
					stackimport_emit_infof( "Progress: %d of %d\n", ++mCurrentProgress, mMaxProgress );
			}
		}
	}
	
	// Now actually load the cards, which depend on knowledge from backgrounds, stack, style table etc.:
	if( success )
	{
		CBlockMap::iterator listItty = mBlockMap.find(CStackBlockIdentifier("LIST",mListBlockID));
		if( listItty != mBlockMap.end() )
			success = LoadListBlock( listItty->second );
		else
		{
			stackimport_emit_diagnosticf( "Warning: Referenced 'LIST' #%d is missing; no cards will be loaded.\n", mListBlockID );
			success = true;
		}
	}
		
  #if MAC_CODE
	if( mResRefNum > 0 )
	{
		LoadBWIcons();
		LoadPictures();
		LoadCursors();
		LoadSounds();
		Load68000Resources();
		LoadPowerPCResources();
		
		CloseResFile( mResRefNum );
	}
  #endif // MAC_CODE
	
	if( success && !WriteJsonIndexes() )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't write RapidJSON output indexes.\n" );
		success = false;
	}

  #if MAC_CODE
	if( resErr != fnfErr && resErr != noErr )
	{
		stackimport_emit_diagnosticf( "Error: During conversion of Macintosh fork of stack.\n" );
		return false;
	}
  #endif // MAC_CODE
	
	return success;
}
