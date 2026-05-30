/*
 *  TestsShared.cpp
 *  stackimport
 *
 *  Common test utilities, platform callbacks, helper classes, and fixture
 *  builders shared across all test suites.
 *
 */

#include "TestsShared.h"

bool stackimport_load_resource_fork(
	const std::string& fpath,
	const std::string& basePath,
	const std::string& stackFileName,
	stackimport::IResourceOutput* resourceOutput,
	std::vector<CResourceSummary>& resourceSummaries,
	std::string& resourceForkStatus,
	uint64_t& resourceForkBytes);

namespace TestShared {

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

auto TestResourceOutput::wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool
{
	return payload.format == stackimport::ResourcePayloadFormat::Native;
}

auto TestResourceOutput::on_resource_payload(const stackimport::ResourcePayload& payload) -> bool
{
	seen_native = payload.format == stackimport::ResourcePayloadFormat::Native;
	return true;
}

auto CountingResourceOutput::wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool
{
	wants_count++;
	return payload.format == stackimport::ResourcePayloadFormat::Native ||
		payload.format == stackimport::ResourcePayloadFormat::Rgba32 ||
		payload.format == stackimport::ResourcePayloadFormat::JsonUtf8 ||
		payload.format == stackimport::ResourcePayloadFormat::TextUtf8;
}

auto CountingResourceOutput::on_resource_payload(const stackimport::ResourcePayload& payload) -> bool
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

void append_u24be(std::vector<uint8_t>& data, uint32_t value)
{
	data.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
	data.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
	data.push_back(static_cast<uint8_t>(value & 0xFFu));
}

void append_fourcc(std::vector<uint8_t>& data, const char type[4])
{
	data.insert(data.end(), type, type + 4);
}

void append_atom(std::vector<uint8_t>& data, const char type[4], const std::vector<uint8_t>& body)
{
	append_u32be(data, static_cast<uint32_t>(body.size() + 8));
	append_fourcc(data, type);
	data.insert(data.end(), body.begin(), body.end());
}

std::vector<uint8_t> make_quicktime_fixture()
{
	std::vector<uint8_t> ftyp;
	append_fourcc(ftyp, "qt  ");
	append_u32be(ftyp, 0);
	append_fourcc(ftyp, "qt  ");

	std::vector<uint8_t> mvhd(100, 0);
	mvhd[0] = 0;
	rsrcd::write_u32be(mvhd.data() + 12, 600);
	rsrcd::write_u32be(mvhd.data() + 16, 1200);
	rsrcd::write_u32be(mvhd.data() + 20, 0x00010000);
	rsrcd::write_u16be(mvhd.data() + 24, 0x0100);
	rsrcd::write_u32be(mvhd.data() + 96, 2);

	std::vector<uint8_t> tkhd(84, 0);
	tkhd[0] = 0;
	rsrcd::write_u32be(tkhd.data() + 12, 1);
	rsrcd::write_u32be(tkhd.data() + 20, 1200);
	rsrcd::write_u32be(tkhd.data() + 76, 320u << 16u);
	rsrcd::write_u32be(tkhd.data() + 80, 240u << 16u);

	std::vector<uint8_t> mdhd(20, 0);
	rsrcd::write_u32be(mdhd.data() + 12, 600);
	rsrcd::write_u32be(mdhd.data() + 16, 1200);

	std::vector<uint8_t> hdlr(24, 0);
	std::memcpy(hdlr.data() + 8, "vide", 4);

	std::vector<uint8_t> stsd;
	append_u32be(stsd, 0);
	append_u32be(stsd, 1);
	append_u32be(stsd, 86);
	append_fourcc(stsd, "rpza");
	stsd.resize(stsd.size() + 24, 0);
	append_u16be(stsd, 320);
	append_u16be(stsd, 240);
	stsd.resize(8 + 86, 0);

	std::vector<uint8_t> stts;
	append_u32be(stts, 0);
	append_u32be(stts, 1);
	append_u32be(stts, 3);
	append_u32be(stts, 20);

	std::vector<uint8_t> stsc;
	append_u32be(stsc, 0);
	append_u32be(stsc, 1);
	append_u32be(stsc, 1);
	append_u32be(stsc, 2);
	append_u32be(stsc, 1);

	std::vector<uint8_t> stsz;
	append_u32be(stsz, 0);
	append_u32be(stsz, 0);
	append_u32be(stsz, 3);
	append_u32be(stsz, 12);
	append_u32be(stsz, 14);
	append_u32be(stsz, 16);

	std::vector<uint8_t> stco;
	append_u32be(stco, 0);
	append_u32be(stco, 2);
	append_u32be(stco, 128);
	append_u32be(stco, 256);

	std::vector<uint8_t> stbl;
	append_atom(stbl, "stsd", stsd);
	append_atom(stbl, "stts", stts);
	append_atom(stbl, "stsc", stsc);
	append_atom(stbl, "stsz", stsz);
	append_atom(stbl, "stco", stco);

	std::vector<uint8_t> minf;
	append_atom(minf, "stbl", stbl);

	std::vector<uint8_t> mdia;
	append_atom(mdia, "mdhd", mdhd);
	append_atom(mdia, "hdlr", hdlr);
	append_atom(mdia, "minf", minf);

	std::vector<uint8_t> trak;
	append_atom(trak, "tkhd", tkhd);
	append_atom(trak, "mdia", mdia);

	std::vector<uint8_t> moov;
	append_atom(moov, "mvhd", mvhd);
	append_atom(moov, "trak", trak);

	std::vector<uint8_t> mov;
	append_atom(mov, "ftyp", ftyp);
	append_atom(mov, "moov", moov);
	return mov;
}

std::vector<uint8_t> make_quicktime_pcm_fixture()
{
	std::vector<uint8_t> ftyp;
	append_fourcc(ftyp, "qt  ");
	append_u32be(ftyp, 0);
	append_fourcc(ftyp, "qt  ");

	std::vector<uint8_t> mdhd(20, 0);
	rsrcd::write_u32be(mdhd.data() + 12, 8000);
	rsrcd::write_u32be(mdhd.data() + 16, 2);

	std::vector<uint8_t> hdlr(24, 0);
	std::memcpy(hdlr.data() + 8, "soun", 4);

	const uint32_t sampleOffset = static_cast<uint32_t>(8 + ftyp.size() + 8);
	std::vector<uint8_t> stsd;
	append_u32be(stsd, 0);
	append_u32be(stsd, 1);
	append_u32be(stsd, 36);
	append_fourcc(stsd, "twos");
	stsd.resize(stsd.size() + 6, 0);
	append_u16be(stsd, 1);
	append_u16be(stsd, 0);
	append_u16be(stsd, 0);
	append_u32be(stsd, 0);
	append_u16be(stsd, 1);
	append_u16be(stsd, 16);
	append_u16be(stsd, 0);
	append_u16be(stsd, 0);
	append_u32be(stsd, 8000u << 16u);

	std::vector<uint8_t> stts;
	append_u32be(stts, 0);
	append_u32be(stts, 1);
	append_u32be(stts, 2);
	append_u32be(stts, 1);

	std::vector<uint8_t> stsc;
	append_u32be(stsc, 0);
	append_u32be(stsc, 1);
	append_u32be(stsc, 1);
	append_u32be(stsc, 2);
	append_u32be(stsc, 1);

	std::vector<uint8_t> stsz;
	append_u32be(stsz, 0);
	append_u32be(stsz, 0);
	append_u32be(stsz, 2);
	append_u32be(stsz, 2);
	append_u32be(stsz, 2);

	std::vector<uint8_t> stco;
	append_u32be(stco, 0);
	append_u32be(stco, 1);
	append_u32be(stco, sampleOffset);

	std::vector<uint8_t> stbl;
	append_atom(stbl, "stsd", stsd);
	append_atom(stbl, "stts", stts);
	append_atom(stbl, "stsc", stsc);
	append_atom(stbl, "stsz", stsz);
	append_atom(stbl, "stco", stco);

	std::vector<uint8_t> minf;
	append_atom(minf, "stbl", stbl);

	std::vector<uint8_t> mdia;
	append_atom(mdia, "mdhd", mdhd);
	append_atom(mdia, "hdlr", hdlr);
	append_atom(mdia, "minf", minf);

	std::vector<uint8_t> trak;
	append_atom(trak, "mdia", mdia);

	std::vector<uint8_t> moov;
	append_atom(moov, "trak", trak);

	std::vector<uint8_t> mdat = {0x01, 0x02, 0x03, 0x04};
	std::vector<uint8_t> mov;
	append_atom(mov, "ftyp", ftyp);
	append_atom(mov, "mdat", mdat);
	append_atom(mov, "moov", moov);
	return mov;
}

std::vector<uint8_t> make_cinepak_fixture()
{
	std::vector<uint8_t> codebook;
	append_u16be(codebook, 0x2200);
	append_u16be(codebook, 10);
	codebook.insert(codebook.end(), {80, 80, 80, 80, 128, 128});

	std::vector<uint8_t> vectors;
	append_u16be(vectors, 0x3200);
	append_u16be(vectors, 5);
	vectors.push_back(0);

	std::vector<uint8_t> strip;
	append_u16be(strip, 0x1000);
	append_u16be(strip, static_cast<uint16_t>(12 + codebook.size() + vectors.size()));
	append_u16be(strip, 0);
	append_u16be(strip, 0);
	append_u16be(strip, 4);
	append_u16be(strip, 4);
	strip.insert(strip.end(), codebook.begin(), codebook.end());
	strip.insert(strip.end(), vectors.begin(), vectors.end());

	std::vector<uint8_t> frame;
	frame.push_back(0);
	append_u24be(frame, static_cast<uint32_t>(10 + strip.size()));
	append_u16be(frame, 4);
	append_u16be(frame, 4);
	append_u16be(frame, 1);
	frame.insert(frame.end(), strip.begin(), strip.end());
	return frame;
}

std::vector<uint8_t> make_cinepak_inter_skip_fixture()
{
	std::vector<uint8_t> vectors;
	append_u16be(vectors, 0x3100);
	append_u16be(vectors, 8);
	append_u32be(vectors, 0);

	std::vector<uint8_t> strip;
	append_u16be(strip, 0x1100);
	append_u16be(strip, static_cast<uint16_t>(12 + vectors.size()));
	append_u16be(strip, 0);
	append_u16be(strip, 0);
	append_u16be(strip, 4);
	append_u16be(strip, 4);
	strip.insert(strip.end(), vectors.begin(), vectors.end());

	std::vector<uint8_t> frame;
	frame.push_back(1);
	append_u24be(frame, static_cast<uint32_t>(10 + strip.size()));
	append_u16be(frame, 4);
	append_u16be(frame, 4);
	append_u16be(frame, 1);
	frame.insert(frame.end(), strip.begin(), strip.end());
	return frame;
}

std::vector<uint8_t> make_rpza_single_color_fixture()
{
	std::vector<uint8_t> frame;
	frame.push_back(0xE1);
	append_u24be(frame, 7);
	frame.push_back(0xA0);
	append_u16be(frame, 0x7C00);
	return frame;
}

std::vector<uint8_t> make_rpza_literal_fixture()
{
	std::vector<uint8_t> frame;
	frame.push_back(0xE1);
	append_u24be(frame, 36);
	append_u16be(frame, 0x001F);
	for(uint16_t i = 1; i < 16; i++)
		append_u16be(frame, static_cast<uint16_t>(i));
	return frame;
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

MemoryStackReader::MemoryStackReader(rsrcd::Bytes bytes) : bytes_(bytes), pos_(0) {}

auto MemoryStackReader::read(uint8_t* dst, size_t len) -> size_t
{
	const size_t available = pos_ < bytes_.size ? bytes_.size - pos_ : 0;
	const size_t count = len < available ? len : available;
	if(count > 0)
		std::memcpy(dst, bytes_.data + pos_, count);
	pos_ += count;
	return count;
}

auto MemoryStackReader::seek(size_t pos) -> bool
{
	if(pos > bytes_.size)
		return false;
	pos_ = pos;
	return true;
}

auto MemoryStackReader::position() const -> size_t { return pos_; }
auto MemoryStackReader::size() const -> size_t { return bytes_.size; }

auto CapturingBlockOutput::on_block(const stackimport::BlockRef&, stackimport::IStackReader&) -> BlockResult
{
	block_count++;
	return BlockResult::Continue;
}

auto CapturingBlockOutput::on_error(const char* msg) -> bool
{
	error = msg ? msg : "";
	return false;
}

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
	append_u16be(rom, 0x4E71);
	append_u16be(rom, 0xA9F0);
	append_u16be(rom, 0x4E75);
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

std::vector<uint8_t> make_synthetic_kurt_rom_fixture()
{
	std::vector<uint8_t> rom;
	append_u16be(rom, 0x4E71);
	while(rom.size() < 0x40)
		rom.push_back(0);
	append_fourcc(rom, "STR ");
	append_u16be(rom, 129);
	rom.push_back(0x58);
	rom.push_back(4);
	rom.insert(rom.end(), {'T', 'e', 's', 't'});
	rom.insert(rom.end(), {'k', 'c', 'k', 'c'});
	append_fourcc(rom, "Kurt");
	append_u32be(rom, 0xC0A00000u);
	append_u32be(rom, 6);
	append_u32be(rom, 6);
	rom.insert(rom.end(), {5, 'W', 'o', 'r', 'l', 'd'});
	return rom;
}

std::vector<uint8_t> make_synthetic_inline_rom_fixture()
{
	std::vector<uint8_t> rom;
	append_u16be(rom, 0x4E71);
	while(rom.size() < 0x40)
		rom.push_back(0);
	rom.push_back(0x78);
	while(rom.size() < 0x48)
		rom.push_back(0);
	append_u32be(rom, 0);
	append_u32be(rom, 0x78);
	append_fourcc(rom, "clut");
	append_u16be(rom, 4);
	rom.push_back(0x58);
	rom.push_back(7);
	rom.insert(rom.end(), {'4', 'B', 'i', 't', 'S', 't', 'd'});
	while(rom.size() < 0x68)
		rom.push_back(0);
	append_u32be(rom, 0);
	append_u16be(rom, 0xC0A0);
	append_u16be(rom, 0);
	append_u32be(rom, 16);
	append_u32be(rom, 0);
	append_u32be(rom, 4);
	append_u16be(rom, 0x8000);
	append_u16be(rom, 0);
	append_u16be(rom, 0);
	append_u16be(rom, 0xFFFF);
	append_u16be(rom, 0xFFFF);
	append_u16be(rom, 0xFFFF);
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

std::vector<uint8_t> make_compact_4bit_pixmap_payload(bool explicitDepth)
{
	std::vector<uint8_t> data;
	append_u16be(data, 0x8002);
	append_u16be(data, 0);
	append_u16be(data, 0);
	append_u16be(data, 2);
	append_u16be(data, 4);
	if(explicitDepth)
		append_u16be(data, 4);
	data.insert(data.end(), {0x01, 0x23, 0x45, 0x67});
	return data;
}

}
