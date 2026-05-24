/*
 *  CStackFile.cpp
 *  stackimport
 *
 *  Created by Mr. Z. on 10/06/06.
 *  Copyright 2006 Mr Z. All rights reserved.
 *
 */


#include "CStackFile.h"
#include "Mac68kDisassembly.h"
#include <cerrno>
#include <iostream>
#include <fstream>
#include <memory>
#include <span>
#include <sys/stat.h>
#include <unistd.h>
#include "picture.h"
#include "woba.h"
#include "CBuf.h"
#include "stackimport_logging.h"
#include "stackimport_platform_internal.h"
#include "stackimport_rapidjson_allocator.h"
#include "vendor/snd2wav/snd2wav/snd2wav.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#if STACKIMPORT_HAS_RESOURCE_DASM
#include <resource_file/IndexFormats/Formats.hh>
#include <resource_file/ResourceTypes.hh>
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

std::string four_char_type(uint32_t type)
{
	std::string result;
	result.push_back(static_cast<char>((type >> 24u) & 0xFFu));
	result.push_back(static_cast<char>((type >> 16u) & 0xFFu));
	result.push_back(static_cast<char>((type >> 8u) & 0xFFu));
	result.push_back(static_cast<char>(type & 0xFFu));
	return result;
}

std::string sanitized_resource_file_name(const std::string& stackName, const std::string& type, int32_t id, const std::string& name, const char* extension)
{
	std::string result;
	result.reserve(stackName.size() + type.size() + name.size() + 32);
	auto append_sanitized = [&result](const std::string& text) {
		for(char ch : text)
		{
			const unsigned char uch = static_cast<unsigned char>(ch);
			if((uch >= 'A' && uch <= 'Z') || (uch >= 'a' && uch <= 'z') || (uch >= '0' && uch <= '9') || ch == '-' || ch == '_')
				result.push_back(ch);
			else if(ch == ' ' || ch == '.')
				result.push_back('_');
		}
	};
	append_sanitized(stackName);
	result.push_back('_');
	append_sanitized(type);
	result.push_back('_');
	result += std::to_string(id);
	if(!name.empty())
	{
		result.push_back('_');
		append_sanitized(name);
	}
	result += extension;
	return result;
}

bool write_text_file(const std::string& path, const std::string& text)
{
	std::ofstream file(path.c_str(), std::ios::binary);
	if(!file.is_open())
		return false;
	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	return static_cast<bool>(file);
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
	stackimport_internal_close_file(file);
	return ok;
}

bool write_json_document(const std::string& path, JsonDocument& document, StackImportRapidJsonAllocator& baseAllocator)
{
	JsonStringBuffer jsonBuffer(&baseAllocator);
	JsonWriter writer(jsonBuffer, &baseAllocator);
	document.Accept(writer);
	return write_json_file(path, jsonBuffer.GetString(), jsonBuffer.GetSize());
}

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
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.buf( offs, sizeof(uint16_t) ));
	return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) << 8 | static_cast<uint16_t>(bytes[1]));
}

int32_t	ReadBEInt32( const CBuf& data, size_t offs )
{
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.buf( offs, sizeof(uint32_t) ));
	return static_cast<int32_t>(static_cast<uint32_t>(bytes[0]) << 24
		| static_cast<uint32_t>(bytes[1]) << 16
		| static_cast<uint32_t>(bytes[2]) << 8
		| static_cast<uint32_t>(bytes[3]));
}

uint16_t	ReadBEUInt16( const CBuf& data, size_t offs )
{
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.buf( offs, sizeof(uint16_t) ));
	return static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) << 8 | static_cast<uint16_t>(bytes[1]));
}

uint32_t	ReadBEUInt32( const CBuf& data, size_t offs )
{
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.buf( offs, sizeof(uint32_t) ));
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

uint64_t	FileSizeBytes( const std::string& path )
{
	struct stat fileInfo = {};
	if( stat( path.c_str(), &fileInfo ) != 0 || fileInfo.st_size < 0 )
		return 0;
	return static_cast<uint64_t>(fileInfo.st_size);
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
	mListBlockID(-1), mFontTableBlockID(-1), mStyleTableBlockID(-1), mStackID(-1),
	mStackCardCount(0), mFirstCardID(0), mUserLevel(0), mCardWidth(512), mCardHeight(342),
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

	JsonValue dataFork(rapidjson::kObjectType);
	dataFork.AddMember("bytes", JsonValue().SetUint64(dataForkBytes), allocator);
	dataFork.AddMember("streamStatus", json_string(streamStatus ? streamStatus : "ok", allocator), allocator);

	JsonValue blocks(rapidjson::kArrayType);
	std::map<std::string,int32_t> blockTypeCounts;
	for( const CSourceBlockSummary& block : mSourceBlocks )
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("type", json_string(block.type, allocator), allocator);
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
	JsonValue resources(rapidjson::kArrayType);
	std::map<std::string,int32_t> resourceTypeCounts;
	for( const CResourceSummary& resource : mResourceSummaries )
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("type", json_string(resource.type, allocator), allocator);
		item.AddMember("id", resource.id, allocator);
		item.AddMember("flags", resource.flags, allocator);
		item.AddMember("name", json_string(resource.name, allocator), allocator);
		item.AddMember("bytes", JsonValue().SetUint64(resource.bytes), allocator);
		item.AddMember("status", json_string(resource.status, allocator), allocator);
		item.AddMember("architecture", json_string(resource.architecture, allocator), allocator);
		if(!resource.disassemblyFile.empty())
			item.AddMember("disassemblyFile", json_string(resource.disassemblyFile, allocator), allocator);
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
	mStackCardCount = ReadBEInt32(blockData, 32);
	mFirstCardID = ReadBEInt32(blockData, 36);
	mListBlockID = ReadBEInt32(blockData, 40);
	mUserLevel = ReadBEInt16(blockData, 60);
	int16_t	flags = ReadBEInt16(blockData, 64);
	mStackCantModify = (flags & (1 << 15)) != 0;
	mStackCantDelete = (flags & (1 << 14)) != 0;
	mStackPrivateAccess = (flags & (1 << 13)) != 0;
	mStackCantAbort = (flags & (1 << 11)) != 0;
	mStackCantPeek = (flags & (1 << 10)) != 0;
	char		versStr[16] = { 0 };
	NumVersionToStr( reinterpret_cast<const unsigned char*>(blockData.buf( 84, 4 )), versStr );
	mCreatedByVersion = "HyperCard ";
	mCreatedByVersion += versStr;
	NumVersionToStr( reinterpret_cast<const unsigned char*>(blockData.buf( 88, 4 )), versStr );
	mLastCompactedVersion = "HyperCard ";
	mLastCompactedVersion += versStr;
	NumVersionToStr( reinterpret_cast<const unsigned char*>(blockData.buf( 92, 4 )), versStr );
	mLastEditedVersion = "HyperCard ";
	mLastEditedVersion += versStr;
	NumVersionToStr( reinterpret_cast<const unsigned char*>(blockData.buf( 96, 4 )), versStr );
	mFirstEditedVersion = "HyperCard ";
	mFirstEditedVersion += versStr;
	mFontTableBlockID = ReadBEInt32(blockData, 420);
	mStyleTableBlockID = ReadBEInt32(blockData, 424);
	mCardHeight = ReadBEInt16(blockData, 428);
	if( mCardHeight == 0 )
		mCardHeight = 342;
	mCardWidth = ReadBEInt16(blockData, 430);
	if( mCardWidth == 0 )
		mCardWidth = 512;

	char			pattern[8] = { 0 };
	size_t			offs = 692;
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
	
	mStackScript = mac_roman_string( blockData, 1524 );
	
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
		FILE* vStylesheetFile = fopen( OutputPath(vFileName).c_str(), "w" );
		if( vStylesheetFile )
		{
			fprintf( vStylesheetFile, "/* Missing or empty HyperCard style table %d. */\n", blockID );
			fclose( vStylesheetFile );
		}
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
	
	FILE*		vStylesheetFile = fopen( vLayerFilePath.c_str(), "w" );
	
	currOffs += 2;
	int16_t	nextStyleID = ReadBEInt16(blockData, currOffs);
	(void)nextStyleID;
	currOffs += 2;
	currOffs += 2;
	
	for( int s = 0; s < styleCount; s++ )
	{
		CStyleEntry		style;
		
		style.mStyleID = ReadBEInt16(blockData, currOffs);
		fprintf( vStylesheetFile, "\t\t.style%d\n\t\t{\n", style.mStyleID );
		currOffs += 2;
		currOffs += 8;
		
		style.mFontID = ReadBEInt16(blockData, currOffs);
		if( style.mFontID != -1 )
		{
			style.mFontName = mFontTable[style.mFontID];
			fprintf( vStylesheetFile, "\t\t\tfont-family: \"%s\";\n", style.mFontName.c_str() );
		}
		currOffs += 2;
		
		int16_t	textStyleFlags = ReadBEInt16(blockData, currOffs);
		currOffs += 2;
		
		if( textStyleFlags == 0 )
			fprintf( vStylesheetFile, "\t\t\tfont-style: normal;\n" );
		else if( textStyleFlags != -1 )	// -1 means use field style.
		{
			if( textStyleFlags & (1 << 15) )
			{
				fprintf( vStylesheetFile, "\t\t\t/* group text style */\n" );
				style.mGroup = true;
			}
			if( textStyleFlags & (1 << 14) )
			{
				fprintf( vStylesheetFile, "\t\t\tletter-spacing: 0.1em;\n" );
				style.mExtend = true;
			}
			if( textStyleFlags & (1 << 13) )
			{
				fprintf( vStylesheetFile, "\t\t\tletter-spacing: -0.1em;\n" );
				style.mCondense = true;
			}
			if( textStyleFlags & (1 << 12) )
			{
				fprintf( vStylesheetFile, "\t\t\ttext-shadow: 1px 1px #000000;\n" );
				style.mShadow = true;
			}
			if( textStyleFlags & (1 << 11) )
			{
				fprintf( vStylesheetFile, "\t\t\tcolor: white; -webkit-text-stroke-width: 1pt; -webkit-text-stroke-color: #000;\n" );
				style.mOutline = true;
			}
			if( textStyleFlags & (1 << 10) )
			{
				fprintf( vStylesheetFile, "\t\t\ttext-decoration: underline;\n" );
				style.mUnderline = true;
			}
			if( textStyleFlags & (1 << 9) )
			{
				fprintf( vStylesheetFile, "\t\t\tfont-style: italic;\n" );
				style.mItalic = true;
			}
			if( textStyleFlags & (1 << 8) )
			{
				fprintf( vStylesheetFile, "\t\t\tfont-weight: bold;\n" );
				style.mBold = true;
			}
		}
		int16_t	fontSize = ReadBEInt16(blockData, currOffs);
		if( fontSize != -1 )
		{
			fprintf( vStylesheetFile, "\t\t\tfont-size: %dpt;\n", fontSize );
			style.mFontSize = fontSize;
		}
		currOffs += 2;
		currOffs += 8;	// 2 bytes padding?
		
		fprintf( vStylesheetFile, "\t\t}\n" );
		
		mStyles[style.mStyleID] = style;
	}
	
	fclose( vStylesheetFile );
	
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
		part.AddMember("name", json_string(mac_roman_string(blockData, startOffs), allocator), allocator);
		x = MacRomanStringEnd(blockData, startOffs);
		startOffs = x +2;
		part.AddMember("script", json_string(mac_roman_string(blockData, startOffs), allocator), allocator);
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
	mResourceSummaries.clear();
	mResourceForkBytes = 0;

	const std::string resourceForkPath = fpath + "/..namedfork/rsrc";
	std::ifstream resourceFile(resourceForkPath.c_str(), std::ios::binary);
	if(!resourceFile.is_open())
	{
		mResourceForkStatus = "missing_or_empty";
		return true;
	}
	std::string resourceForkData(
		(std::istreambuf_iterator<char>(resourceFile)),
		std::istreambuf_iterator<char>());
	mResourceForkBytes = static_cast<uint64_t>(resourceForkData.size());
	if(resourceForkData.empty())
	{
		mResourceForkStatus = "missing_or_empty";
		return true;
	}

#if STACKIMPORT_HAS_RESOURCE_DASM
	ResourceDASM::ResourceFile parsed = ResourceDASM::parse_resource_fork(resourceForkData);
	if(parsed.empty())
	{
		mResourceForkStatus = "empty";
		return true;
	}

	const std::string outputDir = OutputPath("resource-disassembly");
	if(stackimport_internal_make_directory(outputDir.c_str()) != 0 && errno != EEXIST)
	{
		stackimport_emit_diagnosticf( "Warning: Couldn't create resource disassembly directory '%s'.\n", outputDir.c_str() );
		mResourceForkStatus = "output_directory_failed";
		return true;
	}

	const uint32_t xcmdType = ResourceDASM::resource_type("xcmd");
	const uint32_t xfcnType = ResourceDASM::resource_type("xfcn");
	for(const auto& resourceId : parsed.all_resources())
	{
		std::shared_ptr<const ResourceDASM::ResourceFile::Resource> resource = parsed.get_resource(resourceId.first, resourceId.second);
		if(!resource)
			continue;

		CResourceSummary summary;
		summary.type = four_char_type(resource->type);
		summary.id = resource->id;
		summary.flags = resource->flags;
		summary.name = resource->name;
		summary.bytes = resource->data.size();
		summary.status = "preserved";

		stackimport::Mac68kDisassemblyResult disassembly;
		if(resource->type == ResourceDASM::RESOURCE_TYPE_XCMD || resource->type == ResourceDASM::RESOURCE_TYPE_XFCN)
		{
			summary.architecture = "mac68k";
			disassembly = stackimport::DisassembleMac68kCodeResource(
				std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(resource->data.data()), resource->data.size()),
				0,
				resource->data.size(),
				0);
		}
		else if(resource->type == xcmdType || resource->type == xfcnType)
		{
			summary.architecture = "macppc";
			disassembly = stackimport::DisassemblePowerPCCodeResource(
				std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(resource->data.data()), resource->data.size()),
				0,
				resource->data.size(),
				0);
		}
		else
		{
			mResourceSummaries.push_back(summary);
			continue;
		}

		if(disassembly.ok)
		{
			const std::string typeAndArchitecture = summary.type + "_" + summary.architecture;
			const std::string relativePath = std::string("resource-disassembly/") + sanitized_resource_file_name(
				mFileName,
				typeAndArchitecture,
				summary.id,
				summary.name,
				".s");
			if(write_text_file(OutputPath(relativePath.c_str()), disassembly.text))
			{
				summary.status = "disassembled";
				summary.disassemblyFile = relativePath;
				stackimport_emit_infof( "Status: Wrote %s disassembly for '%s' #%d.\n", summary.architecture.c_str(), summary.type.c_str(), summary.id );
			}
			else
			{
				summary.status = "disassembly_write_failed";
				stackimport_emit_diagnosticf( "Warning: Could not write disassembly for '%s' #%d.\n", summary.type.c_str(), summary.id );
			}
		}
		else
		{
			summary.status = "disassembly_failed";
			stackimport_emit_diagnosticf( "Warning: Could not disassemble '%s' #%d: %s\n", summary.type.c_str(), summary.id, disassembly.error.c_str() );
		}
		mResourceSummaries.push_back(summary);
	}
	mResourceForkStatus = "ok";
#else
	mResourceForkStatus = "resource_dasm_unavailable";
#endif
	return true;
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

	JsonValue outputs(rapidjson::kArrayType);
	for(int n = 0; n < 40; n++)
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
	return write_json_file(stackPath, stackBuffer.GetString(), stackBuffer.GetSize());
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
	const uint64_t dataForkBytes = FileSizeBytes(fpath);
	std::ifstream		theFile( fpath.c_str(), std::ios::binary );
	if( !theFile.is_open() )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't open file '%s'\n", fpath.c_str() );
		return false;
	}
	
	std::string				packagePath( outputPackagePath );
	if( stackimport_internal_make_directory( packagePath.c_str() ) != 0 && errno != EEXIST )
	{
		stackimport_emit_diagnosticf( "Error: Couldn't create output package directory '%s'\n", packagePath.c_str() );
		return false;
	}
	
	mBasePath = packagePath;
		
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
	
	char		vBlockType[5] = { 0 };
	uint32_t	vBlockSize = 0;
	int32_t		vBlockID = 0;
	int			numBlocks = 0;
	bool		success = true;
	std::string	sourceStreamStatus("ok");
	uint64_t	sourceOffset = 0;
	
	// Read all blocks so we can random-access them. Yes, I know there are more
	//	efficient ways, but honestly, who cares?
	while( true )
	{
		memset( vBlockType, 0, sizeof(vBlockType) );
		theFile.read( reinterpret_cast<char*>(&vBlockSize), sizeof(vBlockSize) );
		const std::streamsize blockSizeBytesRead = theFile.gcount();
		if( blockSizeBytesRead == 0 && theFile.eof() )	// Couldn't read because we hit end of file.
			break;
		if( blockSizeBytesRead != static_cast<std::streamsize>(sizeof(vBlockSize)) )
		{
			stackimport_emit_diagnosticf( "Error: Could not read complete block size.\n" );
			sourceStreamStatus = "truncated_header";
			success = false;
			break;
		}
		
		vBlockSize = ReadBEUInt32Bytes(reinterpret_cast<const char*>(&vBlockSize));
		theFile.read( vBlockType, 4 );
		theFile.read( reinterpret_cast<char*>(&vBlockID), sizeof(vBlockID) );
		if( !theFile )
		{
			stackimport_emit_diagnosticf( "Error: Could not read complete block header.\n" );
			sourceStreamStatus = "truncated_header";
			success = false;
			break;
		}
		vBlockID = static_cast<int32_t>(ReadBEUInt32Bytes(reinterpret_cast<const char*>(&vBlockID)));
		
		numBlocks++;
		CSourceBlockSummary sourceBlock;
		sourceBlock.type = vBlockType;
		sourceBlock.id = vBlockID;
		sourceBlock.offset = sourceOffset;
		sourceBlock.size = vBlockSize;
		sourceBlock.payloadOffset = sourceOffset + 12;
		sourceBlock.payloadBytes = vBlockSize >= 12 ? vBlockSize - 12 : 0;
		sourceBlock.status = "ok";
		if( vBlockSize < 12 )
		{
			stackimport_emit_diagnosticf( "Error: Invalid block size %u for '%4s' #%d.\n", vBlockSize, vBlockType, vBlockID );
			sourceBlock.status = "invalid_size";
			mSourceBlocks.push_back(sourceBlock);
			sourceStreamStatus = "invalid_block_size";
			success = false;
			break;
		}
		
		if( strcmp(vBlockType,"TAIL") == 0 && vBlockID == -1 )	// End marker block?
		{
			mSourceBlocks.push_back(sourceBlock);
			sourceOffset += vBlockSize;
			break;
		}
		else if( strcmp(vBlockType,"FREE") == 0 )	// Not a free, reusable block?
		{
			if( mStatusMessages )
				stackimport_emit_infof( "Status: Skipping '%4s' #%d (%u bytes)\n", vBlockType, vBlockID, vBlockSize );
			theFile.ignore( vBlockSize -12 );	// Skip rest of block data.
			if( !theFile )
			{
				stackimport_emit_diagnosticf( "Error: Could not skip complete 'FREE' block #%d.\n", vBlockID );
				sourceBlock.status = "truncated_payload";
				mSourceBlocks.push_back(sourceBlock);
				sourceStreamStatus = "truncated_payload";
				success = false;
				break;
			}
			mSourceBlocks.push_back(sourceBlock);
		}
		else
		{
			CBuf		blockData( vBlockSize -12 );
			theFile.read( blockData.buf(0,vBlockSize -12), vBlockSize -12 );
			if( !theFile )
			{
				stackimport_emit_diagnosticf( "Error: Could not read complete '%4s' block #%d.\n", vBlockType, vBlockID );
				sourceBlock.status = "truncated_payload";
				mSourceBlocks.push_back(sourceBlock);
				sourceStreamStatus = "truncated_payload";
				success = false;
				break;
			}
			CStackBlockIdentifier	theTypeAndID(vBlockType,vBlockID);
			mBlockMap[theTypeAndID] = blockData;
			mSourceBlocks.push_back(sourceBlock);
	//			if( mStatusMessages )
	//				stackimport_emit_infof( "Status: Located block %s %d - (%lu)\n", vBlockType, vBlockID, mBlockMap.size() );
		}
		sourceOffset += vBlockSize;
	}
	
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
					
					FILE*	rawFile = fopen( OutputPath(fname).c_str(), "w" );
					if( rawFile )
					{
						if( blockData.size() != fwrite( blockData.buf(), 1, blockData.size(), rawFile ) )
							stackimport_emit_diagnosticf( "Error: Writing un-decoded BMAP #%u.\n", blockID );
						fclose( rawFile );
					}
					else
						stackimport_emit_diagnosticf( "Error: Creating file for un-decoded BMAP #%u.\n", blockID );
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
