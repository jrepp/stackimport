#pragma once

#include <cstdint>
#include <cstddef>
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
	std::vector<SampleDescription> sample_descriptions;
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

struct CinepakFrame {
	uint16_t width = 0;
	uint16_t height = 0;
	std::vector<uint8_t> rgba;
};

bool decode_cinepak_frame(std::span<const uint8_t> data, CinepakFrame& frame, std::string& error);

} // namespace stackimport::mov2qt
