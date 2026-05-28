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
#include "RomDasm.h"
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
#include <initializer_list>
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

struct LogHandlerState {
	int messages = 0;
	uint32_t last_severity = STACKIMPORT_MESSAGE_INFO;
	uint32_t last_category = STACKIMPORT_LOG_GENERAL;
	std::string last_message;
	std::string last_detail;
	bool saw_status_or_progress = false;
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

void STACKIMPORT_CALL test_log_message(uint32_t severity, const char* message, void* user_data)
{
	auto* state = static_cast<LogHandlerState*>(user_data);
	state->messages++;
	state->last_severity = severity;
	state->last_message = message ? message : "";
	if(state->last_message.find("Status:") != std::string::npos ||
		state->last_message.find("Progress:") != std::string::npos)
		state->saw_status_or_progress = true;
}

void STACKIMPORT_CALL test_log_record(const stackimport_log_record* record, void* user_data)
{
	assert(record);
	assert(record->struct_size >= sizeof(stackimport_log_record));
	auto* state = static_cast<LogHandlerState*>(user_data);
	state->messages++;
	state->last_severity = record->severity;
	state->last_category = record->category;
	state->last_message = record->message ? record->message : "";
	state->last_detail = record->detail ? record->detail : "";
	if(record->category == STACKIMPORT_LOG_STATUS || record->category == STACKIMPORT_LOG_PROGRESS)
		state->saw_status_or_progress = true;
	if(record->category != STACKIMPORT_LOG_GENERAL)
		assert(state->last_detail.size() < state->last_message.size());
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

CountingResourceOutput parse_single_resource(const char type[4], int16_t id, const std::vector<uint8_t>& payload)
{
	const std::vector<uint8_t> fork = make_single_resource_fork(type, id, payload);
	CountingResourceOutput output;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{fork.data(), fork.size()}, output));
	assert(output.native_count == 1);
	return output;
}

void assert_rgba_resource(
	const char type[4],
	int16_t id,
	const std::vector<uint8_t>& payload,
	uint32_t width,
	uint32_t height)
{
	CountingResourceOutput output = parse_single_resource(type, id, payload);
	assert(output.rgba_count == 1);
	assert(output.last_width == width);
	assert(output.last_height == height);
	assert(output.last_payload_size == static_cast<size_t>(width) * height * 4u);
}

void assert_json_resource_contains(
	const char type[4],
	int16_t id,
	const std::vector<uint8_t>& payload,
	std::initializer_list<const char*> needles)
{
	CountingResourceOutput output = parse_single_resource(type, id, payload);
	assert(output.json_count == 1);
	for(const char* needle : needles)
		assert(output.last_json.find(needle) != std::string::npos);
}

void assert_text_resource_equals(
	const char type[4],
	int16_t id,
	const std::vector<uint8_t>& payload,
	const std::string& expected)
{
	CountingResourceOutput output = parse_single_resource(type, id, payload);
	assert(output.text_count == 1);
	assert(output.last_text == expected);
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

std::vector<uint8_t> make_synthetic_rom_fixture()
{
	std::vector<uint8_t> rom;
	append_u16be(rom, 0x4E71); // nop: marks the fixture as 68K-like.
	append_u16be(rom, 0xA9F0); // Toolbox trap-shaped word.
	append_u16be(rom, 0x4E75); // rts.
	while(rom.size() < 0x20)
		rom.push_back(0);
	append_u32be(rom, 0x40800040);
	append_u32be(rom, 0x40800044);
	append_u32be(rom, 0x40800048);
	append_u32be(rom, 0x4080004C);
	while(rom.size() < 0x60)
		rom.push_back(0);
	rom.insert(rom.end(), {'B', 'o', 'o', 't', 'R', 'O', 'M', 0});
	rom.insert(rom.end(), {5, 'H', 'e', 'l', 'l', 'o'});
	rom.push_back(0);
	rom.insert(rom.end(), {'T', 'a', 'b', 'l', 'e', 'M', 'a', 'p', 0});
	while(rom.size() < 0x90)
		rom.push_back(0);
	rom.insert(rom.end(), {'D', 'R', 'V', 'R'});
	rom.insert(rom.end(), {'C', 'O', 'D', 'E'});
	while(rom.size() < 0xC0)
		rom.push_back(0);
	const std::vector<uint8_t> resourceFork = make_single_resource_fork("STR ", 128, {5, 'W', 'o', 'r', 'l', 'd'});
	rom.insert(rom.end(), resourceFork.begin(), resourceFork.end());
	return rom;
}

std::vector<uint8_t> make_icon_payload()
{
	std::vector<uint8_t> payload(128);
	payload[0] = 0xFF;
	return payload;
}

std::vector<uint8_t> make_curs_payload()
{
	std::vector<uint8_t> payload(68);
	payload[0] = 0x80;
	payload[32] = 0x80;
	rsrcd::write_u16be(payload.data() + 64, 2);
	rsrcd::write_u16be(payload.data() + 66, 3);
	return payload;
}

std::vector<uint8_t> make_cicn_payload()
{
	std::vector<uint8_t> data(100, 0);
	rsrcd::write_u16be(data.data() + 4, 1);
	rsrcd::write_u16be(data.data() + 10, 1);
	rsrcd::write_u16be(data.data() + 12, 1);
	rsrcd::write_u16be(data.data() + 32, 1);
	rsrcd::write_u16be(data.data() + 54, 1);
	rsrcd::write_u16be(data.data() + 60, 1);
	rsrcd::write_u16be(data.data() + 62, 1);
	rsrcd::write_u16be(data.data() + 74, 1);
	rsrcd::write_u16be(data.data() + 76, 1);
	data[82] = 0x80;
	rsrcd::write_u16be(data.data() + 87, 0x8000);
	rsrcd::write_u16be(data.data() + 89, 0);
	rsrcd::write_u16be(data.data() + 93, 0xFFFF);
	return data;
}

std::vector<uint8_t> make_crsr_payload()
{
	std::vector<uint8_t> data(163, 0);
	rsrcd::write_u16be(data.data(), 0x8001);
	rsrcd::write_u32be(data.data() + 2, 96);
	rsrcd::write_u32be(data.data() + 6, 162);
	rsrcd::write_u16be(data.data() + 14, 1);
	data[20] = 0x80;
	data[52] = 0x80;
	rsrcd::write_u16be(data.data() + 84, 2);
	rsrcd::write_u16be(data.data() + 86, 3);
	rsrcd::write_u32be(data.data() + 88, 146);
	rsrcd::write_u32be(data.data() + 92, 55);
	rsrcd::write_u16be(data.data() + 100, 1);
	rsrcd::write_u16be(data.data() + 106, 1);
	rsrcd::write_u16be(data.data() + 108, 1);
	rsrcd::write_u16be(data.data() + 128, 1);
	rsrcd::write_u32be(data.data() + 138, 146);
	rsrcd::write_u16be(data.data() + 150, 0x8000);
	rsrcd::write_u16be(data.data() + 152, 0);
	rsrcd::write_u16be(data.data() + 156, 0xFFFF);
	return data;
}

void test_resource_image_transforms()
{
	assert_rgba_resource("ICON", 128, make_icon_payload(), 32, 32);

	std::vector<uint8_t> icnPayload(256);
	icnPayload[0] = 0x80;
	icnPayload[128] = 0x80;
	assert_rgba_resource("ICN#", 130, icnPayload, 32, 32);

	assert_rgba_resource("PAT ", 9, {0x80, 0, 0, 0, 0, 0, 0, 0}, 8, 8);

	std::vector<uint8_t> sicnPayload(32);
	sicnPayload[0] = 0x80;
	assert_rgba_resource("SICN", 10, sicnPayload, 16, 16);

	struct IndexedIconCase
	{
		const char* type;
		uint16_t width;
		uint16_t height;
		uint8_t bits_per_pixel;
	};
	const IndexedIconCase indexedIconCases[] = {
		{"icl4", 32, 32, 4},
		{"icl8", 32, 32, 8},
		{"icm4", 16, 12, 4},
		{"icm8", 16, 12, 8},
		{"ics4", 16, 16, 4},
		{"ics8", 16, 16, 8},
	};
	for(const IndexedIconCase& c : indexedIconCases)
	{
		const size_t bytesPerIcon = static_cast<size_t>(c.width) * c.height * c.bits_per_pixel / 8u;
		std::vector<uint8_t> payload(bytesPerIcon);
		payload[0] = c.bits_per_pixel == 4 ? 0x0Fu : 0xFFu;
		assert_rgba_resource(c.type, 11, payload, c.width, c.height);
	}

	struct MonoIconCase
	{
		const char* type;
		uint16_t width;
		uint16_t height;
	};
	const MonoIconCase monoIconCases[] = {
		{"icm#", 16, 12},
		{"ics#", 16, 16},
	};
	for(const MonoIconCase& c : monoIconCases)
	{
		const size_t bytesPerIcon = static_cast<size_t>(c.width) * c.height / 8u;
		std::vector<uint8_t> payload(bytesPerIcon * 2u);
		payload[0] = 0x80;
		payload[bytesPerIcon] = 0x80;
		assert_rgba_resource(c.type, 12, payload, c.width, c.height);
	}

	{
		CountingResourceOutput cursOutput = parse_single_resource("CURS", 13, make_curs_payload());
		assert(cursOutput.rgba_count == 1);
		assert(cursOutput.json_count == 1);
		assert(cursOutput.last_json.find("\"hotspotX\": 3") != std::string::npos);
		assert(cursOutput.last_json.find("\"hotspotY\": 2") != std::string::npos);
	}
}

void test_resource_text_transforms()
{
	assert_text_resource_equals("STR ", 12, {5, 'H', 0x8E, 'l', 'l', 'o'}, "H\xC3\xA9llo");

	std::vector<uint8_t> stringListPayload;
	append_u16be(stringListPayload, 2);
	stringListPayload.insert(stringListPayload.end(), {3, 'O', 'n', 'e'});
	stringListPayload.insert(stringListPayload.end(), {3, 'T', 'w', 'o'});
	assert_json_resource_contains("STR#", 44, stringListPayload, {"\"One\"", "\"Two\""});

	assert_json_resource_contains("TwCS", 1, {0, 1, 0, 5, 'T', 'w', 'C', 'S', '!'}, {"\"TwCS!\""});

	std::vector<uint8_t> txstPayload;
	txstPayload.push_back(0x03);
	txstPayload.push_back(0x00);
	append_u16be(txstPayload, 12);
	append_u16be(txstPayload, 0x1111);
	append_u16be(txstPayload, 0x2222);
	append_u16be(txstPayload, 0x3333);
	txstPayload.insert(txstPayload.end(), {9, 'H', 'e', 'l', 'v', 'e', 't', 'i', 'c', 'a'});
	assert_json_resource_contains("TxSt", 1, txstPayload, {
		"\"fontStyle\": 3",
		"\"fontSize\": 12",
		"\"fontName\": \"Helvetica\"",
	});

	std::vector<uint8_t> stylPayload;
	append_u16be(stylPayload, 1);
	append_u32be(stylPayload, 5);
	append_u16be(stylPayload, 14);
	append_u16be(stylPayload, 11);
	append_u16be(stylPayload, 128);
	append_u16be(stylPayload, 0x0001);
	append_u16be(stylPayload, 12);
	append_u16be(stylPayload, 0x1111);
	append_u16be(stylPayload, 0x2222);
	append_u16be(stylPayload, 0x3333);
	assert_json_resource_contains("styl", 1, stylPayload, {
		"\"offset\": 5",
		"\"fontId\": 128",
		"\"green\": 8738",
	});

	std::vector<uint8_t> kchrPayload(2 + 256 + 2 + 128 + 2 + 8);
	rsrcd::write_u16be(kchrPayload.data(), 0);
	rsrcd::write_u16be(kchrPayload.data() + 258, 1);
	kchrPayload[260] = 'a';
	rsrcd::write_u16be(kchrPayload.data() + 388, 1);
	kchrPayload[390] = 0;
	kchrPayload[391] = 12;
	rsrcd::write_u16be(kchrPayload.data() + 392, 1);
	kchrPayload[394] = '`';
	kchrPayload[395] = 'a';
	kchrPayload[396] = 0;
	kchrPayload[397] = '`';
	assert_json_resource_contains("KCHR", 1, kchrPayload, {
		"\"modifierTableIndexes\"",
		"\"charsHex\": \"61",
		"\"virtualKeyCode\": 12",
		"\"substituteChar\": 97",
	});
}

void test_resource_metadata_transforms()
{
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
	assert_json_resource_contains("HCbg", 77, addColorData, {
		"\"targetKind\": \"background\"",
		"\"targetId\": 77",
		"\"type\": \"rect\"",
		"\"red\": 4369",
	});

	std::vector<uint8_t> versionPayload = {0x01, 0x23, 0x80, 0x00, 0x00, 0x00};
	versionPayload.insert(versionPayload.end(), {3, '1', '.', '2'});
	versionPayload.insert(versionPayload.end(), {7, 'R', 'e', 'l', 'e', 'a', 's', 'e'});
	assert_json_resource_contains("vers", 1, versionPayload, {
		"\"majorRevision\": 1",
		"\"minorAndBugRevision\": 35",
		"\"shortVersion\": \"1.2\"",
	});

	std::vector<uint8_t> cfrgPayload(79);
	rsrcd::write_u16be(cfrgPayload.data() + 10, 1);
	rsrcd::write_u16be(cfrgPayload.data() + 30, 1);
	size_t cfrgOffset = 32;
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset, 0x70777063);
	cfrgPayload[cfrgOffset + 7] = 2;
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 8, 0x01020304);
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 12, 0x00010000);
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 16, 0x00002000);
	rsrcd::write_u16be(cfrgPayload.data() + cfrgOffset + 20, 7);
	cfrgPayload[cfrgOffset + 22] = 1;
	cfrgPayload[cfrgOffset + 23] = 2;
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 24, 0x434F4445);
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 28, 128);
	rsrcd::write_u16be(cfrgPayload.data() + cfrgOffset + 36, 1);
	rsrcd::write_u16be(cfrgPayload.data() + cfrgOffset + 40, 47);
	cfrgPayload[cfrgOffset + 42] = 4;
	cfrgPayload[cfrgOffset + 43] = 'M';
	cfrgPayload[cfrgOffset + 44] = 'a';
	cfrgPayload[cfrgOffset + 45] = 'i';
	cfrgPayload[cfrgOffset + 46] = 'n';
	assert_json_resource_contains("cfrg", 1, cfrgPayload, {
		"\"architectureType\": \"pwpc\"",
		"\"usageName\": \"application\"",
		"\"whereName\": \"resourceFork\"",
		"\"name\": \"Main\"",
	});

	std::vector<uint8_t> mbarPayload;
	append_u16be(mbarPayload, 2);
	append_u16be(mbarPayload, 128);
	append_u16be(mbarPayload, 129);
	assert_json_resource_contains("MBAR", 1, mbarPayload, {"\"menuIds\"", "128", "129"});

	std::vector<uint8_t> alrtPayload;
	append_u16be(alrtPayload, 1);
	append_u16be(alrtPayload, 2);
	append_u16be(alrtPayload, 101);
	append_u16be(alrtPayload, 202);
	append_u16be(alrtPayload, 300);
	alrtPayload.push_back(0x12);
	alrtPayload.push_back(0x34);
	append_u16be(alrtPayload, 0x0A00);
	assert_json_resource_contains("ALRT", 1, alrtPayload, {"\"itemListId\": 300", "\"autoPosition\": 2560"});

	std::vector<uint8_t> frefPayload;
	append_u32be(frefPayload, 0x4150504C);
	append_u16be(frefPayload, 42);
	frefPayload.insert(frefPayload.end(), {4, 'A', 'p', 'p', 's'});
	assert_json_resource_contains("FREF", 1, frefPayload, {"\"fileTypeString\": \"APPL\"", "\"fileName\": \"Apps\""});

	std::vector<uint8_t> bndlPayload;
	append_u32be(bndlPayload, 0x4F574E52);
	append_u16be(bndlPayload, 128);
	append_u16be(bndlPayload, 0);
	append_u32be(bndlPayload, 0x49434F4E);
	append_u16be(bndlPayload, 1);
	append_u16be(bndlPayload, 0);
	append_u16be(bndlPayload, 1000);
	append_u16be(bndlPayload, 1);
	append_u16be(bndlPayload, 1001);
	assert_json_resource_contains("BNDL", 1, bndlPayload, {
		"\"ownerNameString\": \"OWNR\"",
		"\"typeString\": \"ICON\"",
		"\"resourceId\": 1001",
	});

	std::vector<uint8_t> rovPayload;
	append_u16be(rovPayload, 0x0750);
	append_u16be(rovPayload, 1);
	append_u32be(rovPayload, 0x4D454E55);
	append_u16be(rovPayload, 128);
	assert_json_resource_contains("ROv#", 1, rovPayload, {
		"\"romVersion\": 1872",
		"\"typeString\": \"MENU\"",
		"\"id\": 128",
	});

	std::vector<uint8_t> rsscPayload(24);
	rsrcd::write_u32be(rsscPayload.data(), 0x52535343);
	rsrcd::write_u16be(rsscPayload.data() + 4, 22);
	rsscPayload[22] = 0x4E;
	rsscPayload[23] = 0x75;
	assert_json_resource_contains("RSSC", 1, rsscPayload, {
		"\"typeSignatureString\": \"RSSC\"",
		"\"codeStartOffset\": 22",
		"\"codeSize\": 2",
	});

	assert_json_resource_contains("RECT", 1, {0, 1, 0, 2, 0, 101, 0, 202}, {"\"top\": 1", "\"right\": 202"});

	std::vector<uint8_t> toolPayload;
	append_u16be(toolPayload, 2);
	append_u16be(toolPayload, 1);
	append_u16be(toolPayload, 128);
	append_u16be(toolPayload, 129);
	append_u16be(toolPayload, 130);
	assert_json_resource_contains("TOOL", 1, toolPayload, {"\"toolsPerRow\": 2", "\"cursorIds\"", "130"});

	std::vector<uint8_t> pickPayload;
	append_u32be(pickPayload, 0x50494354);
	pickPayload.insert(pickPayload.end(), {1, 2, 3, 0});
	append_u16be(pickPayload, 16);
	append_u16be(pickPayload, 1);
	append_u32be(pickPayload, 0x49434F4E);
	append_u16be(pickPayload, 128);
	append_u32be(pickPayload, 0x50494354);
	append_u16be(pickPayload, 129);
	assert_json_resource_contains("PICK", 1, pickPayload, {
		"\"typeString\": \"PICT\"",
		"\"verticalCellSize\": 16",
		"\"id\": 129",
	});

	assert_json_resource_contains("KBDN", 1, {3, 'U', 'S', 'A'}, {"\"name\": \"USA\""});

	std::vector<uint8_t> papaPayload = {
		7, 'P', 'r', 'i', 'n', 't', 'e', 'r',
		3, 'L', 'W', 'R',
		4, 'Z', 'o', 'n', 'e',
	};
	append_u32be(papaPayload, 0x12345678);
	papaPayload.push_back(0xAA);
	assert_json_resource_contains("PAPA", 1, papaPayload, {
		"\"name\": \"Printer\"",
		"\"addressBlock\": 305419896",
		"\"dataHex\": \"AA\"",
	});

	std::vector<uint8_t> layoPayload;
	append_u16be(layoPayload, 128);
	append_u16be(layoPayload, 12);
	append_u16be(layoPayload, 20);
	append_u16be(layoPayload, 1);
	append_u16be(layoPayload, 2);
	append_u16be(layoPayload, 30);
	append_u16be(layoPayload, 40);
	append_u16be(layoPayload, 10);
	append_u16be(layoPayload, 20);
	append_u16be(layoPayload, 110);
	append_u16be(layoPayload, 220);
	append_u16be(layoPayload, 0);
	append_u16be(layoPayload, 1);
	append_u16be(layoPayload, 2);
	append_u16be(layoPayload, 3);
	append_u16be(layoPayload, 4);
	assert_json_resource_contains("LAYO", 1, layoPayload, {"\"fontId\": 128", "\"right\": 220", "\"rectangles\""});
}

void test_resource_code_transforms()
{
	std::vector<uint8_t> code0Payload;
	append_u32be(code0Payload, 0x1000);
	append_u32be(code0Payload, 0x2000);
	append_u32be(code0Payload, 8);
	append_u32be(code0Payload, 16);
	append_u16be(code0Payload, 0x0010);
	append_u16be(code0Payload, 0x3F3C);
	append_u16be(code0Payload, 1);
	append_u16be(code0Payload, 0xA9F0);
	assert_json_resource_contains("CODE", 0, code0Payload, {"\"kind\": \"jumpTable\"", "\"loadSegmentTrap\": true"});

	std::vector<uint8_t> codePayload;
	append_u16be(codePayload, 2);
	append_u16be(codePayload, 3);
	codePayload.insert(codePayload.end(), {0x4E, 0x75});
	assert_json_resource_contains("CODE", 1, codePayload, {"\"kind\": \"nearSegment\"", "\"codeSize\": 2"});
	{
		CountingResourceOutput codeOutput = parse_single_resource("CODE", 1, codePayload);
		assert(codeOutput.json_count == 1);
		assert(codeOutput.text_count == 1);
		assert(codeOutput.last_text.find("rts") != std::string::npos ||
			codeOutput.last_text.find("dc.w $4E75") != std::string::npos);
	}

	std::vector<uint8_t> drvrPayload(26);
	rsrcd::write_u16be(drvrPayload.data(), 0x0100);
	rsrcd::write_u16be(drvrPayload.data() + 2, 60);
	rsrcd::write_u16be(drvrPayload.data() + 4, 0xFFFF);
	rsrcd::write_u16be(drvrPayload.data() + 6, 128);
	rsrcd::write_u16be(drvrPayload.data() + 8, 24);
	drvrPayload[18] = 4;
	drvrPayload[19] = 'D';
	drvrPayload[20] = 'r';
	drvrPayload[21] = 'v';
	drvrPayload[22] = 'r';
	drvrPayload[24] = 0x4E;
	drvrPayload[25] = 0x75;
	assert_json_resource_contains("DRVR", 1, drvrPayload, {
		"\"name\": \"Drvr\"",
		"\"codeStartOffset\": 24",
		"\"codeSize\": 2",
	});
	{
		CountingResourceOutput drvrOutput = parse_single_resource("DRVR", 1, drvrPayload);
		assert(drvrOutput.json_count == 1);
		assert(drvrOutput.text_count == 1);
		assert(drvrOutput.last_text.find("rts") != std::string::npos ||
			drvrOutput.last_text.find("dc.w $4E75") != std::string::npos);
	}

	std::vector<uint8_t> dcmpPayload;
	append_u16be(dcmpPayload, 10);
	append_u16be(dcmpPayload, 12);
	append_u16be(dcmpPayload, 14);
	dcmpPayload.insert(dcmpPayload.end(), {0x4E, 0x75});
	assert_json_resource_contains("dcmp", 1, dcmpPayload, {
		"\"initLabel\": 10",
		"\"pcOffset\": 6",
		"\"codeSize\": 2",
	});
	{
		CountingResourceOutput dcmpOutput = parse_single_resource("dcmp", 1, dcmpPayload);
		assert(dcmpOutput.json_count == 1);
		assert(dcmpOutput.text_count == 1);
		assert(dcmpOutput.last_text.find("rts") != std::string::npos ||
			dcmpOutput.last_text.find("dc.w $4E75") != std::string::npos);
	}
}

void test_resource_color_and_ui_transforms()
{
	std::vector<uint8_t> colorTablePayload;
	append_u32be(colorTablePayload, 0x12345678);
	append_u16be(colorTablePayload, 0x8000);
	append_u16be(colorTablePayload, 0);
	append_u16be(colorTablePayload, 7);
	append_u16be(colorTablePayload, 0x1111);
	append_u16be(colorTablePayload, 0x2222);
	append_u16be(colorTablePayload, 0x3333);
	assert_json_resource_contains("clut", 8, colorTablePayload, {
		"\"seed\": 305419896",
		"\"flags\": 32768",
		"\"green\": 8738",
	});
	assert_json_resource_contains("wctb", 0, colorTablePayload, {"\"green\": 8738"});

	std::vector<uint8_t> plttPayload(32);
	rsrcd::write_u16be(plttPayload.data(), 1);
	rsrcd::write_u16be(plttPayload.data() + 18, 0x1111);
	rsrcd::write_u16be(plttPayload.data() + 20, 0x2222);
	rsrcd::write_u16be(plttPayload.data() + 22, 0x3333);
	assert_json_resource_contains("pltt", 3, plttPayload, {"\"green\": 8738"});

	std::vector<uint8_t> ppatPayload(28);
	rsrcd::write_u16be(ppatPayload.data(), 0);
	rsrcd::write_u32be(ppatPayload.data() + 20, 0x80000000);
	rsrcd::write_u32be(ppatPayload.data() + 24, 0x00000001);
	assert_json_resource_contains("ppat", 3, ppatPayload, {
		"\"type\": 0",
		"\"monochromePatternHex\": \"8000000000000001\"",
	});
	{
		CountingResourceOutput ppatOutput = parse_single_resource("ppat", 3, ppatPayload);
		assert(ppatOutput.json_count == 1);
		assert(ppatOutput.rgba_count == 4);
	}

	std::vector<uint8_t> pptPayload(34);
	rsrcd::write_u16be(pptPayload.data(), 1);
	rsrcd::write_u32be(pptPayload.data() + 2, 6);
	rsrcd::write_u16be(pptPayload.data() + 6, 0);
	rsrcd::write_u32be(pptPayload.data() + 26, 0x11111111);
	rsrcd::write_u32be(pptPayload.data() + 30, 0x22222222);
	assert_json_resource_contains("ppt#", 3, pptPayload, {
		"\"patterns\"",
		"\"offset\": 6",
		"\"monochromePatternHex\": \"1111111122222222\"",
	});
	{
		CountingResourceOutput pptOutput = parse_single_resource("ppt#", 3, pptPayload);
		assert(pptOutput.json_count == 1);
		assert(pptOutput.rgba_count == 4);
	}

	std::vector<uint8_t> cicnPayload = make_cicn_payload();
	assert_json_resource_contains("cicn", 3, cicnPayload, {
		"\"pixelSize\": 1",
		"\"maskDataOffset\": 82",
		"\"colorTableOffset\": 83",
	});
	assert_rgba_resource("cicn", 3, cicnPayload, 1, 1);

	{
		CountingResourceOutput crsrOutput = parse_single_resource("crsr", 4, make_crsr_payload());
		assert(crsrOutput.json_count == 1);
		assert(crsrOutput.rgba_count == 2);
		assert(crsrOutput.last_width == 16);
		assert(crsrOutput.last_height == 16);
	}

	std::vector<uint8_t> sizePayload;
	append_u16be(sizePayload, 0xD048);
	append_u32be(sizePayload, 0x00100000);
	append_u32be(sizePayload, 0x00080000);
	assert_json_resource_contains("SIZE", -1, sizePayload, {
		"\"preferredSize\": 1048576",
		"\"minimumSize\": 524288",
		"\"saveScreen\": true",
	});

	std::vector<uint8_t> finfPayload;
	append_u16be(finfPayload, 1);
	append_u16be(finfPayload, 128);
	append_u16be(finfPayload, 1);
	append_u16be(finfPayload, 12);
	assert_json_resource_contains("finf", 1, finfPayload, {"\"fontId\": 128", "\"size\": 12"});

	std::vector<uint8_t> dlogPayload;
	append_u16be(dlogPayload, 1);
	append_u16be(dlogPayload, 2);
	append_u16be(dlogPayload, 30);
	append_u16be(dlogPayload, 40);
	append_u16be(dlogPayload, 4);
	append_u16be(dlogPayload, 1);
	append_u16be(dlogPayload, 1);
	append_u32be(dlogPayload, 0x12345678);
	append_u16be(dlogPayload, 128);
	dlogPayload.insert(dlogPayload.end(), {5, 'H', 'e', 'l', 'l', 'o'});
	append_u16be(dlogPayload, 0xABCD);
	assert_json_resource_contains("DLOG", 128, dlogPayload, {
		"\"itemsId\": 128",
		"\"title\": \"Hello\"",
		"\"autoPosition\": 43981",
	});

	std::vector<uint8_t> menuPayload;
	append_u16be(menuPayload, 128);
	append_u16be(menuPayload, 0);
	append_u16be(menuPayload, 0);
	append_u16be(menuPayload, 0);
	append_u16be(menuPayload, 0);
	append_u32be(menuPayload, 0x00000003);
	menuPayload.insert(menuPayload.end(), {4, 'F', 'i', 'l', 'e'});
	menuPayload.insert(menuPayload.end(), {4, 'O', 'p', 'e', 'n', 0, 'O', 0, 0});
	menuPayload.push_back(0);
	assert_json_resource_contains("MENU", 128, menuPayload, {
		"\"title\": \"File\"",
		"\"name\": \"Open\"",
		"\"keyEquivalent\": 79",
	});

	std::vector<uint8_t> ditlPayload;
	append_u16be(ditlPayload, 1);
	ditlPayload.insert(ditlPayload.end(), {0, 0, 0, 0});
	append_u16be(ditlPayload, 1);
	append_u16be(ditlPayload, 2);
	append_u16be(ditlPayload, 30);
	append_u16be(ditlPayload, 40);
	ditlPayload.insert(ditlPayload.end(), {0x04, 2, 'O', 'K'});
	ditlPayload.insert(ditlPayload.end(), {0, 0, 0, 0});
	append_u16be(ditlPayload, 1);
	append_u16be(ditlPayload, 2);
	append_u16be(ditlPayload, 30);
	append_u16be(ditlPayload, 40);
	ditlPayload.insert(ditlPayload.end(), {0x20, 2});
	append_u16be(ditlPayload, 128);
	if((ditlPayload.size() & 1u) != 0)
		ditlPayload.push_back(0);
	assert_json_resource_contains("DITL", 128, ditlPayload, {
		"\"kind\": \"button\"",
		"\"info\": \"OK\"",
		"\"resourceId\": 128",
	});
}

void test_resource_package_writes()
{
	const std::string resourceForkRoot = std::string("/tmp/stackimport-rsrc-root-") + std::to_string(std::rand());
	const std::string resourceForkOutput = std::string("/tmp/stackimport-rsrc-output-") + std::to_string(std::rand());
	assert(counting_make_directory(resourceForkOutput.c_str(), nullptr) == 0);
	const std::vector<uint8_t> iconFork = make_single_resource_fork("ICON", 128, make_icon_payload());
	ResourceForkPlatformState resourceForkPlatformState;
	resourceForkPlatformState.resource_fork_data = iconFork.data();
	resourceForkPlatformState.resource_fork_size = iconFork.size();
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
	assert(resourceForkBytes == iconFork.size());
	assert(resourceSummaries.size() == 1);
	assert(resourceSummaries[0].type == "ICON");
	assert(resourceSummaries[0].status == "exported");
	assert(resourceSummaries[0].outputFile == "ICON_128.png");
	assert(resourceSummaries[0].outputArtifacts.size() == 1);
	assert(resourceSummaries[0].outputArtifacts[0].path == "ICON_128.png");
	assert(resourceSummaries[0].outputArtifacts[0].format == "png");
	assert(resourceSummaries[0].outputArtifacts[0].mediaType == "image/png");
	assert(packageResourceOutput.native_count == 1);
	assert(packageResourceOutput.rgba_count == 1);
	assert(resourceForkPlatformState.opens > 0);
	assert(resourceForkPlatformState.reads > 0);
	assert(resourceForkPlatformState.writes > 0);

	const std::vector<uint8_t> cursFork = make_single_resource_fork("CURS", 24, make_curs_payload());
	const std::string cursOutputPath = std::string("/tmp/stackimport-curs-output-") + std::to_string(std::rand());
	assert(counting_make_directory(cursOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState cursPlatformState;
	cursPlatformState.resource_fork_data = cursFork.data();
	cursPlatformState.resource_fork_size = cursFork.size();
	stackimport_platform cursPlatform = resourceForkPlatform;
	cursPlatform.user_data = &cursPlatformState;
	const stackimport_internal_platform cursInternalPlatform = stackimport_internal_platform_from_api(&cursPlatform);
	CountingResourceOutput cursOutput;
	std::vector<CResourceSummary> cursSummaries;
	std::string cursStatus;
	uint64_t cursBytes = 0;
	{
		stackimport_platform_scope cursScope(cursInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			cursOutputPath,
			"Stack",
			&cursOutput,
			cursSummaries,
			cursStatus,
			cursBytes));
	}
	assert(cursStatus == "ok");
	assert(cursSummaries.size() == 1);
	assert(cursSummaries[0].type == "CURS");
	assert(cursSummaries[0].status == "exported");
	assert(cursSummaries[0].outputFile == "CURS_24.png");
	assert(cursSummaries[0].outputArtifacts.size() == 2);
	assert(cursSummaries[0].outputArtifacts[0].path == "CURS_24.png");
	assert(cursSummaries[0].outputArtifacts[1].path == "CURS_24.json");

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
	const std::string addColorOutputPath = std::string("/tmp/stackimport-addcolor-output-") + std::to_string(std::rand());
	assert(counting_make_directory(addColorOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState addColorPlatformState;
	addColorPlatformState.resource_fork_data = addColorFork.data();
	addColorPlatformState.resource_fork_size = addColorFork.size();
	stackimport_platform addColorPlatform = resourceForkPlatform;
	addColorPlatform.user_data = &addColorPlatformState;
	const stackimport_internal_platform addColorInternalPlatform = stackimport_internal_platform_from_api(&addColorPlatform);
	CountingResourceOutput addColorOutput;
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
	assert(addColorSummaries[0].outputArtifacts.size() == 1);
	assert(addColorSummaries[0].outputArtifacts[0].path == "HCbg_77.json");
	assert(addColorSummaries[0].outputArtifacts[0].format == "json");
	assert(addColorSummaries[0].outputArtifacts[0].mediaType == "application/json");
	const std::string addColorJson = read_text_file(addColorOutputPath + "/HCbg_77.json");
	assert(addColorJson.find("\"targetKind\": \"background\"") != std::string::npos);

	const std::vector<uint8_t> cicnFork = make_single_resource_fork("cicn", 22, make_cicn_payload());
	const std::string cicnOutputPath = std::string("/tmp/stackimport-cicn-output-") + std::to_string(std::rand());
	assert(counting_make_directory(cicnOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState cicnPlatformState;
	cicnPlatformState.resource_fork_data = cicnFork.data();
	cicnPlatformState.resource_fork_size = cicnFork.size();
	stackimport_platform cicnPlatform = resourceForkPlatform;
	cicnPlatform.user_data = &cicnPlatformState;
	const stackimport_internal_platform cicnInternalPlatform = stackimport_internal_platform_from_api(&cicnPlatform);
	CountingResourceOutput cicnOutput;
	std::vector<CResourceSummary> cicnSummaries;
	std::string cicnStatus;
	uint64_t cicnBytes = 0;
	{
		stackimport_platform_scope cicnScope(cicnInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			cicnOutputPath,
			"Stack",
			&cicnOutput,
			cicnSummaries,
			cicnStatus,
			cicnBytes));
	}
	assert(cicnStatus == "ok");
	assert(cicnSummaries.size() == 1);
	assert(cicnSummaries[0].type == "cicn");
	assert(cicnSummaries[0].status == "exported");
	assert(cicnSummaries[0].outputFile == "cicn_22.json");
	assert(cicnSummaries[0].outputArtifacts.size() == 2);
	assert(cicnSummaries[0].outputArtifacts[0].path == "cicn_22.json");
	assert(cicnSummaries[0].outputArtifacts[0].format == "json");
	assert(cicnSummaries[0].outputArtifacts[1].path == "cicn_22.png");
	assert(cicnSummaries[0].outputArtifacts[1].format == "png");
	assert(cicnSummaries[0].outputArtifacts[1].mediaType == "image/png");

	const std::vector<uint8_t> crsrFork = make_single_resource_fork("crsr", 23, make_crsr_payload());
	const std::string crsrOutputPath = std::string("/tmp/stackimport-crsr-output-") + std::to_string(std::rand());
	assert(counting_make_directory(crsrOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState crsrPlatformState;
	crsrPlatformState.resource_fork_data = crsrFork.data();
	crsrPlatformState.resource_fork_size = crsrFork.size();
	stackimport_platform crsrPlatform = resourceForkPlatform;
	crsrPlatform.user_data = &crsrPlatformState;
	const stackimport_internal_platform crsrInternalPlatform = stackimport_internal_platform_from_api(&crsrPlatform);
	CountingResourceOutput crsrOutput;
	std::vector<CResourceSummary> crsrSummaries;
	std::string crsrStatus;
	uint64_t crsrBytes = 0;
	{
		stackimport_platform_scope crsrScope(crsrInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			crsrOutputPath,
			"Stack",
			&crsrOutput,
			crsrSummaries,
			crsrStatus,
			crsrBytes));
	}
	assert(crsrStatus == "ok");
	assert(crsrSummaries.size() == 1);
	assert(crsrSummaries[0].type == "crsr");
	assert(crsrSummaries[0].status == "exported");
	assert(crsrSummaries[0].outputFile == "crsr_23.json");
	assert(crsrSummaries[0].outputArtifacts.size() == 3);
	assert(crsrSummaries[0].outputArtifacts[0].path == "crsr_23.json");
	assert(crsrSummaries[0].outputArtifacts[1].path == "crsr_23.png");
	assert(crsrSummaries[0].outputArtifacts[2].path == "crsr_23_bitmap.png");

	std::vector<uint8_t> packagePpatPayload(28);
	rsrcd::write_u16be(packagePpatPayload.data(), 0);
	rsrcd::write_u32be(packagePpatPayload.data() + 20, 0x80000000);
	rsrcd::write_u32be(packagePpatPayload.data() + 24, 0x00000001);
	const std::vector<uint8_t> ppatFork = make_single_resource_fork("ppat", 44, packagePpatPayload);
	const std::string ppatOutputPath = std::string("/tmp/stackimport-ppat-output-") + std::to_string(std::rand());
	assert(counting_make_directory(ppatOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState ppatPlatformState;
	ppatPlatformState.resource_fork_data = ppatFork.data();
	ppatPlatformState.resource_fork_size = ppatFork.size();
	stackimport_platform ppatPlatform = resourceForkPlatform;
	ppatPlatform.user_data = &ppatPlatformState;
	const stackimport_internal_platform ppatInternalPlatform = stackimport_internal_platform_from_api(&ppatPlatform);
	CountingResourceOutput ppatOutput;
	std::vector<CResourceSummary> ppatSummaries;
	std::string ppatStatus;
	uint64_t ppatBytes = 0;
	{
		stackimport_platform_scope ppatScope(ppatInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			ppatOutputPath,
			"Stack",
			&ppatOutput,
			ppatSummaries,
			ppatStatus,
			ppatBytes));
	}
	assert(ppatStatus == "ok");
	assert(ppatSummaries.size() == 1);
	assert(ppatSummaries[0].type == "ppat");
	assert(ppatSummaries[0].status == "exported");
	assert(ppatSummaries[0].outputFile == "ppat_44.json");
	assert(ppatSummaries[0].outputArtifacts.size() == 5);
	assert(ppatSummaries[0].outputArtifacts[0].path == "ppat_44.json");
	assert(ppatSummaries[0].outputArtifacts[1].path == "ppat_44_color.png");
	assert(ppatSummaries[0].outputArtifacts[2].path == "ppat_44_color_tiled.png");
	assert(ppatSummaries[0].outputArtifacts[3].path == "ppat_44_bitmap.png");
	assert(ppatSummaries[0].outputArtifacts[4].path == "ppat_44_bitmap_tiled.png");

	std::vector<uint8_t> sicnPayload(64, 0x00);
	for(size_t row = 0; row < 16; row++)
	{
		sicnPayload[row * 2] = 0xFF;
		sicnPayload[32 + row * 2 + 1] = 0xFF;
	}
	const std::vector<uint8_t> sicnFork = make_single_resource_fork("SICN", 33, sicnPayload);
	const std::string sicnOutputPath = std::string("/tmp/stackimport-sicn-output-") + std::to_string(std::rand());
	assert(counting_make_directory(sicnOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState sicnPlatformState;
	sicnPlatformState.resource_fork_data = sicnFork.data();
	sicnPlatformState.resource_fork_size = sicnFork.size();
	stackimport_platform sicnPlatform = resourceForkPlatform;
	sicnPlatform.user_data = &sicnPlatformState;
	const stackimport_internal_platform sicnInternalPlatform = stackimport_internal_platform_from_api(&sicnPlatform);
	CountingResourceOutput sicnOutput;
	std::vector<CResourceSummary> sicnSummaries;
	std::string sicnStatus;
	uint64_t sicnBytes = 0;
	{
		stackimport_platform_scope sicnScope(sicnInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			sicnOutputPath,
			"Stack",
			&sicnOutput,
			sicnSummaries,
			sicnStatus,
			sicnBytes));
	}
	assert(sicnStatus == "ok");
	assert(sicnSummaries.size() == 1);
	assert(sicnSummaries[0].type == "SICN");
	assert(sicnSummaries[0].status == "exported");
	assert(sicnSummaries[0].outputFile == "SICN_33_00.png");
	assert(sicnSummaries[0].outputArtifacts.size() == 2);
	assert(sicnSummaries[0].outputArtifacts[0].path == "SICN_33_00.png");
	assert(sicnSummaries[0].outputArtifacts[0].variantIndex == 0);
	assert(sicnSummaries[0].outputArtifacts[1].path == "SICN_33_01.png");
	assert(sicnSummaries[0].outputArtifacts[1].variantIndex == 1);

	const std::vector<uint8_t> strFork = make_single_resource_fork("STR ", 12, {5, 'H', 0x8E, 'l', 'l', 'o'});
	const std::string textOutputPath = std::string("/tmp/stackimport-text-output-") + std::to_string(std::rand());
	assert(counting_make_directory(textOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState textPlatformState;
	textPlatformState.resource_fork_data = strFork.data();
	textPlatformState.resource_fork_size = strFork.size();
	stackimport_platform textPlatform = resourceForkPlatform;
	textPlatform.user_data = &textPlatformState;
	const stackimport_internal_platform textInternalPlatform = stackimport_internal_platform_from_api(&textPlatform);
	CountingResourceOutput textOutput;
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
	assert(textSummaries[0].outputArtifacts.size() == 1);
	assert(textSummaries[0].outputArtifacts[0].path == "resource-text/Stack_STR%20_12.txt");
	assert(textSummaries[0].outputArtifacts[0].format == "text");
	const std::string convertedText = read_text_file(textOutputPath + "/resource-text/Stack_STR%20_12.txt");
	assert(convertedText == "H\xC3\xA9llo");

	std::vector<uint8_t> packageCodePayload;
	append_u16be(packageCodePayload, 2);
	append_u16be(packageCodePayload, 3);
	packageCodePayload.insert(packageCodePayload.end(), {0x4E, 0x75});
	const std::vector<uint8_t> codeFork = make_single_resource_fork("CODE", 1, packageCodePayload);
	const std::string codeOutputPath = std::string("/tmp/stackimport-code-output-") + std::to_string(std::rand());
	assert(counting_make_directory(codeOutputPath.c_str(), nullptr) == 0);
	ResourceForkPlatformState codePlatformState;
	codePlatformState.resource_fork_data = codeFork.data();
	codePlatformState.resource_fork_size = codeFork.size();
	stackimport_platform codePlatform = resourceForkPlatform;
	codePlatform.user_data = &codePlatformState;
	const stackimport_internal_platform codeInternalPlatform = stackimport_internal_platform_from_api(&codePlatform);
	CountingResourceOutput codeOutput;
	std::vector<CResourceSummary> codeSummaries;
	std::string codeStatus;
	uint64_t codeBytes = 0;
	{
		stackimport_platform_scope codeScope(codeInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			codeOutputPath,
			"Stack",
			&codeOutput,
			codeSummaries,
			codeStatus,
			codeBytes));
	}
	assert(codeStatus == "ok");
	assert(codeSummaries.size() == 1);
	assert(codeSummaries[0].type == "CODE");
	assert(codeSummaries[0].status == "exported");
	assert(codeSummaries[0].outputFile == "CODE_1.json");
	assert(codeSummaries[0].disassemblyFile == "resource-disassembly/Stack_CODE_mac68k_1.s");
	assert(codeSummaries[0].outputArtifacts.size() == 2);
	assert(codeSummaries[0].outputArtifacts[0].path == "resource-disassembly/Stack_CODE_mac68k_1.s");
	assert(codeSummaries[0].outputArtifacts[0].format == "text");
	assert(codeSummaries[0].outputArtifacts[1].path == "CODE_1.json");
}

void test_stack_block_identifier()
{
	CStackBlockIdentifier a("TEST", 1);
	CStackBlockIdentifier b("TEST", 2);
	CStackBlockIdentifier wildcardYes("TEST");
	CStackBlockIdentifier wildcardNo("TOON");

	assert((a == b) == false);
	assert(a == wildcardYes);
	assert(wildcardYes == a);
	assert(b == wildcardYes);
	assert(b == wildcardYes);
	assert((a == wildcardNo) == false);
	assert((wildcardNo == a) == false);
	assert((b == wildcardNo) == false);
	assert((wildcardNo == b) == false);

	assert(a < b);
	assert((a < wildcardYes) == false);
	assert((wildcardYes < a) == false);
	assert((b < wildcardYes) == false);
	assert((wildcardYes < b) == false);
	assert(a < wildcardNo);
	assert((wildcardNo < a) == false);
	assert(b < wildcardNo);
	assert((wildcardNo < b) == false);

	assert((a > b) == false);
	assert((a > wildcardYes) == false);
	assert((wildcardYes > a) == false);
	assert((b > wildcardYes) == false);
	assert((wildcardYes > b) == false);
	assert((a > wildcardNo) == false);
	assert(wildcardNo > a);
	assert((b > wildcardNo) == false);
	assert(wildcardNo > b);
}

void test_rom_analysis_contracts()
{
	const uint8_t crcFixture[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
	const std::span<const uint8_t> crcSpan(crcFixture, sizeof(crcFixture));
	assert(stackimport::RomDasm::compute_crc32(crcSpan) == 0xCBF43926u);
	assert(stackimport::RomDasm::format_address(0xCBF43926u) == "CBF43926");
	assert(stackimport::RomDasm::format_address(0x0000007Au) == "0000007A");

	const uint32_t baseAddress = 0x40800000u;
	const std::vector<uint8_t> rom = make_synthetic_rom_fixture();
	const std::span<const uint8_t> romSpan(rom.data(), rom.size());
	stackimport::RomDasm::RomInfo info = stackimport::RomDasm::analyze_rom_header(romSpan, baseAddress);
	info.filename = "synthetic-old-world-rom.bin";
	assert(info.size == static_cast<uint32_t>(rom.size()));
	assert(info.crc32 == stackimport::RomDasm::compute_crc32(romSpan));
	assert(info.base_address == baseAddress);
	assert(info.sha256.size() == 64);
	assert(info.machine_family == "68K");

	stackimport::RomDasm::ScanOptions options;
	options.start_address = baseAddress;
	options.disassemble_code = false;
	const stackimport::RomDasm::RomAnalysis analysis = stackimport::RomDasm::scan_rom(romSpan, info, options);
	assert(analysis.info.filename == "synthetic-old-world-rom.bin");
	assert(analysis.entry_point == baseAddress);
	assert(analysis.pointer_tables.size() >= 4);
	assert(!analysis.pointer_table_regions.empty());
	assert(analysis.pointer_table_regions[0].address == baseAddress + 0x20);
	assert(analysis.pointer_table_regions[0].entry_count == 4);
	assert(analysis.pointer_table_regions[0].targets[0] == baseAddress + 0x40);

	bool sawBootRom = false;
	bool sawPascalHello = false;
	bool sawTableMap = false;
	for(const auto& stringRegion : analysis.strings) {
		if(!stringRegion.is_pascal && stringRegion.value == "BootROM")
			sawBootRom = true;
		if(stringRegion.is_pascal && stringRegion.value == "Hello")
			sawPascalHello = true;
		if(!stringRegion.is_pascal && stringRegion.value == "TableMap")
			sawTableMap = true;
	}
	assert(sawBootRom);
	assert(sawPascalHello);
	assert(sawTableMap);

	bool sawDriverMarker = false;
	bool sawCodeMarker = false;
	for(const auto& marker : analysis.resource_markers) {
		if(marker.type == "DRVR" && marker.address == baseAddress + 0x90)
			sawDriverMarker = true;
		if(marker.type == "CODE" && marker.address == baseAddress + 0x94)
			sawCodeMarker = true;
	}
	assert(sawDriverMarker);
	assert(sawCodeMarker);
	assert(analysis.resource_maps.size() == 1);
	assert(analysis.resource_maps[0].address == baseAddress + 0xC0);
	assert(analysis.resource_maps[0].resource_count == 1);
	assert(analysis.resources.size() == 1);
	assert(analysis.resources[0].address == baseAddress + 0xC0 + 20);
	assert(analysis.resources[0].map_address == baseAddress + 0xC0 + 26);
	assert(analysis.resources[0].type == "STR ");
	assert(analysis.resources[0].id == 128);
	assert(analysis.resources[0].length == 6);

	bool sawStringCluster = false;
	bool sawResourceMapRegion = false;
	bool sawResourceDataRegion = false;
	for(const auto& region : analysis.data_regions) {
		if(region.kind == "string_cluster" && region.item_count >= 3)
			sawStringCluster = true;
		if(region.kind == "resource_map" && region.item_count == 1)
			sawResourceMapRegion = true;
		if(region.kind == "resource_data" && region.item_count == 1)
			sawResourceDataRegion = true;
	}
	assert(sawStringCluster);
	assert(sawResourceMapRegion);
	assert(sawResourceDataRegion);

	stackimport::RomDasm::RomAnalysis controlFlow;
	controlFlow.info = info;
	stackimport::RomDasm::DisassemblyRegion codeRegion;
	codeRegion.start_address = baseAddress;
	codeRegion.end_address = baseAddress + 0x80;
	codeRegion.is_code = true;
	codeRegion.confidence = 0.95;
	codeRegion.text =
		"40800000  6100 001E                bsr        +0x20 /* 40800020 */\n"
		"40800004  60FA                     bra        -0x4 /* 40800000 */\n"
		"40800006  4EFA 0018                jmp        [PC + 0x18 /* 40800020 */]\n"
		"4080000A  41FA 0020                lea.l      A0, [PC + 0x20 /* 4080002C */]\n"
		"4080000E  A9F0                     syscall    SysBeep\n";
	controlFlow.code_regions.push_back(codeRegion);
	std::vector<uint8_t> controlFlowBytes(0x100, 0);
	stackimport::RomDasm::classify_rom_structure(
		controlFlow,
		std::span<const uint8_t>(controlFlowBytes.data(), controlFlowBytes.size()),
		baseAddress);
	bool sawCallXref = false;
	bool sawBranchXref = false;
	bool sawJumpXref = false;
	bool sawMemoryXref = false;
	for(const auto& xref : controlFlow.xrefs) {
		if(xref.from == baseAddress && xref.to == baseAddress + 0x20 && xref.kind == "call")
			sawCallXref = true;
		if(xref.from == baseAddress + 0x04 && xref.to == baseAddress && xref.kind == "branch")
			sawBranchXref = true;
		if(xref.from == baseAddress + 0x06 && xref.to == baseAddress + 0x20 && xref.kind == "jump")
			sawJumpXref = true;
		if(xref.from == baseAddress + 0x0A && xref.to == baseAddress + 0x2C && xref.kind == "memory")
			sawMemoryXref = true;
	}
	assert(sawCallXref);
	assert(sawBranchXref);
	assert(sawJumpXref);
	assert(sawMemoryXref);
	assert(!controlFlow.traps.empty());
	assert(controlFlow.traps[0].address == baseAddress + 0x0E);
	assert(controlFlow.traps[0].trap_number == 0x09F0);
	bool sawCallCandidate = false;
	for(const auto& fn : controlFlow.function_candidates) {
		if(fn.address == baseAddress + 0x20 && fn.calls == 1)
			sawCallCandidate = true;
	}
	assert(sawCallCandidate);
}

}


void	RunTests()
{
	test_stack_block_identifier();
	test_rom_analysis_contracts();

	assert(stackimport_context_size() <= 4096);
	assert(stackimport_api_version() == STACKIMPORT_API_VERSION);
	assert(std::strcmp(stackimport_version_string(), STACKIMPORT_VERSION_STRING) == 0);
	assert(stackimport_version_packed() == STACKIMPORT_VERSION_PACKED);
	assert(stackimport_version_packed() == 0x00000300u);
	assert(STACKIMPORT_VERSION_MAJOR == 0);
	assert(STACKIMPORT_VERSION_MINOR == 3);
	assert(STACKIMPORT_VERSION_PATCH == 0);
	assert(stackimport_context_abi_signature() != 0);
	assert(std::strcmp(stackimport_status_string(STACKIMPORT_STATUS_ABI_MISMATCH), "ABI mismatch") == 0);
	alignas(std::max_align_t) unsigned char storage[4096] = {};
	stackimport_context* context = nullptr;
	assert(stackimport_context_init(storage, sizeof(storage), &context) == STACKIMPORT_STATUS_OK);
	stackimport_context_deinit(context);
	assert(stackimport_context_init(storage, sizeof(storage), &context) == STACKIMPORT_STATUS_OK);
	const uint32_t contextSignature = stackimport_context_abi_signature();
	const uint32_t badContextSignature = contextSignature ^ 0xFFFFFFFFu;
	std::memcpy(storage, &badContextSignature, sizeof(badContextSignature));
	stackimport_import_options abiMismatchOptions = {};
	stackimport_import_options_init(&abiMismatchOptions);
	abiMismatchOptions.input_path = "/tmp/missing-stack";
	abiMismatchOptions.output_package_path = "/tmp/missing-stack.xstk";
	assert(stackimport_import(context, &abiMismatchOptions) == STACKIMPORT_STATUS_ABI_MISMATCH);
	std::memcpy(storage, &contextSignature, sizeof(contextSignature));
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

	test_resource_image_transforms();
	test_resource_package_writes();
	test_resource_metadata_transforms();
	test_resource_text_transforms();
	test_resource_code_transforms();
	test_resource_color_and_ui_transforms();

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
	const size_t cWavSize = stackimport_snd_to_wav(
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
	const size_t queriedWavSize = stackimport_snd_to_wav(
		multiCommandSnd.data(),
		multiCommandSnd.size(),
		nullptr,
		0,
		&cSoundError);
	assert(queriedWavSize == 48);
	assert(cSoundError == nullptr);
	assert(stackimport_snd_to_wav(
		multiCommandSnd.data(),
		multiCommandSnd.size(),
		wavBuffer,
		8,
		&cSoundError) == 0);
	assert(std::strcmp(cSoundError, "output buffer too small") == 0);
	assert(stackimport_snd_to_wav(
		multiCommandSnd.data(),
		multiCommandSnd.size(),
		nullptr,
		8,
		&cSoundError) == 0);
	assert(std::strcmp(cSoundError, "invalid output buffer") == 0);
	assert(stackimport_snd_to_wav(nullptr, 0, nullptr, 0, nullptr) == 0);

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

	stackimport_log_handler invalidLogHandler = {};
	stackimport_log_handler_init(&invalidLogHandler);
	assert(stackimport_context_create_with_log_handler(&invalidLogHandler, &context) == STACKIMPORT_STATUS_INVALID_ARGUMENT);

	LogHandlerState logHandlerState;
	stackimport_log_handler logHandler = {};
	stackimport_log_handler_init(&logHandler);
	logHandler.log = test_log_record;
	logHandler.user_data = &logHandlerState;
	stackimport_context* logHandlerContext = nullptr;
	assert(stackimport_context_create_with_log_handler(&logHandler, &logHandlerContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options logHandlerOptions = {};
	stackimport_import_options_init(&logHandlerOptions);
	const std::string logHandlerPackage = platformStackPath + ".log-handler.xstk";
	logHandlerOptions.input_path = platformStackPath.c_str();
	logHandlerOptions.output_package_path = logHandlerPackage.c_str();
	assert(stackimport_import(logHandlerContext, &logHandlerOptions) == STACKIMPORT_STATUS_OK);
	stackimport_context_destroy(logHandlerContext);
	assert(logHandlerState.messages > 0);
	assert(logHandlerState.saw_status_or_progress);

	LogHandlerState storageLogHandlerState;
	stackimport_log_handler storageLogHandler = {};
	stackimport_log_handler_init(&storageLogHandler);
	storageLogHandler.log = test_log_record;
	storageLogHandler.user_data = &storageLogHandlerState;
	alignas(std::max_align_t) unsigned char logHandlerStorage[4096] = {};
	stackimport_context* storageLogHandlerContext = nullptr;
	assert(stackimport_context_init_with_log_handler(
		logHandlerStorage,
		sizeof(logHandlerStorage),
		&storageLogHandler,
		&storageLogHandlerContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options storageLogHandlerOptions = {};
	stackimport_import_options_init(&storageLogHandlerOptions);
	const std::string storageLogHandlerPackage = platformStackPath + ".storage-log-handler.xstk";
	storageLogHandlerOptions.input_path = platformStackPath.c_str();
	storageLogHandlerOptions.output_package_path = storageLogHandlerPackage.c_str();
	assert(stackimport_import(storageLogHandlerContext, &storageLogHandlerOptions) == STACKIMPORT_STATUS_OK);
	stackimport_context_deinit(storageLogHandlerContext);
	assert(storageLogHandlerState.messages > 0);
	assert(storageLogHandlerState.saw_status_or_progress);

	LogHandlerState legacyLogHandlerState;
	stackimport_log_handler legacyLogHandler = {};
	stackimport_log_handler_init(&legacyLogHandler);
	legacyLogHandler.message = test_log_message;
	legacyLogHandler.user_data = &legacyLogHandlerState;
	stackimport_context* legacyLogHandlerContext = nullptr;
	assert(stackimport_context_create_with_log_handler(&legacyLogHandler, &legacyLogHandlerContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options legacyLogHandlerOptions = {};
	stackimport_import_options_init(&legacyLogHandlerOptions);
	const std::string legacyLogHandlerPackage = platformStackPath + ".legacy-log-handler.xstk";
	legacyLogHandlerOptions.input_path = platformStackPath.c_str();
	legacyLogHandlerOptions.output_package_path = legacyLogHandlerPackage.c_str();
	assert(stackimport_import(legacyLogHandlerContext, &legacyLogHandlerOptions) == STACKIMPORT_STATUS_OK);
	stackimport_context_destroy(legacyLogHandlerContext);
	assert(legacyLogHandlerState.messages > 0);
	assert(legacyLogHandlerState.saw_status_or_progress);

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
