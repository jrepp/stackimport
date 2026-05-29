#include "stackimport/mov2qt.hpp"

#include "rsrcd.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <array>

namespace stackimport::mov2qt {

namespace {

void apply_codec_mapping(SampleDescription& description, const std::string& handler_type);

struct Parser {
	std::span<const uint8_t> data;
	Analysis analysis;
	std::string atom_source = "dataFork";

	uint8_t u1(uint64_t offset) const
	{
		if(offset >= data.size())
			return 0;
		return data[static_cast<size_t>(offset)];
	}

	uint16_t u2(uint64_t offset) const
	{
		if(offset + 2 > data.size())
			return 0;
		return static_cast<uint16_t>((static_cast<uint16_t>(data[static_cast<size_t>(offset)]) << 8) |
			static_cast<uint16_t>(data[static_cast<size_t>(offset + 1)]));
	}

	uint32_t u4(uint64_t offset) const
	{
		if(offset + 4 > data.size())
			return 0;
		return (static_cast<uint32_t>(data[static_cast<size_t>(offset)]) << 24) |
			(static_cast<uint32_t>(data[static_cast<size_t>(offset + 1)]) << 16) |
			(static_cast<uint32_t>(data[static_cast<size_t>(offset + 2)]) << 8) |
			static_cast<uint32_t>(data[static_cast<size_t>(offset + 3)]);
	}

	uint64_t u8(uint64_t offset) const
	{
		return (static_cast<uint64_t>(u4(offset)) << 32) | static_cast<uint64_t>(u4(offset + 4));
	}

	std::string fourcc(uint64_t offset) const
	{
		std::string out;
		if(offset + 4 > data.size())
			return out;
		for(uint64_t i = 0; i < 4; i++)
		{
			const uint8_t ch = data[static_cast<size_t>(offset + i)];
			out.push_back(ch >= 0x20 && ch <= 0x7E ? static_cast<char>(ch) : '.');
		}
		return out;
	}

	bool is_container(const std::string& type) const
	{
		return type == "dinf" || type == "mdia" || type == "minf" || type == "moof" ||
			type == "moov" || type == "stbl" || type == "traf" || type == "trak";
	}

	double fixed_16_16(uint64_t offset) const
	{
		const int16_t integer = static_cast<int16_t>(u2(offset));
		const uint16_t frac = u2(offset + 2);
		return static_cast<double>(integer) + (static_cast<double>(frac) / 65536.0);
	}

	double fixed_8_8(uint64_t offset) const
	{
		const int8_t integer = static_cast<int8_t>(u1(offset));
		const uint8_t frac = u1(offset + 1);
		return static_cast<double>(integer) + (static_cast<double>(frac) / 256.0);
	}

	Track* current_track()
	{
		if(analysis.tracks.empty())
			return nullptr;
		return &analysis.tracks.back();
	}

	void parse_ftyp(uint64_t body, uint64_t body_size)
	{
		if(body_size < 8)
			return;
		analysis.file_type.major_brand = fourcc(body);
		analysis.file_type.minor_version = u4(body + 4);
		for(uint64_t pos = body + 8; pos + 4 <= body + body_size; pos += 4)
			analysis.file_type.compatible_brands.push_back(fourcc(pos));
	}

	void parse_mvhd(uint64_t body, uint64_t body_size)
	{
		if(body_size < 4)
			return;
		const uint8_t version = u1(body);
		uint64_t pos = body + 4;
		MovieHeader header;
		header.present = true;
		if(version == 1)
		{
			if(body_size < 112)
				return;
			pos += 16;
			header.time_scale = u4(pos);
			header.duration = u8(pos + 4);
			pos += 12;
		}
		else
		{
			if(body_size < 100)
				return;
			pos += 8;
			header.time_scale = u4(pos);
			header.duration = u4(pos + 4);
			pos += 8;
		}
		header.duration_seconds = header.time_scale > 0 ? static_cast<double>(header.duration) / static_cast<double>(header.time_scale) : 0.0;
		header.preferred_rate = fixed_16_16(pos);
		header.preferred_volume = fixed_8_8(pos + 4);
		header.next_track_id = u4(body + (version == 1 ? 108 : 96));
		analysis.movie_header = header;
	}

	void parse_tkhd(uint64_t body, uint64_t body_size)
	{
		Track* track = current_track();
		if(!track || body_size < 4)
			return;
		const uint8_t version = u1(body);
		if(version == 1)
		{
			if(body_size < 104)
				return;
			track->id = u4(body + 20);
			track->duration = u8(body + 28);
			track->width = fixed_16_16(body + 96);
			track->height = fixed_16_16(body + 100);
		}
		else
		{
			if(body_size < 84)
				return;
			track->id = u4(body + 12);
			track->duration = u4(body + 20);
			track->width = fixed_16_16(body + 76);
			track->height = fixed_16_16(body + 80);
		}
	}

	void parse_mdhd(uint64_t body, uint64_t body_size)
	{
		Track* track = current_track();
		if(!track || body_size < 4)
			return;
		const uint8_t version = u1(body);
		if(version == 1)
		{
			if(body_size < 32)
				return;
			track->time_scale = u4(body + 20);
			track->duration = u8(body + 24);
		}
		else
		{
			if(body_size < 20)
				return;
			track->time_scale = u4(body + 12);
			track->duration = u4(body + 16);
		}
		track->duration_seconds = track->time_scale > 0 ? static_cast<double>(track->duration) / static_cast<double>(track->time_scale) : 0.0;
	}

	void parse_hdlr(uint64_t body, uint64_t body_size)
	{
		Track* track = current_track();
		if(!track || body_size < 12)
			return;
		const std::string subtype = fourcc(body + 8);
		if(track->handler_type.empty() || subtype == "vide" || subtype == "soun" || subtype == "text" || subtype == "tmcd")
			track->handler_type = subtype;
	}

	void parse_stsd(uint64_t body, uint64_t body_size)
	{
		Track* track = current_track();
		if(!track || body_size < 8)
			return;
		const uint32_t entry_count = u4(body + 4);
		uint64_t pos = body + 8;
		const uint64_t end = body + body_size;
		for(uint32_t i = 0; i < entry_count && pos + 8 <= end; i++)
		{
			const uint32_t entry_size = u4(pos);
			if(entry_size < 8 || pos + entry_size > end)
				break;
			SampleDescription description;
			description.size = entry_size;
			description.format = fourcc(pos + 4);
			apply_codec_mapping(description, track->handler_type);
			if(entry_size >= 16)
				description.data_reference_index = u2(pos + 14);
			if(entry_size >= 36 && track->handler_type == "vide")
			{
				description.width = u2(pos + 32);
				description.height = u2(pos + 34);
			}
			track->sample_descriptions.push_back(description);
			pos += entry_size;
		}
	}

	void parse_stts(uint64_t body, uint64_t body_size)
	{
		Track* track = current_track();
		if(!track || body_size < 8)
			return;
		const uint32_t entry_count = u4(body + 4);
		uint64_t sample_count = 0;
		uint64_t pos = body + 8;
		for(uint32_t i = 0; i < entry_count && pos + 8 <= body + body_size; i++, pos += 8)
			sample_count += u4(pos);
		if(sample_count > 0)
			track->sample_count = sample_count;
	}

	void parse_stsz(uint64_t body, uint64_t body_size)
	{
		Track* track = current_track();
		if(!track || body_size < 12)
			return;
		const uint32_t sample_count = u4(body + 8);
		if(sample_count > 0)
			track->sample_count = sample_count;
	}

	void parse_chunk_offsets(uint64_t body, uint64_t body_size)
	{
		Track* track = current_track();
		if(!track || body_size < 8)
			return;
		track->chunk_count = u4(body + 4);
	}

	void parse_leaf(const std::string& type, uint64_t body, uint64_t body_size)
	{
		if(type == "ftyp")
			parse_ftyp(body, body_size);
		else if(type == "mvhd")
			parse_mvhd(body, body_size);
		else if(type == "tkhd")
			parse_tkhd(body, body_size);
		else if(type == "mdhd")
			parse_mdhd(body, body_size);
		else if(type == "hdlr")
			parse_hdlr(body, body_size);
		else if(type == "stsd")
			parse_stsd(body, body_size);
		else if(type == "stts")
			parse_stts(body, body_size);
		else if(type == "stsz")
			parse_stsz(body, body_size);
		else if(type == "stco" || type == "co64")
			parse_chunk_offsets(body, body_size);
	}

	bool parse_atoms(uint64_t begin, uint64_t end, uint32_t depth)
	{
		uint64_t pos = begin;
		while(pos < end)
		{
			if(end - pos < 8)
			{
				analysis.error = "truncated atom header";
				return false;
			}
			const uint32_t len32 = u4(pos);
			const std::string type = fourcc(pos + 4);
			uint64_t header_size = 8;
			uint64_t size = len32;
			if(len32 == 1)
			{
				if(end - pos < 16)
				{
					analysis.error = "truncated extended atom header";
					return false;
				}
				header_size = 16;
				size = u8(pos + 8);
			}
			else if(len32 == 0)
			{
				size = end - pos;
			}
			if(size < header_size || pos + size > end)
			{
				analysis.error = "invalid atom size";
				return false;
			}
			analysis.atoms.push_back(Atom{type, atom_source, pos, size, header_size, depth});
			const uint64_t body = pos + header_size;
			const uint64_t body_size = size - header_size;
			if(type == "trak")
				analysis.tracks.push_back(Track{});
			parse_leaf(type, body, body_size);
			if(is_container(type) && !parse_atoms(body, body + body_size, depth + 1))
				return false;
			pos += size;
		}
		return true;
	}
};

void apply_codec_mapping(SampleDescription& description, const std::string& handler_type)
{
	description.media_kind = handler_type == "soun" ? "audio" : (handler_type == "vide" ? "video" : "unknown");
	description.decoder_status = "unsupported";
	description.permissive_decoder = "none_found";
	description.preferred_output = description.media_kind == "audio" ? "wav" : "rgba32_frames";
	description.notes = "No decoder mapping is available yet.";

	if(description.format == "cvid")
	{
		description.codec_name = "Cinepak";
		description.codec_family = "vector_quantized_interframe_video";
		description.decoder_status = "research";
		description.permissive_decoder = "none_found_yet";
		description.preferred_output = "rgba32_frames";
		description.notes = "FFmpeg has an LGPL Cinepak decoder and encoder; current permissive path is a clean-room decoder from public format notes.";
	}
	else if(description.format == "rpza")
	{
		description.codec_name = "Apple Video / Road Pizza";
		description.codec_family = "rgb555_block_vector_quantized_video";
		description.decoder_status = "candidate_clean_room";
		description.permissive_decoder = "none_found_yet";
		description.preferred_output = "rgba32_frames";
		description.notes = "Simple 4x4 RGB555 block codec; public format notes make this a good early clean-room decoder candidate.";
	}
	else if(description.format == "rle ")
	{
		description.codec_name = "QuickTime Animation / QTRLE";
		description.codec_family = "run_length_encoded_video";
		description.decoder_status = "candidate_clean_room";
		description.permissive_decoder = "none_found_yet";
		description.preferred_output = "rgba32_frames";
		description.notes = "RLE video with bit depths carried by the sample description; public format notes make this a feasible clean-room decoder.";
	}
	else if(description.format == "raw ")
	{
		description.codec_name = handler_type == "soun" ? "Unsigned 8-bit PCM" : "Raw samples";
		description.codec_family = handler_type == "soun" ? "pcm_audio" : "raw_media";
		description.decoder_status = handler_type == "soun" ? "internal_candidate" : "research";
		description.permissive_decoder = "not_required";
		description.preferred_output = handler_type == "soun" ? "wav_pcm_u8" : "raw_copy";
		description.notes = handler_type == "soun" ? "Uncompressed samples can be wrapped or converted to WAV without a third-party codec." : "Raw video/media interpretation needs more sample-description fields.";
	}
	else if(description.format == "twos")
	{
		description.codec_name = "Signed big-endian PCM";
		description.codec_family = "pcm_audio";
		description.decoder_status = "internal_candidate";
		description.permissive_decoder = "not_required";
		description.preferred_output = "wav_pcm_s16";
		description.notes = "Uncompressed signed big-endian PCM can be byte-swapped and wrapped as WAV internally.";
	}
	else if(description.format == "sowt")
	{
		description.codec_name = "Signed little-endian PCM";
		description.codec_family = "pcm_audio";
		description.decoder_status = "internal_candidate";
		description.permissive_decoder = "not_required";
		description.preferred_output = "wav_pcm_s16";
		description.notes = "Uncompressed signed little-endian PCM can be wrapped as WAV internally.";
	}
	else
	{
		description.codec_name = description.format.empty() ? "unknown" : description.format;
		description.codec_family = "unknown";
	}
}

bool parse_resource_moov_atoms(Parser& parser, std::span<const uint8_t> resource_fork)
{
	if(resource_fork.empty())
		return true;
	rsrcd::VecParserOutput<64> resources;
	rsrcd::Parser resource_parser;
	const rsrcd::Result result = resource_parser.parse_fork(rsrcd::Bytes{resource_fork.data(), resource_fork.size()}, resources);
	if(!result)
	{
		parser.analysis.error = result.message() ? result.message() : "resource fork parse failed";
		return false;
	}
	for(size_t i = 0; i < resources.count(); i++)
	{
		const rsrcd::ResRef& resource = resources.at(i);
		if(resource.type.size != 4 || std::memcmp(resource.type.data, "moov", 4) != 0 || resource.data.size < 8)
			continue;
		const std::span<const uint8_t> previous_data = parser.data;
		const std::string previous_source = parser.atom_source;
		parser.data = std::span<const uint8_t>(resource.data.data, resource.data.size);
		parser.atom_source = "resourceFork:moov#" + std::to_string(resource.id);
		parser.analysis.movie_resource_count++;
		const bool ok = parser.parse_atoms(0, resource.data.size, 0);
		parser.data = previous_data;
		parser.atom_source = previous_source;
		if(!ok)
			return false;
	}
	return true;
}

std::string json_escape(const std::string& text)
{
	std::string out;
	for(const char ch : text)
	{
		switch(ch)
		{
			case '\\':
				out += "\\\\";
				break;
			case '"':
				out += "\\\"";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				if(static_cast<unsigned char>(ch) < 0x20)
				{
					char buffer[7] = {};
					std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned char>(ch));
					out += buffer;
				}
				else
					out.push_back(ch);
				break;
		}
	}
	return out;
}

std::string indent_text(int count)
{
	return std::string(static_cast<size_t>(std::max(count, 0)), ' ');
}

void append_number(std::string& out, double value)
{
	char buffer[64] = {};
	std::snprintf(buffer, sizeof(buffer), "%.6f", value);
	out += buffer;
	while(out.size() > 1 && out.back() == '0')
		out.pop_back();
	if(!out.empty() && out.back() == '.')
		out.push_back('0');
}

uint16_t read_be16(std::span<const uint8_t> data, size_t offset)
{
	if(offset + 2 > data.size())
		return 0;
	return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8u) | static_cast<uint16_t>(data[offset + 1]));
}

uint32_t read_be24(std::span<const uint8_t> data, size_t offset)
{
	if(offset + 3 > data.size())
		return 0;
	return (static_cast<uint32_t>(data[offset]) << 16u) |
		(static_cast<uint32_t>(data[offset + 1]) << 8u) |
		static_cast<uint32_t>(data[offset + 2]);
}

uint32_t read_be32(std::span<const uint8_t> data, size_t offset)
{
	if(offset + 4 > data.size())
		return 0;
	return (static_cast<uint32_t>(data[offset]) << 24u) |
		(static_cast<uint32_t>(data[offset + 1]) << 16u) |
		(static_cast<uint32_t>(data[offset + 2]) << 8u) |
		static_cast<uint32_t>(data[offset + 3]);
}

struct CinepakVector {
	std::array<uint8_t, 4> y = {};
	int8_t u = 0;
	int8_t v = 0;
	bool color = true;
};

struct CinepakStripState {
	std::array<CinepakVector, 256> v1 = {};
	std::array<CinepakVector, 256> v4 = {};
};

uint8_t clamp_u8(int value)
{
	if(value < 0)
		return 0;
	if(value > 255)
		return 255;
	return static_cast<uint8_t>(value);
}

void write_cinepak_pixel(CinepakFrame& frame, uint16_t x, uint16_t y, const CinepakVector& vector, uint8_t y_value)
{
	if(x >= frame.width || y >= frame.height)
		return;
	const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)) * 4u;
	if(index + 3 >= frame.rgba.size())
		return;
	if(vector.color)
	{
		const int yy = static_cast<int>(y_value);
		const int uu = static_cast<int>(vector.u);
		const int vv = static_cast<int>(vector.v);
		frame.rgba[index + 0] = clamp_u8(yy + (2 * vv));
		frame.rgba[index + 1] = clamp_u8(yy - (uu / 2) - vv);
		frame.rgba[index + 2] = clamp_u8(yy + (2 * uu));
	}
	else
	{
		frame.rgba[index + 0] = y_value;
		frame.rgba[index + 1] = y_value;
		frame.rgba[index + 2] = y_value;
	}
	frame.rgba[index + 3] = 255;
}

void draw_v1_block(CinepakFrame& frame, uint16_t x, uint16_t y, const CinepakVector& vector)
{
	for(uint16_t dy = 0; dy < 4; dy++)
	{
		for(uint16_t dx = 0; dx < 4; dx++)
		{
			const size_t y_index = static_cast<size_t>((dy >= 2 ? 2 : 0) + (dx >= 2 ? 1 : 0));
			write_cinepak_pixel(frame, static_cast<uint16_t>(x + dx), static_cast<uint16_t>(y + dy), vector, vector.y[y_index]);
		}
	}
}

void draw_vector_2x2(CinepakFrame& frame, uint16_t x, uint16_t y, const CinepakVector& vector)
{
	write_cinepak_pixel(frame, x, y, vector, vector.y[0]);
	write_cinepak_pixel(frame, static_cast<uint16_t>(x + 1), y, vector, vector.y[1]);
	write_cinepak_pixel(frame, x, static_cast<uint16_t>(y + 1), vector, vector.y[2]);
	write_cinepak_pixel(frame, static_cast<uint16_t>(x + 1), static_cast<uint16_t>(y + 1), vector, vector.y[3]);
}

void draw_v4_block(CinepakFrame& frame, uint16_t x, uint16_t y, const std::array<CinepakVector, 256>& codebook, const uint8_t indices[4])
{
	draw_vector_2x2(frame, x, y, codebook[indices[0]]);
	draw_vector_2x2(frame, static_cast<uint16_t>(x + 2), y, codebook[indices[1]]);
	draw_vector_2x2(frame, x, static_cast<uint16_t>(y + 2), codebook[indices[2]]);
	draw_vector_2x2(frame, static_cast<uint16_t>(x + 2), static_cast<uint16_t>(y + 2), codebook[indices[3]]);
}

bool read_cinepak_vector(std::span<const uint8_t> data, size_t& pos, bool color, CinepakVector& vector)
{
	const size_t vector_size = color ? 6u : 4u;
	if(pos + vector_size > data.size())
		return false;
	for(size_t i = 0; i < 4; i++)
		vector.y[i] = data[pos + i];
	vector.color = color;
	if(color)
	{
		vector.u = static_cast<int8_t>(data[pos + 4]);
		vector.v = static_cast<int8_t>(data[pos + 5]);
	}
	else
	{
		vector.u = 0;
		vector.v = 0;
	}
	pos += vector_size;
	return true;
}

bool decode_full_codebook_chunk(std::span<const uint8_t> payload, bool color, std::array<CinepakVector, 256>& codebook)
{
	size_t pos = 0;
	size_t index = 0;
	while(pos < payload.size() && index < codebook.size())
	{
		if(!read_cinepak_vector(payload, pos, color, codebook[index]))
			return false;
		index++;
	}
	return true;
}

bool decode_selective_codebook_chunk(std::span<const uint8_t> payload, bool color, std::array<CinepakVector, 256>& codebook)
{
	size_t pos = 0;
	size_t index = 0;
	while(pos + 4 <= payload.size() && index < codebook.size())
	{
		const uint32_t flags = read_be32(payload, pos);
		pos += 4;
		for(uint32_t bit = 0; bit < 32u && index < codebook.size(); bit++, index++)
		{
			if((flags & (0x80000000u >> bit)) == 0)
				continue;
			if(!read_cinepak_vector(payload, pos, color, codebook[index]))
				return false;
		}
	}
	return pos == payload.size();
}

struct CinepakBlockCursor {
	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t left = 0;
	uint16_t right = 0;
	uint16_t bottom = 0;

	bool done() const
	{
		return y >= bottom;
	}

	void advance()
	{
		x = static_cast<uint16_t>(x + 4);
		if(x >= right)
		{
			x = left;
			y = static_cast<uint16_t>(y + 4);
		}
	}
};

bool decode_vector_chunk(std::span<const uint8_t> payload, uint16_t chunk_id, CinepakStripState& state, CinepakFrame& frame, CinepakBlockCursor cursor)
{
	size_t pos = 0;
	uint32_t flags = 0;
	uint32_t bits_left = 0;
	auto next_bit = [&]() -> int {
		if(bits_left == 0)
		{
			if(pos + 4 > payload.size())
				return -1;
			flags = read_be32(payload, pos);
			pos += 4;
			bits_left = 32;
		}
		const int bit = (flags & 0x80000000u) != 0 ? 1 : 0;
		flags <<= 1u;
		bits_left--;
		return bit;
	};

	while(!cursor.done() && pos < payload.size())
	{
		if(chunk_id == 0x3200)
		{
			const uint8_t index = payload[pos++];
			draw_v1_block(frame, cursor.x, cursor.y, state.v1[index]);
			cursor.advance();
			continue;
		}

		if(chunk_id == 0x3000)
		{
			const int bit = next_bit();
			if(bit < 0)
				return false;
			if(bit == 0)
			{
				if(pos >= payload.size())
					return false;
				const uint8_t index = payload[pos++];
				draw_v1_block(frame, cursor.x, cursor.y, state.v1[index]);
			}
			else
			{
				if(pos + 4 > payload.size())
					return false;
				const uint8_t indices[4] = {payload[pos], payload[pos + 1], payload[pos + 2], payload[pos + 3]};
				pos += 4;
				draw_v4_block(frame, cursor.x, cursor.y, state.v4, indices);
			}
			cursor.advance();
			continue;
		}

		if(chunk_id == 0x3100)
		{
			const int first = next_bit();
			if(first < 0)
				return false;
			if(first == 0)
			{
				cursor.advance();
				continue;
			}
			const int second = next_bit();
			if(second < 0)
				return false;
			if(second == 0)
			{
				if(pos >= payload.size())
					return false;
				const uint8_t index = payload[pos++];
				draw_v1_block(frame, cursor.x, cursor.y, state.v1[index]);
			}
			else
			{
				if(pos + 4 > payload.size())
					return false;
				const uint8_t indices[4] = {payload[pos], payload[pos + 1], payload[pos + 2], payload[pos + 3]};
				pos += 4;
				draw_v4_block(frame, cursor.x, cursor.y, state.v4, indices);
			}
			cursor.advance();
			continue;
		}

		return false;
	}
	return true;
}

} // namespace

Analysis analyze(std::span<const uint8_t> data)
{
	return analyze(data, std::span<const uint8_t>());
}

Analysis analyze(std::span<const uint8_t> data, std::span<const uint8_t> resource_fork)
{
	Parser parser;
	parser.data = data;
	parser.analysis.file_size = data.size();
	parser.analysis.resource_fork_size = resource_fork.size();
	parser.analysis.ok = parser.parse_atoms(0, data.size(), 0) && parse_resource_moov_atoms(parser, resource_fork);
	if(parser.analysis.ok && parser.analysis.atoms.empty())
	{
		parser.analysis.ok = false;
		parser.analysis.error = "no atoms";
	}
	return parser.analysis;
}

std::string analysis_to_json(const Analysis& analysis, int indent)
{
	const std::string i0 = indent_text(indent);
	const std::string i1 = indent_text(indent + 2);
	const std::string i2 = indent_text(indent + 4);
	const std::string i3 = indent_text(indent + 6);
	std::string out;
	out += i0 + "{\n";
	out += i1 + "\"format\": \"stackimport.quicktimeAnalysis\",\n";
	out += i1 + "\"schemaVersion\": 1,\n";
	out += i1 + "\"parser\": \"vendor/mov2qt quicktime_mov wrapper\",\n";
	out += i1 + "\"schema\": \"kaitai quicktime_mov CC0-1.0\",\n";
	out += i1 + "\"ok\": ";
	out += analysis.ok ? "true" : "false";
	out += ",\n";
	out += i1 + "\"error\": \"" + json_escape(analysis.error) + "\",\n";
	out += i1 + "\"fileSize\": " + std::to_string(analysis.file_size) + ",\n";
	out += i1 + "\"resourceForkSize\": " + std::to_string(analysis.resource_fork_size) + ",\n";
	out += i1 + "\"movieResourceCount\": " + std::to_string(analysis.movie_resource_count) + ",\n";
	out += i1 + "\"fileType\": {\"majorBrand\":\"" + json_escape(analysis.file_type.major_brand) + "\",\"minorVersion\":" + std::to_string(analysis.file_type.minor_version) + ",\"compatibleBrands\":[";
	for(size_t idx = 0; idx < analysis.file_type.compatible_brands.size(); idx++)
	{
		if(idx > 0)
			out += ",";
		out += "\"" + json_escape(analysis.file_type.compatible_brands[idx]) + "\"";
	}
	out += "]},\n";
	out += i1 + "\"movieHeader\": {\"present\": ";
	out += analysis.movie_header.present ? "true" : "false";
	out += ",\"timeScale\":" + std::to_string(analysis.movie_header.time_scale);
	out += ",\"duration\":" + std::to_string(analysis.movie_header.duration);
	out += ",\"durationSeconds\":";
	append_number(out, analysis.movie_header.duration_seconds);
	out += ",\"preferredRate\":";
	append_number(out, analysis.movie_header.preferred_rate);
	out += ",\"preferredVolume\":";
	append_number(out, analysis.movie_header.preferred_volume);
	out += ",\"nextTrackId\":" + std::to_string(analysis.movie_header.next_track_id) + "},\n";
	out += i1 + "\"tracks\": [\n";
	for(size_t idx = 0; idx < analysis.tracks.size(); idx++)
	{
		const Track& track = analysis.tracks[idx];
		if(idx > 0)
			out += ",\n";
		out += i2 + "{";
		out += "\"id\":" + std::to_string(track.id);
		out += ",\"handlerType\":\"" + json_escape(track.handler_type) + "\"";
		out += ",\"timeScale\":" + std::to_string(track.time_scale);
		out += ",\"duration\":" + std::to_string(track.duration);
		out += ",\"durationSeconds\":";
		append_number(out, track.duration_seconds);
		out += ",\"width\":";
		append_number(out, track.width);
		out += ",\"height\":";
		append_number(out, track.height);
		out += ",\"sampleCount\":" + std::to_string(track.sample_count);
		out += ",\"chunkCount\":" + std::to_string(track.chunk_count);
		out += ",\"sampleDescriptions\":[";
		for(size_t desc_idx = 0; desc_idx < track.sample_descriptions.size(); desc_idx++)
		{
			const SampleDescription& desc = track.sample_descriptions[desc_idx];
			if(desc_idx > 0)
				out += ",";
			out += "{\"format\":\"" + json_escape(desc.format) + "\",\"size\":" + std::to_string(desc.size);
			out += ",\"codecName\":\"" + json_escape(desc.codec_name) + "\"";
			out += ",\"codecFamily\":\"" + json_escape(desc.codec_family) + "\"";
			out += ",\"mediaKind\":\"" + json_escape(desc.media_kind) + "\"";
			out += ",\"decoderStatus\":\"" + json_escape(desc.decoder_status) + "\"";
			out += ",\"permissiveDecoder\":\"" + json_escape(desc.permissive_decoder) + "\"";
			out += ",\"preferredOutput\":\"" + json_escape(desc.preferred_output) + "\"";
			out += ",\"notes\":\"" + json_escape(desc.notes) + "\"";
			out += ",\"dataReferenceIndex\":" + std::to_string(desc.data_reference_index);
			out += ",\"width\":" + std::to_string(desc.width);
			out += ",\"height\":" + std::to_string(desc.height) + "}";
		}
		out += "]}";
	}
	out += "\n" + i1 + "],\n";
	out += i1 + "\"atoms\": [\n";
	for(size_t idx = 0; idx < analysis.atoms.size(); idx++)
	{
		const Atom& atom = analysis.atoms[idx];
		if(idx > 0)
			out += ",\n";
		out += i2 + "{\"type\":\"" + json_escape(atom.type) + "\",\"source\":\"" + json_escape(atom.source) + "\",\"offset\":" + std::to_string(atom.offset);
		out += ",\"size\":" + std::to_string(atom.size);
		out += ",\"headerSize\":" + std::to_string(atom.header_size);
		out += ",\"depth\":" + std::to_string(atom.depth) + "}";
	}
	out += "\n" + i1 + "]\n";
	out += i0 + "}";
	(void)i3;
	return out;
}

bool decode_cinepak_frame(std::span<const uint8_t> data, CinepakFrame& frame, std::string& error)
{
	error.clear();
	frame = CinepakFrame{};
	if(data.size() < 10)
	{
		error = "cinepak frame too small";
		return false;
	}

	const uint8_t flags = data[0];
	const uint32_t declared_size = read_be24(data, 1);
	if(declared_size > data.size())
	{
		error = "cinepak declared frame size exceeds input";
		return false;
	}
	const size_t frame_size = declared_size > 0 ? static_cast<size_t>(declared_size) : data.size();
	frame.width = read_be16(data, 4);
	frame.height = read_be16(data, 6);
	const uint16_t strip_count = read_be16(data, 8);
	if(frame.width == 0 || frame.height == 0 || strip_count == 0)
	{
		error = "invalid cinepak frame dimensions or strip count";
		return false;
	}
	frame.rgba.assign(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4u, 0);

	CinepakStripState previous_strip;
	uint16_t previous_bottom_y = 0;
	size_t pos = 10;
	for(uint16_t strip_index = 0; strip_index < strip_count; strip_index++)
	{
		if(pos + 12 > frame_size)
		{
			error = "truncated cinepak strip header";
			return false;
		}
		CinepakStripState strip_state = (flags & 0x01u) != 0 ? previous_strip : CinepakStripState{};
		const uint16_t strip_id = read_be16(data, pos);
		const uint16_t strip_size = read_be16(data, pos + 2);
		const uint16_t top_y = read_be16(data, pos + 4);
		const uint16_t left_x = read_be16(data, pos + 6);
		uint16_t bottom_y = read_be16(data, pos + 8);
		const uint16_t right_x = read_be16(data, pos + 10);
		if(strip_size < 12 || pos + strip_size > frame_size)
		{
			error = "invalid cinepak strip size";
			return false;
		}
		if(strip_id != 0x1000 && strip_id != 0x1100)
		{
			error = "unsupported cinepak strip id";
			return false;
		}
		if(top_y == 0 && strip_index > 0)
			bottom_y = static_cast<uint16_t>(previous_bottom_y + bottom_y);

		const size_t strip_end = pos + strip_size;
		size_t chunk_pos = pos + 12;
		while(chunk_pos + 4 <= strip_end)
		{
			const uint16_t chunk_id = read_be16(data, chunk_pos);
			const uint16_t chunk_size = read_be16(data, chunk_pos + 2);
			if(chunk_size < 4 || chunk_pos + chunk_size > strip_end)
			{
				error = "invalid cinepak chunk size";
				return false;
			}
			const std::span<const uint8_t> payload(data.data() + chunk_pos + 4, static_cast<size_t>(chunk_size - 4));
			bool ok = true;
			switch(chunk_id)
			{
				case 0x2000:
					ok = decode_full_codebook_chunk(payload, true, strip_state.v4);
					break;
				case 0x2200:
					ok = decode_full_codebook_chunk(payload, true, strip_state.v1);
					break;
				case 0x2400:
					ok = decode_full_codebook_chunk(payload, false, strip_state.v4);
					break;
				case 0x2600:
					ok = decode_full_codebook_chunk(payload, false, strip_state.v1);
					break;
				case 0x2100:
					ok = decode_selective_codebook_chunk(payload, true, strip_state.v4);
					break;
				case 0x2300:
					ok = decode_selective_codebook_chunk(payload, true, strip_state.v1);
					break;
				case 0x2500:
					ok = decode_selective_codebook_chunk(payload, false, strip_state.v4);
					break;
				case 0x2700:
					ok = decode_selective_codebook_chunk(payload, false, strip_state.v1);
					break;
				case 0x3000:
				case 0x3100:
				case 0x3200:
					ok = decode_vector_chunk(payload, chunk_id, strip_state, frame, CinepakBlockCursor{left_x, top_y, left_x, right_x, bottom_y});
					break;
				default:
					break;
			}
			if(!ok)
			{
				error = "failed to decode cinepak chunk";
				return false;
			}
			chunk_pos += chunk_size;
		}
		previous_strip = strip_state;
		previous_bottom_y = bottom_y;
		pos = strip_end;
	}
	return true;
}

} // namespace stackimport::mov2qt
