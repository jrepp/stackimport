/*
 *  Tests.cpp
 *  stackimport
 *
 *  Created by Mr. Z. on 10/06/06.
 *  Copyright 2006 Mr Z. All rights reserved.
 *
 */

#include "CStackFile.h"
#include "Mac68kDisassembly.h"
#include "StackImportSoundConverter.h"
#include "stackimport_c.h"
#include "stackimport_platform_internal.h"
#include "stackimport_rapidjson_allocator.h"
#include <assert.h>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <span>
#include <sys/stat.h>
#if defined(_WIN32)
#include <malloc.h>
#include <direct.h>
#endif

bool stackimport_load_resource_fork(
	const std::string& fpath,
	const std::string& basePath,
	const std::string& stackFileName,
	stackimport::IResourceOutput* resourceOutput,
	std::vector<CResourceSummary>& resourceSummaries,
	std::string& resourceForkStatus,
	uint64_t& resourceForkBytes);

namespace {

void* STACKIMPORT_CALL test_allocate(size_t size, size_t alignment, void*)
{
	void* ptr = nullptr;
#if defined(_WIN32)
	if(alignment < sizeof(void*))
		alignment = sizeof(void*);
	ptr = _aligned_malloc(size, alignment);
	return ptr;
#else
	if(posix_memalign(&ptr, alignment, size) != 0)
		return nullptr;
	return ptr;
#endif
}

void STACKIMPORT_CALL test_deallocate(void* ptr, void*)
{
#if defined(_WIN32)
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

int STACKIMPORT_CALL test_resource_wants(const stackimport_resource_payload* payload, void*)
{
	return payload && payload->format == STACKIMPORT_RESOURCE_PAYLOAD_NATIVE;
}

int STACKIMPORT_CALL test_resource_payload(const stackimport_resource_payload* payload, const void* data, size_t size, void*)
{
	return payload && payload->payload_size == size && (size == 0 || data != nullptr);
}

struct CountingPlatformState {
	int opens = 0;
	int reads = 0;
	int writes = 0;
	int closes = 0;
	size_t allocations = 0;
	size_t fail_after_allocations = SIZE_MAX;
	bool fail_writes = false;
	bool fail_closes = false;
};

struct ResourceForkPlatformState : CountingPlatformState {
	const uint8_t* resource_fork_data = nullptr;
	size_t resource_fork_size = 0;
	size_t resource_fork_pos = 0;
};

void* STACKIMPORT_CALL counting_allocate(size_t size, size_t alignment, void* user_data)
{
	auto* state = static_cast<CountingPlatformState*>(user_data);
	state->allocations++;
	if(state->allocations > state->fail_after_allocations)
		return nullptr;
	return test_allocate(size, alignment, nullptr);
}

void STACKIMPORT_CALL counting_deallocate(void* ptr, void*)
{
	test_deallocate(ptr, nullptr);
}

stackimport_file_handle STACKIMPORT_CALL counting_open_file(const char* path, const char* mode, void* user_data)
{
	auto* state = static_cast<CountingPlatformState*>(user_data);
	state->opens++;
	return std::fopen(path, mode);
}

size_t STACKIMPORT_CALL counting_read_file(stackimport_file_handle file, void* data, size_t size, void* user_data)
{
	auto* state = static_cast<CountingPlatformState*>(user_data);
	state->reads++;
	return std::fread(data, 1, size, static_cast<FILE*>(file));
}

size_t STACKIMPORT_CALL counting_write_file(stackimport_file_handle file, const void* data, size_t size, void* user_data)
{
	auto* state = static_cast<CountingPlatformState*>(user_data);
	state->writes++;
	if(state->fail_writes)
		return 0;
	return std::fwrite(data, 1, size, static_cast<FILE*>(file));
}

int STACKIMPORT_CALL counting_close_file(stackimport_file_handle file, void* user_data)
{
	auto* state = static_cast<CountingPlatformState*>(user_data);
	state->closes++;
	const int result = std::fclose(static_cast<FILE*>(file));
	return state->fail_closes ? -1 : result;
}

int STACKIMPORT_CALL counting_make_directory(const char* path, void*)
{
#if defined(_WIN32)
	const int result = _mkdir(path);
#else
	const int result = mkdir(path, 0777);
#endif
	return result == 0 || errno == EEXIST ? 0 : result;
}

bool has_resource_fork_suffix(const char* path)
{
	const std::string text(path ? path : "");
	const std::string suffix = "/..namedfork/rsrc";
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

stackimport_file_handle STACKIMPORT_CALL resource_fork_open_file(const char* path, const char* mode, void* user_data)
{
	auto* state = static_cast<ResourceForkPlatformState*>(user_data);
	state->opens++;
	if(mode && std::strcmp(mode, "rb") == 0 && has_resource_fork_suffix(path))
	{
		state->resource_fork_pos = 0;
		return state;
	}
	return std::fopen(path, mode);
}

size_t STACKIMPORT_CALL resource_fork_read_file(stackimport_file_handle file, void* data, size_t size, void* user_data)
{
	auto* state = static_cast<ResourceForkPlatformState*>(user_data);
	state->reads++;
	if(file == state)
	{
		const size_t available = state->resource_fork_pos < state->resource_fork_size ? state->resource_fork_size - state->resource_fork_pos : 0;
		const size_t count = size < available ? size : available;
		if(count > 0)
			std::memcpy(data, state->resource_fork_data + state->resource_fork_pos, count);
		state->resource_fork_pos += count;
		return count;
	}
	return std::fread(data, 1, size, static_cast<FILE*>(file));
}

size_t STACKIMPORT_CALL resource_fork_write_file(stackimport_file_handle file, const void* data, size_t size, void* user_data)
{
	auto* state = static_cast<ResourceForkPlatformState*>(user_data);
	state->writes++;
	return std::fwrite(data, 1, size, static_cast<FILE*>(file));
}

int STACKIMPORT_CALL resource_fork_close_file(stackimport_file_handle file, void* user_data)
{
	auto* state = static_cast<ResourceForkPlatformState*>(user_data);
	state->closes++;
	if(file == state)
		return 0;
	return std::fclose(static_cast<FILE*>(file));
}

class TestResourceOutput final : public stackimport::IResourceOutput {
public:
	auto wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		return payload.format == stackimport::ResourcePayloadFormat::Native;
	}

	auto on_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		seen_native = payload.format == stackimport::ResourcePayloadFormat::Native;
		return true;
	}

	bool seen_native = false;
};

class CountingResourceOutput final : public stackimport::IResourceOutput {
public:
	auto wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		wants_count++;
		return payload.format == stackimport::ResourcePayloadFormat::Native ||
			payload.format == stackimport::ResourcePayloadFormat::Rgba32 ||
			payload.format == stackimport::ResourcePayloadFormat::JsonUtf8 ||
			payload.format == stackimport::ResourcePayloadFormat::TextUtf8;
	}

	auto on_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		if(payload.format == stackimport::ResourcePayloadFormat::Native)
			native_count++;
		if(payload.format == stackimport::ResourcePayloadFormat::Rgba32)
		{
			rgba_count++;
			last_width = payload.width;
			last_height = payload.height;
			last_payload_size = payload.data.size;
		}
		if(payload.format == stackimport::ResourcePayloadFormat::JsonUtf8)
		{
			json_count++;
			last_json.assign(reinterpret_cast<const char*>(payload.data.data), payload.data.size);
		}
		if(payload.format == stackimport::ResourcePayloadFormat::TextUtf8)
		{
			text_count++;
			last_text.assign(reinterpret_cast<const char*>(payload.data.data), payload.data.size);
		}
		return true;
	}

	int wants_count = 0;
	int native_count = 0;
	int rgba_count = 0;
	int json_count = 0;
	int text_count = 0;
	uint32_t last_width = 0;
	uint32_t last_height = 0;
	size_t last_payload_size = 0;
	std::string last_json;
	std::string last_text;
};

void write_basic_resource_fork_header(uint8_t* fork, uint32_t data_off, uint32_t map_off, uint32_t data_len, uint32_t map_len)
{
	rsrcd::write_u32be(fork + 0, data_off);
	rsrcd::write_u32be(fork + 4, map_off);
	rsrcd::write_u32be(fork + 8, data_len);
	rsrcd::write_u32be(fork + 12, map_len);
}

void append_u16be(std::vector<uint8_t>& data, uint16_t value)
{
	data.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
	data.push_back(static_cast<uint8_t>(value & 0xFFu));
}

void append_u32be(std::vector<uint8_t>& data, uint32_t value)
{
	data.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
	data.push_back(static_cast<uint8_t>(value & 0xFFu));
}

std::vector<uint8_t> make_snd_format2_fixture(bool extraNullCommand, uint32_t commandOffset)
{
	std::vector<uint8_t> snd;
	append_u16be(snd, 2);
	append_u16be(snd, 0);
	append_u16be(snd, extraNullCommand ? 2 : 1);
	if(extraNullCommand)
	{
		append_u16be(snd, 0);
		append_u16be(snd, 0x5354);
		append_u32be(snd, 0);
	}
	append_u16be(snd, 0x8051);
	append_u16be(snd, 0);
	append_u32be(snd, commandOffset);
	append_u32be(snd, 0);
	append_u32be(snd, 4);
	append_u32be(snd, 22050u << 16u);
	append_u32be(snd, 0);
	append_u32be(snd, 4);
	snd.push_back(0);
	snd.push_back(60);
	snd.insert(snd.end(), {0x80, 0x81, 0x82, 0x83});
	return snd;
}

std::vector<uint8_t> make_single_resource_fork(const char type[4], int16_t id, const std::vector<uint8_t>& payload)
{
	const uint32_t data_off = 16;
	const uint32_t data_len = static_cast<uint32_t>(4 + payload.size());
	const uint32_t map_off = data_off + data_len;
	const uint32_t map_len = 96;
	std::vector<uint8_t> fork(map_off + map_len);
	write_basic_resource_fork_header(fork.data(), data_off, map_off, data_len, map_len);
	rsrcd::write_u32be(fork.data() + data_off, static_cast<uint32_t>(payload.size()));
	if(!payload.empty())
		std::memcpy(fork.data() + data_off + 4, payload.data(), payload.size());

	uint8_t* map = fork.data() + map_off;
	rsrcd::write_u16be(map + 24, 28);
	rsrcd::write_u16be(map + 26, 64);
	uint8_t* type_list = map + 28;
	rsrcd::write_u16be(type_list, 0);
	std::memcpy(type_list + 2, type, 4);
	rsrcd::write_u16be(type_list + 6, 0);
	rsrcd::write_u16be(type_list + 8, 10);
	uint8_t* ref = type_list + 10;
	rsrcd::write_u16be(ref + 0, static_cast<uint16_t>(id));
	rsrcd::write_u16be(ref + 2, 0xFFFF);
	rsrcd::write_u32be(ref + 4, 0);
	rsrcd::write_u32be(ref + 8, 0);
	return fork;
}

void append_block_header(std::vector<uint8_t>& data, uint32_t size, const char type[4], int32_t id)
{
	data.push_back(static_cast<uint8_t>((size >> 24u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((size >> 16u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((size >> 8u) & 0xFFu));
	data.push_back(static_cast<uint8_t>(size & 0xFFu));
	data.insert(data.end(), type, type + 4);
	const uint32_t unsignedId = static_cast<uint32_t>(id);
	data.push_back(static_cast<uint8_t>((unsignedId >> 24u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((unsignedId >> 16u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((unsignedId >> 8u) & 0xFFu));
	data.push_back(static_cast<uint8_t>(unsignedId & 0xFFu));
}

class MemoryStackReader final : public stackimport::IStackReader {
public:
	explicit MemoryStackReader(rsrcd::Bytes bytes) : bytes_(bytes), pos_(0) {}

	auto read(uint8_t* dst, size_t len) -> size_t override
	{
		const size_t available = pos_ < bytes_.size ? bytes_.size - pos_ : 0;
		const size_t count = len < available ? len : available;
		if(count > 0)
			std::memcpy(dst, bytes_.data + pos_, count);
		pos_ += count;
		return count;
	}

	auto seek(size_t pos) -> bool override
	{
		if(pos > bytes_.size)
			return false;
		pos_ = pos;
		return true;
	}

	auto position() const -> size_t override { return pos_; }
	auto size() const -> size_t override { return bytes_.size; }

private:
	rsrcd::Bytes bytes_;
	size_t pos_;
};

class CapturingBlockOutput final : public stackimport::IBlockOutput {
public:
	auto on_block(const stackimport::BlockRef&, stackimport::IStackReader&) -> BlockResult override
	{
		block_count++;
		return BlockResult::Continue;
	}

	auto on_error(const char* msg) -> bool override
	{
		error = msg ? msg : "";
		return false;
	}

	int block_count = 0;
	std::string error;
};

void write_minimal_short_stak(const std::string& path)
{
	std::vector<uint8_t> data(12 + 68 + 12);
	rsrcd::write_u32be(data.data(), 12 + 68);
	std::memcpy(data.data() + 4, "STAK", 4);
	rsrcd::write_u32be(data.data() + 8, 0xFFFFFFFFu);
	const size_t tail_offset = 12 + 68;
	rsrcd::write_u32be(data.data() + tail_offset, 12);
	std::memcpy(data.data() + tail_offset + 4, "TAIL", 4);
	rsrcd::write_u32be(data.data() + tail_offset + 8, 0xFFFFFFFFu);

	std::ofstream file(path.c_str(), std::ios::binary);
	file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	assert(file);
}

void write_minimal_free_then_short_stak(const std::string& path)
{
	std::vector<uint8_t> data;
	append_block_header(data, 16, "FREE", 0);
	data.insert(data.end(), {0, 1, 2, 3});
	append_block_header(data, 12 + 68, "STAK", -1);
	data.resize(data.size() + 68);
	append_block_header(data, 12, "TAIL", -1);

	std::ofstream file(path.c_str(), std::ios::binary);
	file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	assert(file);
}

std::string read_text_file(const std::string& path)
{
	std::ifstream file(path.c_str(), std::ios::binary);
	assert(file.is_open());
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

}


void	RunTests()
{
	CStackBlockIdentifier		a( "TEST", 1 );
	CStackBlockIdentifier		b( "TEST", 2 );
	CStackBlockIdentifier		wildcardYes( "TEST" );
	CStackBlockIdentifier		wildcardNo( "TOON" );
	
	// ==
	assert((a == b) == false);
	
	assert(a == wildcardYes);
	assert(wildcardYes == a);
	
	assert(b == wildcardYes);
	assert(b == wildcardYes);
	
	assert((a == wildcardNo) == false);
	assert((wildcardNo == a) == false);
	
	assert((b == wildcardNo) == false);
	assert((wildcardNo == b) == false);
	
	// <
	assert( a < b );
	
	assert( (a < wildcardYes) == false );
	assert( (wildcardYes < a) == false );
	
	assert( (b < wildcardYes) == false );
	assert( (wildcardYes < b) == false );
	
	assert( a < wildcardNo );
	assert( (wildcardNo < a) == false );
	
	assert( b < wildcardNo );
	assert( (wildcardNo < b) == false );
	
	// >
	assert( (a > b) == false );
	
	assert( (a > wildcardYes) == false );
	assert( (wildcardYes > a) == false );
	
	assert( (b > wildcardYes) == false );
	assert( (wildcardYes > b) == false );
	
	assert( (a > wildcardNo) == false );
	assert( wildcardNo > a );
	
	assert( (b > wildcardNo) == false );
	assert( wildcardNo > b );

	assert(stackimport_context_size() <= 4096);
	assert(stackimport_api_version() == STACKIMPORT_API_VERSION);
	alignas(std::max_align_t) unsigned char storage[4096] = {};
	stackimport_context* context = nullptr;
	assert(stackimport_context_init(storage, sizeof(storage), &context) == STACKIMPORT_STATUS_OK);
	stackimport_context_deinit(context);

	stackimport_allocator allocator = {};
	stackimport_allocator_init(&allocator);
	allocator.allocate = test_allocate;
	allocator.deallocate = test_deallocate;
	assert(stackimport_context_create(&allocator, &context) == STACKIMPORT_STATUS_OK);
	stackimport_import_options options = {};
	stackimport_import_options_init(&options);
	assert(options.resource_payload_flags == STACKIMPORT_RESOURCE_PAYLOADS_ALL);
	options.resource_wants = test_resource_wants;
	options.resource_payload = test_resource_payload;
	assert(static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_RGBA32) == 1u);
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_INVALID_ARGUMENT);
	options.input_path = "/tmp/missing-stack";
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_INVALID_ARGUMENT);
	options.output_package_path = "/tmp/missing-stack.xstk";
	options.flags = 1u << 31;
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_UNSUPPORTED_OPTION);
	stackimport_context_destroy(context);

	std::vector<uint8_t> truncatedBlock;
	append_block_header(truncatedBlock, 16, "STAK", -1);
	truncatedBlock.insert(truncatedBlock.end(), {0xAA, 0xBB});
	CapturingBlockOutput streamingCapture;
	MemoryStackReader truncatedReader(rsrcd::Bytes{truncatedBlock.data(), truncatedBlock.size()});
	assert(stackimport::BlockParser{}.parse(truncatedReader, streamingCapture) == stackimport::BlockErr::TruncatedPayload);
	assert(streamingCapture.block_count == 0);
	assert(streamingCapture.error == "truncated payload");

	CapturingBlockOutput viewCapture;
	assert(stackimport::BlockParser{}.parse_view(rsrcd::Bytes{truncatedBlock.data(), truncatedBlock.size()}, viewCapture) == stackimport::BlockErr::TruncatedPayload);
	assert(viewCapture.block_count == 0);
	assert(viewCapture.error == "truncated payload");

	const uint8_t codeBytes[] = {0x4E, 0x75, 0xA9, 0xF0, 0x12};
	TestResourceOutput resourceOutput;
	stackimport::ResourcePayload payload = {};
	payload.format = stackimport::ResourcePayloadFormat::Native;
	payload.data = rsrcd::Bytes{codeBytes, sizeof(codeBytes)};
	assert(resourceOutput.wants_resource_payload(payload));
	assert(resourceOutput.on_resource_payload(payload));
	assert(resourceOutput.seen_native);

	uint8_t fork[256] = {};
	write_basic_resource_fork_header(fork, 16, 160, 132, 96);
	rsrcd::write_u32be(fork + 16, 128);
	for(size_t i = 0; i < 128; ++i)
		fork[20 + i] = 0xFF;
	uint8_t* map = fork + 160;
	rsrcd::write_u16be(map + 24, 28);
	rsrcd::write_u16be(map + 26, 64);
	uint8_t* type_list = map + 28;
	rsrcd::write_u16be(type_list, 0);
	std::memcpy(type_list + 2, "ICON", 4);
	rsrcd::write_u16be(type_list + 6, 0);
	rsrcd::write_u16be(type_list + 8, 10);
	uint8_t* ref = type_list + 10;
	rsrcd::write_u16be(ref + 0, 128);
	rsrcd::write_u16be(ref + 2, 0xFFFF);
	rsrcd::write_u32be(ref + 4, 0);
	rsrcd::write_u32be(ref + 8, 0);
	CountingResourceOutput countingOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{fork, sizeof(fork)}, countingOutput));
	assert(countingOutput.native_count == 1);
	assert(countingOutput.rgba_count == 1);
	assert(countingOutput.last_width == 32);
	assert(countingOutput.last_height == 32);
	assert(countingOutput.last_payload_size == 32u * 32u * 4u);

	std::vector<uint8_t> icnPayload(256);
	icnPayload[0] = 0x80;
	icnPayload[128] = 0x80;
	const std::vector<uint8_t> icnFork = make_single_resource_fork("ICN#", 130, icnPayload);
	CountingResourceOutput icnOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{icnFork.data(), icnFork.size()}, icnOutput));
	assert(icnOutput.native_count == 1);
	assert(icnOutput.rgba_count == 1);
	assert(icnOutput.last_width == 32);
	assert(icnOutput.last_height == 32);
	assert(icnOutput.last_payload_size == 32u * 32u * 4u);

	const std::vector<uint8_t> patPayload = {0x80, 0, 0, 0, 0, 0, 0, 0};
	const std::vector<uint8_t> patFork = make_single_resource_fork("PAT ", 9, patPayload);
	CountingResourceOutput patOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{patFork.data(), patFork.size()}, patOutput));
	assert(patOutput.native_count == 1);
	assert(patOutput.rgba_count == 1);
	assert(patOutput.last_width == 8);
	assert(patOutput.last_height == 8);
	assert(patOutput.last_payload_size == 8u * 8u * 4u);

	const std::string resourceForkRoot = std::string("/tmp/stackimport-rsrc-root-") + std::to_string(std::rand());
	const std::string resourceForkOutput = std::string("/tmp/stackimport-rsrc-output-") + std::to_string(std::rand());
	assert(counting_make_directory(resourceForkOutput.c_str(), nullptr) == 0);
	ResourceForkPlatformState resourceForkPlatformState;
	resourceForkPlatformState.resource_fork_data = fork;
	resourceForkPlatformState.resource_fork_size = sizeof(fork);
	stackimport_platform resourceForkPlatform = {};
	stackimport_platform_init(&resourceForkPlatform);
	resourceForkPlatform.open_file = resource_fork_open_file;
	resourceForkPlatform.read_file = resource_fork_read_file;
	resourceForkPlatform.write_file = resource_fork_write_file;
	resourceForkPlatform.close_file = resource_fork_close_file;
	resourceForkPlatform.make_directory = counting_make_directory;
	resourceForkPlatform.user_data = &resourceForkPlatformState;
	const stackimport_internal_platform resourceForkInternalPlatform = stackimport_internal_platform_from_api(&resourceForkPlatform);
	CountingResourceOutput packageResourceOutput;
	std::vector<CResourceSummary> resourceSummaries;
	std::string resourceForkStatus;
	uint64_t resourceForkBytes = 0;
	{
		stackimport_platform_scope resourceForkScope(resourceForkInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			resourceForkOutput,
			"Stack",
			&packageResourceOutput,
			resourceSummaries,
			resourceForkStatus,
			resourceForkBytes));
	}
	assert(resourceForkStatus == "ok");
	assert(resourceForkBytes == sizeof(fork));
	assert(resourceSummaries.size() == 1);
	assert(resourceSummaries[0].type == "ICON");
	assert(resourceSummaries[0].status == "exported");
	assert(packageResourceOutput.native_count == 1);
	assert(packageResourceOutput.rgba_count == 1);
	assert(resourceForkPlatformState.opens > 0);
	assert(resourceForkPlatformState.reads > 0);
	assert(resourceForkPlatformState.writes > 0);

	std::vector<uint8_t> addColorData;
	addColorData.push_back(0x03);
	append_u16be(addColorData, 10);
	append_u16be(addColorData, 20);
	append_u16be(addColorData, 30);
	append_u16be(addColorData, 40);
	append_u16be(addColorData, 2);
	append_u16be(addColorData, 0x1111);
	append_u16be(addColorData, 0x2222);
	append_u16be(addColorData, 0x3333);
	const std::vector<uint8_t> addColorFork = make_single_resource_fork("HCbg", 77, addColorData);
	CountingResourceOutput addColorOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{addColorFork.data(), addColorFork.size()}, addColorOutput));
	assert(addColorOutput.native_count == 1);
	assert(addColorOutput.json_count == 1);
	assert(addColorOutput.last_json.find("\"targetKind\": \"background\"") != std::string::npos);
	assert(addColorOutput.last_json.find("\"targetId\": 77") != std::string::npos);
	assert(addColorOutput.last_json.find("\"type\": \"rect\"") != std::string::npos);
	assert(addColorOutput.last_json.find("\"red\": 4369") != std::string::npos);

	const std::string addColorOutputPath = std::string("/tmp/stackimport-addcolor-output-") + std::to_string(std::rand());
	assert(counting_make_directory(addColorOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState addColorPlatformState;
	addColorPlatformState.resource_fork_data = addColorFork.data();
	addColorPlatformState.resource_fork_size = addColorFork.size();
	stackimport_platform addColorPlatform = resourceForkPlatform;
	addColorPlatform.user_data = &addColorPlatformState;
	const stackimport_internal_platform addColorInternalPlatform = stackimport_internal_platform_from_api(&addColorPlatform);
	std::vector<CResourceSummary> addColorSummaries;
	std::string addColorStatus;
	uint64_t addColorBytes = 0;
	{
		stackimport_platform_scope addColorScope(addColorInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			addColorOutputPath,
			"Stack",
			&addColorOutput,
			addColorSummaries,
			addColorStatus,
			addColorBytes));
	}
	assert(addColorStatus == "ok");
	assert(addColorSummaries.size() == 1);
	assert(addColorSummaries[0].type == "HCbg");
	assert(addColorSummaries[0].status == "exported");
	assert(addColorSummaries[0].outputFile == "HCbg_77.json");
	const std::string addColorJson = read_text_file(addColorOutputPath + "/HCbg_77.json");
	assert(addColorJson.find("\"targetKind\": \"background\"") != std::string::npos);

	const std::vector<uint8_t> strPayload = {5, 'H', 0x8E, 'l', 'l', 'o'};
	const std::vector<uint8_t> strFork = make_single_resource_fork("STR ", 12, strPayload);
	CountingResourceOutput textOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{strFork.data(), strFork.size()}, textOutput));
	assert(textOutput.native_count == 1);
	assert(textOutput.text_count == 1);
	assert(textOutput.last_text == "H\xC3\xA9llo");

	std::vector<uint8_t> stringListPayload;
	append_u16be(stringListPayload, 2);
	stringListPayload.insert(stringListPayload.end(), {3, 'O', 'n', 'e'});
	stringListPayload.insert(stringListPayload.end(), {3, 'T', 'w', 'o'});
	const std::vector<uint8_t> stringListFork = make_single_resource_fork("STR#", 44, stringListPayload);
	CountingResourceOutput stringListOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{stringListFork.data(), stringListFork.size()}, stringListOutput));
	assert(stringListOutput.native_count == 1);
	assert(stringListOutput.json_count == 1);
	assert(stringListOutput.last_json.find("\"One\"") != std::string::npos);
	assert(stringListOutput.last_json.find("\"Two\"") != std::string::npos);

	std::vector<uint8_t> versionPayload = {0x01, 0x23, 0x80, 0x00, 0x00, 0x00};
	versionPayload.insert(versionPayload.end(), {3, '1', '.', '2'});
	versionPayload.insert(versionPayload.end(), {7, 'R', 'e', 'l', 'e', 'a', 's', 'e'});
	const std::vector<uint8_t> versionFork = make_single_resource_fork("vers", 1, versionPayload);
	CountingResourceOutput versionOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{versionFork.data(), versionFork.size()}, versionOutput));
	assert(versionOutput.native_count == 1);
	assert(versionOutput.json_count == 1);
	assert(versionOutput.last_json.find("\"majorRevision\": 1") != std::string::npos);
	assert(versionOutput.last_json.find("\"minorAndBugRevision\": 35") != std::string::npos);
	assert(versionOutput.last_json.find("\"shortVersion\": \"1.2\"") != std::string::npos);

	std::vector<uint8_t> colorTablePayload;
	append_u32be(colorTablePayload, 0x12345678);
	append_u16be(colorTablePayload, 0x8000);
	append_u16be(colorTablePayload, 0);
	append_u16be(colorTablePayload, 7);
	append_u16be(colorTablePayload, 0x1111);
	append_u16be(colorTablePayload, 0x2222);
	append_u16be(colorTablePayload, 0x3333);
	const std::vector<uint8_t> colorTableFork = make_single_resource_fork("clut", 8, colorTablePayload);
	CountingResourceOutput colorTableOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{colorTableFork.data(), colorTableFork.size()}, colorTableOutput));
	assert(colorTableOutput.native_count == 1);
	assert(colorTableOutput.json_count == 1);
	assert(colorTableOutput.last_json.find("\"seed\": 305419896") != std::string::npos);
	assert(colorTableOutput.last_json.find("\"flags\": 32768") != std::string::npos);
	assert(colorTableOutput.last_json.find("\"green\": 8738") != std::string::npos);

	std::vector<uint8_t> sizePayload;
	append_u16be(sizePayload, 0xD048);
	append_u32be(sizePayload, 0x00100000);
	append_u32be(sizePayload, 0x00080000);
	const std::vector<uint8_t> sizeFork = make_single_resource_fork("SIZE", -1, sizePayload);
	CountingResourceOutput sizeOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{sizeFork.data(), sizeFork.size()}, sizeOutput));
	assert(sizeOutput.native_count == 1);
	assert(sizeOutput.json_count == 1);
	assert(sizeOutput.last_json.find("\"preferredSize\": 1048576") != std::string::npos);
	assert(sizeOutput.last_json.find("\"minimumSize\": 524288") != std::string::npos);
	assert(sizeOutput.last_json.find("\"saveScreen\": true") != std::string::npos);

	const std::string textOutputPath = std::string("/tmp/stackimport-text-output-") + std::to_string(std::rand());
	assert(counting_make_directory(textOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState textPlatformState;
	textPlatformState.resource_fork_data = strFork.data();
	textPlatformState.resource_fork_size = strFork.size();
	stackimport_platform textPlatform = resourceForkPlatform;
	textPlatform.user_data = &textPlatformState;
	const stackimport_internal_platform textInternalPlatform = stackimport_internal_platform_from_api(&textPlatform);
	std::vector<CResourceSummary> textSummaries;
	std::string textStatus;
	uint64_t textBytes = 0;
	{
		stackimport_platform_scope textScope(textInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			textOutputPath,
			"Stack",
			&textOutput,
			textSummaries,
			textStatus,
			textBytes));
	}
	assert(textStatus == "ok");
	assert(textSummaries.size() == 1);
	assert(textSummaries[0].type == "STR ");
	assert(textSummaries[0].status == "exported");
	assert(textSummaries[0].outputFile == "resource-text/Stack_STR%20_12.txt");
	const std::string convertedText = read_text_file(textOutputPath + "/resource-text/Stack_STR%20_12.txt");
	assert(convertedText == "H\xC3\xA9llo");

	stackimport::PlatformByteVector wavData;
	std::string soundError;
	const std::vector<uint8_t> multiCommandSnd = make_snd_format2_fixture(true, 22);
	assert(stackimport::ConvertSndResourceToWav(rsrcd::Bytes{multiCommandSnd.data(), multiCommandSnd.size()}, wavData, soundError));
	assert(wavData.size() == 48);
	assert(std::memcmp(wavData.data(), "RIFF", 4) == 0);
	assert(wavData[44] == 0x80);
	assert(wavData[47] == 0x83);
	uint8_t wavBuffer[64] = {};
	const char* cSoundError = nullptr;
	const size_t cWavSize = stackimport_convert_snd_to_wav(
		multiCommandSnd.data(),
		multiCommandSnd.size(),
		wavBuffer,
		sizeof(wavBuffer),
		&cSoundError);
	assert(cWavSize == 48);
	assert(cSoundError == nullptr);
	assert(std::memcmp(wavBuffer, "RIFF", 4) == 0);
	assert(wavBuffer[44] == 0x80);
	assert(wavBuffer[47] == 0x83);
	assert(stackimport_convert_snd_to_wav(
		multiCommandSnd.data(),
		multiCommandSnd.size(),
		wavBuffer,
		8,
		&cSoundError) == 0);
	assert(std::strcmp(cSoundError, "output buffer too small") == 0);
	assert(stackimport_convert_snd_to_wav(nullptr, 0, nullptr, 0, nullptr) == 0);

	const std::vector<uint8_t> badOffsetSnd = make_snd_format2_fixture(false, 20);
	wavData.clear();
	soundError.clear();
	assert(stackimport::ConvertSndResourceToWav(rsrcd::Bytes{badOffsetSnd.data(), badOffsetSnd.size()}, wavData, soundError));
	assert(wavData.size() == 48);
	assert(std::memcmp(wavData.data(), "RIFF", 4) == 0);
	assert(wavData[44] == 0x80);
	assert(wavData[47] == 0x83);

	CountingPlatformState failingSoundState;
	failingSoundState.fail_after_allocations = 0;
	stackimport_platform failingSoundPlatform = {};
	stackimport_platform_init(&failingSoundPlatform);
	failingSoundPlatform.allocate = counting_allocate;
	failingSoundPlatform.deallocate = counting_deallocate;
	failingSoundPlatform.user_data = &failingSoundState;
	stackimport_internal_platform failingSoundInternal = stackimport_internal_platform_from_api(&failingSoundPlatform);
	{
		stackimport_platform_scope failingSoundScope(failingSoundInternal);
		stackimport::PlatformByteVector failingWavData;
		soundError.clear();
		assert(!stackimport::ConvertSndResourceToWav(rsrcd::Bytes{multiCommandSnd.data(), multiCommandSnd.size()}, failingWavData, soundError));
		assert(soundError == "allocation failed");
	}

	const std::span<const uint8_t> code(codeBytes, sizeof(codeBytes));
	const auto fullDisassembly = stackimport::DisassembleMac68kCodeResource(code, 0, 4, 0x1000);
	assert(fullDisassembly.ok);
	assert(fullDisassembly.text.find("rts") != std::string::npos || fullDisassembly.text.find("dc.w $4E75") != std::string::npos);

	const auto slicedDisassembly = stackimport::DisassembleMac68kCodeResource(code, 2, 3, 0x200);
	assert(slicedDisassembly.ok);
	assert(slicedDisassembly.text.find("A9F0") != std::string::npos || slicedDisassembly.text.find("_SysBeep") != std::string::npos);

	const auto badStart = stackimport::DisassembleMac68kCodeResource(code, 6, 0, 0);
	assert(!badStart.ok);
	const auto badSize = stackimport::DisassembleMac68kCodeResource(code, 4, 2, 0);
	assert(!badSize.ok);

	const std::string shortStackPath = std::string("/tmp/stackimport-short-stak-") + std::to_string(std::rand()) + ".stk";
	const std::string shortStackPackage = shortStackPath + ".xstk";
	write_minimal_short_stak(shortStackPath);
	CStackFile shortStack;
	shortStack.SetStatusMessages(false);
	shortStack.SetProgressMessages(false);
	assert(shortStack.LoadFile(shortStackPath, shortStackPackage));
	const std::string projectJson = read_text_file(shortStackPackage + "/project.json");
	const std::string stackJson = read_text_file(shortStackPackage + "/stack_-1.json");
	const std::string manifestJson = read_text_file(shortStackPackage + "/source-manifest.json");
	assert(projectJson.find("\"kind\": \"pattern\"") == std::string::npos);
	assert(projectJson.find("\"patternCount\": 0") != std::string::npos);
	assert(stackJson.find("\"patternCount\": 0") != std::string::npos);
	assert(manifestJson.find("\"typeCode\":") != std::string::npos);

	const std::string freeStackPath = std::string("/tmp/stackimport-free-before-stak-") + std::to_string(std::rand()) + ".stk";
	const std::string freeStackPackage = freeStackPath + ".xstk";
	write_minimal_free_then_short_stak(freeStackPath);
	CStackFile freeStack;
	freeStack.SetStatusMessages(false);
	freeStack.SetProgressMessages(false);
	assert(freeStack.LoadFile(freeStackPath, freeStackPackage));
	const std::string freeManifestJson = read_text_file(freeStackPackage + "/source-manifest.json");
	assert(freeManifestJson.find("\"type\": \"FREE\"") != std::string::npos);
	assert(freeManifestJson.find("\"status\": \"skipped\"") != std::string::npos);
	assert(freeManifestJson.find("\"type\": \"TAIL\"") != std::string::npos);
	assert(freeManifestJson.find("\"status\": \"terminal\"") != std::string::npos);
	assert(freeManifestJson.find("\"offset\": 16") != std::string::npos);

	const std::string platformStackPath = std::string("/tmp/stackimport-platform-read-") + std::to_string(std::rand()) + ".stk";
	const std::string platformStackPackage = platformStackPath + ".xstk";
	write_minimal_short_stak(platformStackPath);
	CountingPlatformState platformState;
	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	platform.open_file = counting_open_file;
	platform.read_file = counting_read_file;
	platform.write_file = counting_write_file;
	platform.close_file = counting_close_file;
	platform.make_directory = counting_make_directory;
	platform.user_data = &platformState;
	stackimport_context* platformContext = nullptr;
	assert(stackimport_context_create_with_platform(&platform, &platformContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options platformOptions = {};
	stackimport_import_options_init(&platformOptions);
	platformOptions.input_path = platformStackPath.c_str();
	platformOptions.output_package_path = platformStackPackage.c_str();
	assert(stackimport_import(platformContext, &platformOptions) == STACKIMPORT_STATUS_OK);
	stackimport_context_destroy(platformContext);
	assert(platformState.opens > 0);
	assert(platformState.reads > 0);
	assert(platformState.writes > 0);

	const std::string failingWritePackage = platformStackPath + ".write-failed.xstk";
	CountingPlatformState failingWriteState;
	failingWriteState.fail_writes = true;
	stackimport_platform failingWritePlatform = platform;
	failingWritePlatform.user_data = &failingWriteState;
	stackimport_context* failingWriteContext = nullptr;
	assert(stackimport_context_create_with_platform(&failingWritePlatform, &failingWriteContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options failingWriteOptions = {};
	stackimport_import_options_init(&failingWriteOptions);
	failingWriteOptions.input_path = platformStackPath.c_str();
	failingWriteOptions.output_package_path = failingWritePackage.c_str();
	assert(stackimport_import(failingWriteContext, &failingWriteOptions) == STACKIMPORT_STATUS_IMPORT_FAILED);
	stackimport_context_destroy(failingWriteContext);
	assert(failingWriteState.writes > 0);

	const std::string failingClosePackage = platformStackPath + ".close-failed.xstk";
	CountingPlatformState failingCloseState;
	failingCloseState.fail_closes = true;
	stackimport_platform failingClosePlatform = platform;
	failingClosePlatform.user_data = &failingCloseState;
	stackimport_context* failingCloseContext = nullptr;
	assert(stackimport_context_create_with_platform(&failingClosePlatform, &failingCloseContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options failingCloseOptions = {};
	stackimport_import_options_init(&failingCloseOptions);
	failingCloseOptions.input_path = platformStackPath.c_str();
	failingCloseOptions.output_package_path = failingClosePackage.c_str();
	assert(stackimport_import(failingCloseContext, &failingCloseOptions) == STACKIMPORT_STATUS_IMPORT_FAILED);
	stackimport_context_destroy(failingCloseContext);
	assert(failingCloseState.writes > 0);
	assert(failingCloseState.closes > 0);

	const std::string failingStackPackage = platformStackPath + ".allocation-failed.xstk";
	CountingPlatformState failingPlatformState;
	failingPlatformState.fail_after_allocations = 0;
	stackimport_platform failingPlatform = {};
	stackimport_platform_init(&failingPlatform);
	failingPlatform.allocate = counting_allocate;
	failingPlatform.deallocate = counting_deallocate;
	failingPlatform.open_file = counting_open_file;
	failingPlatform.read_file = counting_read_file;
	failingPlatform.write_file = counting_write_file;
	failingPlatform.close_file = counting_close_file;
	failingPlatform.make_directory = counting_make_directory;
	failingPlatform.user_data = &failingPlatformState;
	alignas(std::max_align_t) unsigned char failingStorage[4096] = {};
	stackimport_context* failingContext = nullptr;
	assert(stackimport_context_init_with_platform(failingStorage, sizeof(failingStorage), &failingPlatform, &failingContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options failingOptions = {};
	stackimport_import_options_init(&failingOptions);
	failingOptions.input_path = platformStackPath.c_str();
	failingOptions.output_package_path = failingStackPackage.c_str();
	assert(stackimport_import(failingContext, &failingOptions) == STACKIMPORT_STATUS_ALLOCATION_FAILED);
	stackimport_context_deinit(failingContext);
	assert(failingPlatformState.allocations > 0);

	CountingPlatformState rapidJsonFailingState;
	rapidJsonFailingState.fail_after_allocations = 0;
	stackimport_platform rapidJsonFailingPlatform = failingPlatform;
	rapidJsonFailingPlatform.user_data = &rapidJsonFailingState;
	const stackimport_internal_platform rapidJsonInternalPlatform = stackimport_internal_platform_from_api(&rapidJsonFailingPlatform);
	{
		stackimport_platform_scope scope(rapidJsonInternalPlatform);
		StackImportRapidJsonAllocator rapidJsonAllocator;
		stackimport_internal_reset_allocation_failure();
		assert(rapidJsonAllocator.Malloc(16) == nullptr);
		assert(stackimport_internal_had_allocation_failure());
		stackimport_internal_reset_allocation_failure();
		assert(rapidJsonAllocator.Realloc(nullptr, 0, 16) == nullptr);
		assert(stackimport_internal_had_allocation_failure());
	}

	CBuf sharedBuffer(4);
	sharedBuffer[0] = 'A';
	assert(sharedBuffer.checked_buf(0, 1) != nullptr);
	assert(sharedBuffer.checked_buf(4, 1) == nullptr);
	CBuf copiedBuffer(sharedBuffer);
	{
		stackimport_platform_scope scope(rapidJsonInternalPlatform);
		stackimport_internal_reset_allocation_failure();
		copiedBuffer[0] = 'B';
		assert(stackimport_internal_had_allocation_failure());
		const CBuf& sharedView = sharedBuffer;
		const CBuf& copiedView = copiedBuffer;
		assert(sharedView[0] == 'A');
		assert(copiedView[0] == 'A');
		stackimport_internal_reset_allocation_failure();
		char* copiedBytes = copiedBuffer.buf(0, 1);
		copiedBytes[0] = 'C';
		assert(stackimport_internal_had_allocation_failure());
		assert(sharedView[0] == 'A');
		assert(copiedView[0] == 'A');
		stackimport_internal_reset_allocation_failure();
		assert(copiedBuffer.checked_buf(0, 1) == nullptr);
		assert(stackimport_internal_had_allocation_failure());
		stackimport_internal_reset_allocation_failure();
		sharedBuffer.resize(8);
		assert(stackimport_internal_had_allocation_failure());
		assert(sharedView.size() == 4);
		assert(sharedView[0] == 'A');
		stackimport_internal_reset_allocation_failure();
		copiedBuffer.resize(8);
		assert(stackimport_internal_had_allocation_failure());
		assert(copiedView.size() == 4);
		assert(copiedView[0] == 'A');
	}
}
