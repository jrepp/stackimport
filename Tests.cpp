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
	size_t allocations = 0;
	size_t fail_after_allocations = SIZE_MAX;
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
	return std::fwrite(data, 1, size, static_cast<FILE*>(file));
}

int STACKIMPORT_CALL counting_close_file(stackimport_file_handle file, void*)
{
	return std::fclose(static_cast<FILE*>(file));
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
			payload.format == stackimport::ResourcePayloadFormat::Rgba32;
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
		return true;
	}

	int wants_count = 0;
	int native_count = 0;
	int rgba_count = 0;
	uint32_t last_width = 0;
	uint32_t last_height = 0;
	size_t last_payload_size = 0;
};

void write_basic_resource_fork_header(uint8_t* fork, uint32_t data_off, uint32_t map_off, uint32_t data_len, uint32_t map_len)
{
	rsrcd::write_u32be(fork + 0, data_off);
	rsrcd::write_u32be(fork + 4, map_off);
	rsrcd::write_u32be(fork + 8, data_len);
	rsrcd::write_u32be(fork + 12, map_len);
}


void append_snd_u16be(std::vector<uint8_t>& data, uint16_t value)
{
	data.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
	data.push_back(static_cast<uint8_t>(value & 0xFFu));
}

void append_snd_u32be(std::vector<uint8_t>& data, uint32_t value)
{
	data.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
	data.push_back(static_cast<uint8_t>(value & 0xFFu));
}

std::vector<uint8_t> make_minimal_snd_fixture()
{
	std::vector<uint8_t> snd;
	append_snd_u16be(snd, 2);
	append_snd_u16be(snd, 0);
	append_snd_u16be(snd, 1);
	append_snd_u16be(snd, 0x8051);
	append_snd_u16be(snd, 0);
	append_snd_u32be(snd, 14);
	append_snd_u32be(snd, 0);
	append_snd_u32be(snd, 4);
	append_snd_u32be(snd, 22050u << 16u);
	append_snd_u32be(snd, 0);
	append_snd_u32be(snd, 4);
	snd.push_back(0);
	snd.push_back(60);
	snd.insert(snd.end(), {0x80, 0x81, 0x82, 0x83});
	return snd;
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

	const std::vector<uint8_t> failingSound = make_minimal_snd_fixture();
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
		std::string soundError;
		assert(!stackimport::ConvertSndResourceToWav(rsrcd::Bytes{failingSound.data(), failingSound.size()}, failingWavData, soundError));
		assert(soundError == "allocation failed");
	}

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
	}

}
