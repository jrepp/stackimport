/*
 *  TestsShared.h
 *  stackimport
 *
 *  Common test utilities, platform callbacks, helper classes, and fixture
 *  builders shared across all test suites.
 *
 */

#pragma once

#include "CStackFile.h"
#include "Mac68kDisassembly.h"
#include "RomDasm.h"
#include "StackImportSoundConverter.h"
#include "stackimport_c.h"
#include "stackimport_platform_internal.h"
#include "stackimport_rapidjson_allocator.h"
#include "stackimport/mov2qt.hpp"
#include <cassert>
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

namespace TestShared {

extern void* STACKIMPORT_CALL test_allocate(size_t size, size_t alignment, void*);
extern void STACKIMPORT_CALL test_deallocate(void* ptr, void*);
extern int STACKIMPORT_CALL test_resource_wants(const stackimport_resource_payload* payload, void*);
extern int STACKIMPORT_CALL test_resource_payload(const stackimport_resource_payload* payload, const void* data, size_t size, void*);

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

extern void* STACKIMPORT_CALL counting_allocate(size_t size, size_t alignment, void* user_data);
extern void STACKIMPORT_CALL counting_deallocate(void* ptr, void*);
extern stackimport_file_handle STACKIMPORT_CALL counting_open_file(const char* path, const char* mode, void* user_data);
extern size_t STACKIMPORT_CALL counting_read_file(stackimport_file_handle file, void* data, size_t size, void* user_data);
extern size_t STACKIMPORT_CALL counting_write_file(stackimport_file_handle file, const void* data, size_t size, void* user_data);
extern int STACKIMPORT_CALL counting_close_file(stackimport_file_handle file, void* user_data);
extern int STACKIMPORT_CALL counting_make_directory(const char* path, void*);

extern void STACKIMPORT_CALL test_log_message(uint32_t severity, const char* message, void* user_data);
extern void STACKIMPORT_CALL test_log_record(const stackimport_log_record* record, void* user_data);

extern bool has_resource_fork_suffix(const char* path);
extern stackimport_file_handle STACKIMPORT_CALL resource_fork_open_file(const char* path, const char* mode, void* user_data);
extern size_t STACKIMPORT_CALL resource_fork_read_file(stackimport_file_handle file, void* data, size_t size, void* user_data);
extern size_t STACKIMPORT_CALL resource_fork_write_file(stackimport_file_handle file, const void* data, size_t size, void* user_data);
extern int STACKIMPORT_CALL resource_fork_close_file(stackimport_file_handle file, void* user_data);

class TestResourceOutput final : public stackimport::IResourceOutput {
public:
	auto wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool override;
	auto on_resource_payload(const stackimport::ResourcePayload& payload) -> bool override;
	bool seen_native = false;
};

class CountingResourceOutput final : public stackimport::IResourceOutput {
public:
	auto wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool override;
	auto on_resource_payload(const stackimport::ResourcePayload& payload) -> bool override;
	auto on_resource_error(const stackimport::ResourceRef&, const char* msg) -> bool override;

	int wants_count = 0;
	int native_count = 0;
	int rgba_count = 0;
	int json_count = 0;
	int text_count = 0;
	int error_count = 0;
	uint32_t last_width = 0;
	uint32_t last_height = 0;
	size_t last_payload_size = 0;
	std::string last_json;
	std::string last_text;
	std::string last_error;
};

extern void write_basic_resource_fork_header(uint8_t* fork, uint32_t data_off, uint32_t map_off, uint32_t data_len, uint32_t map_len);
extern void append_u16be(std::vector<uint8_t>& data, uint16_t value);
extern void append_u32be(std::vector<uint8_t>& data, uint32_t value);
extern void append_u24be(std::vector<uint8_t>& data, uint32_t value);
extern void append_fourcc(std::vector<uint8_t>& data, const char type[4]);
extern void append_atom(std::vector<uint8_t>& data, const char type[4], const std::vector<uint8_t>& body);

extern std::vector<uint8_t> make_quicktime_fixture();
extern std::vector<uint8_t> make_quicktime_pcm_fixture();
extern std::vector<uint8_t> make_cinepak_fixture();
extern std::vector<uint8_t> make_cinepak_inter_skip_fixture();
extern std::vector<uint8_t> make_rpza_single_color_fixture();
extern std::vector<uint8_t> make_rpza_literal_fixture();
extern std::vector<uint8_t> make_snd_format2_fixture(bool extraNullCommand, uint32_t commandOffset);

extern std::vector<uint8_t> make_single_resource_fork(const char type[4], int16_t id, const std::vector<uint8_t>& payload);
extern CountingResourceOutput parse_single_resource(const char type[4], int16_t id, const std::vector<uint8_t>& payload);
extern void assert_rgba_resource(const char type[4], int16_t id, const std::vector<uint8_t>& payload, uint32_t width, uint32_t height);
extern void assert_json_resource_contains(const char type[4], int16_t id, const std::vector<uint8_t>& payload, std::initializer_list<const char*> needles);
extern void assert_text_resource_equals(const char type[4], int16_t id, const std::vector<uint8_t>& payload, const std::string& expected);

extern void append_block_header(std::vector<uint8_t>& data, uint32_t size, const char type[4], int32_t id);

class MemoryStackReader final : public stackimport::IStackReader {
public:
	explicit MemoryStackReader(rsrcd::Bytes bytes);

	auto read(uint8_t* dst, size_t len) -> size_t override;
	auto seek(size_t pos) -> bool override;
	auto position() const -> size_t override;
	auto size() const -> size_t override;

private:
	rsrcd::Bytes bytes_;
	size_t pos_;
};

class CapturingBlockOutput final : public stackimport::IBlockOutput {
public:
	auto on_block(const stackimport::BlockRef&, stackimport::IStackReader&) -> BlockResult override;
	auto on_error(const char* msg) -> bool override;

	int block_count = 0;
	std::string error;
};

extern void write_minimal_short_stak(const std::string& path);
extern void write_minimal_free_then_short_stak(const std::string& path);
extern std::string read_text_file(const std::string& path);

extern std::vector<uint8_t> make_synthetic_rom_fixture();
extern std::vector<uint8_t> make_synthetic_kurt_rom_fixture();
extern std::vector<uint8_t> make_synthetic_inline_rom_fixture();

extern std::vector<uint8_t> make_icon_payload();
extern std::vector<uint8_t> make_curs_payload();
extern std::vector<uint8_t> make_cicn_payload();
extern std::vector<uint8_t> make_crsr_payload();
extern std::vector<uint8_t> make_compact_4bit_pixmap_payload(bool explicitDepth);

}
