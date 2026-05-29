#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace stackimport::mov2qt {

struct Atom {
	std::string type;
	std::string source;
	uint64_t offset = 0;
	uint64_t size = 0;
	uint64_t header_size = 0;
	uint32_t depth = 0;
};

struct FileType {
	std::string major_brand;
	uint32_t minor_version = 0;
	std::vector<std::string> compatible_brands;
};

struct MovieHeader {
	bool present = false;
	uint32_t time_scale = 0;
	uint64_t duration = 0;
	double duration_seconds = 0.0;
	double preferred_rate = 0.0;
	double preferred_volume = 0.0;
	uint32_t next_track_id = 0;
};

struct SampleDescription {
	std::string format;
	std::string codec_name;
	std::string codec_family;
	std::string media_kind;
	std::string decoder_status;
	std::string permissive_decoder;
	std::string preferred_output;
	std::string notes;
	uint32_t size = 0;
	uint16_t data_reference_index = 0;
	uint16_t width = 0;
	uint16_t height = 0;
	uint16_t channel_count = 0;
	uint16_t sample_size_bits = 0;
	uint16_t compression_id = 0;
	uint16_t packet_size = 0;
	double sample_rate_hz = 0.0;
	std::vector<std::array<uint8_t, 4>> palette_rgba;
};

struct TimeToSampleEntry {
	uint32_t sample_count = 0;
	uint32_t sample_duration = 0;
};

struct SampleToChunkEntry {
	uint32_t first_chunk = 0;
	uint32_t samples_per_chunk = 0;
	uint32_t sample_description_id = 0;
};

struct SamplePacket {
	uint64_t index = 0;
	uint32_t chunk_index = 0;
	uint64_t offset = 0;
	uint32_t size = 0;
	uint64_t decode_time = 0;
	uint32_t duration = 0;
	uint32_t sample_description_id = 0;
};

struct Track {
	uint32_t id = 0;
	std::string handler_type;
	uint32_t time_scale = 0;
	uint64_t duration = 0;
	double duration_seconds = 0.0;
	double width = 0.0;
	double height = 0.0;
	uint64_t sample_count = 0;
	uint64_t chunk_count = 0;
	uint32_t constant_sample_size = 0;
	uint64_t variable_sample_size_count = 0;
	std::vector<SampleDescription> sample_descriptions;
	std::vector<TimeToSampleEntry> time_to_sample;
	std::vector<SampleToChunkEntry> sample_to_chunk;
	std::vector<uint64_t> chunk_offsets;
	std::vector<uint32_t> sample_sizes;
	std::vector<SamplePacket> sample_packets;
	bool sample_packet_preview_truncated = false;
};

struct Analysis {
	bool ok = false;
	std::string error;
	uint64_t file_size = 0;
	uint64_t resource_fork_size = 0;
	uint64_t movie_resource_count = 0;
	std::vector<Atom> atoms;
	FileType file_type;
	MovieHeader movie_header;
	std::vector<Track> tracks;
};

Analysis analyze(std::span<const uint8_t> data);
Analysis analyze(std::span<const uint8_t> data, std::span<const uint8_t> resource_fork);
std::string analysis_to_json(const Analysis& analysis, int indent = 0);
bool sample_packet_bytes(std::span<const uint8_t> data, const SamplePacket& packet, std::span<const uint8_t>& bytes, std::string& error);
bool decode_pcm_track_to_wav(std::span<const uint8_t> data, const Track& track, std::vector<uint8_t>& wav, std::string& error);

struct RgbaFrame {
	uint16_t width = 0;
	uint16_t height = 0;
	std::vector<uint8_t> rgba;
};

using CinepakFrame = RgbaFrame;

struct CinepakVector {
	std::array<uint8_t, 4> y = {};
	uint8_t u = 128;
	uint8_t v = 128;
	bool color = true;
};

struct CinepakStripCodebooks {
	std::array<CinepakVector, 256> v1 = {};
	std::array<CinepakVector, 256> v4 = {};
};

struct CinepakDecoderState {
	std::vector<CinepakStripCodebooks> strip_codebooks;
	RgbaFrame previous_frame;
	bool has_previous_frame = false;
};

bool decode_rpza_frame(std::span<const uint8_t> data, uint16_t width, uint16_t height, RgbaFrame& frame, std::string& error, const RgbaFrame* previous = nullptr);
bool decode_cinepak_frame(std::span<const uint8_t> data, RgbaFrame& frame, std::string& error, CinepakDecoderState* state = nullptr);
bool decode_qtrle_frame(
	std::span<const uint8_t> data,
	uint16_t width,
	uint16_t height,
	uint16_t depth,
	std::span<const std::array<uint8_t, 4>> palette,
	RgbaFrame& frame,
	std::string& error,
	const RgbaFrame* previous = nullptr);

} // namespace stackimport::mov2qt
