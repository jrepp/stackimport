/*
 *  TestsQuickTime.cpp
 *  stackimport
 *
 *  QuickTime/mov2qt analysis and video codec decoding tests.
 *
 */

#include "TestsShared.h"

namespace TestQuickTime {

void RunTests()
{
	const std::vector<uint8_t> movFixture = TestShared::make_quicktime_fixture();
	const stackimport::mov2qt::Analysis movAnalysis = stackimport::mov2qt::analyze(std::span<const uint8_t>(movFixture.data(), movFixture.size()));
	assert(movAnalysis.ok);
	assert(movAnalysis.movie_header.present);
	assert(movAnalysis.movie_header.time_scale == 600);
	assert(movAnalysis.movie_header.duration == 1200);
	assert(movAnalysis.tracks.size() == 1);
	assert(movAnalysis.tracks[0].handler_type == "vide");
	assert(movAnalysis.tracks[0].sample_count == 3);
	assert(movAnalysis.tracks[0].chunk_count == 2);
	assert(movAnalysis.tracks[0].time_to_sample.size() == 1);
	assert(movAnalysis.tracks[0].sample_to_chunk.size() == 1);
	assert(movAnalysis.tracks[0].sample_sizes.size() == 3);
	assert(movAnalysis.tracks[0].sample_packets.size() == 3);
	assert(movAnalysis.tracks[0].sample_packets[0].offset == 128);
	assert(movAnalysis.tracks[0].sample_packets[0].size == 12);
	assert(movAnalysis.tracks[0].sample_packets[1].offset == 140);
	assert(movAnalysis.tracks[0].sample_packets[1].duration == 20);
	assert(movAnalysis.tracks[0].sample_packets[2].offset == 256);
	assert(movAnalysis.tracks[0].sample_descriptions.size() == 1);
	assert(movAnalysis.tracks[0].sample_descriptions[0].format == "rpza");
	assert(movAnalysis.tracks[0].sample_descriptions[0].codec_name == "Apple Video / Road Pizza");
	assert(movAnalysis.tracks[0].sample_descriptions[0].decoder_status == "candidate_clean_room");
	assert(stackimport::mov2qt::analysis_to_json(movAnalysis).find("\"handlerType\":\"vide\"") != std::string::npos);
	assert(stackimport::mov2qt::analysis_to_json(movAnalysis).find("\"codecName\":\"Apple Video / Road Pizza\"") != std::string::npos);
	assert(stackimport::mov2qt::analysis_to_json(movAnalysis).find("\"packetPreview\"") != std::string::npos);

	const std::vector<uint8_t> pcmMovFixture = TestShared::make_quicktime_pcm_fixture();
	const stackimport::mov2qt::Analysis pcmMovAnalysis = stackimport::mov2qt::analyze(std::span<const uint8_t>(pcmMovFixture.data(), pcmMovFixture.size()));
	assert(pcmMovAnalysis.ok);
	assert(pcmMovAnalysis.tracks.size() == 1);
	assert(pcmMovAnalysis.tracks[0].handler_type == "soun");
	assert(pcmMovAnalysis.tracks[0].sample_descriptions.size() == 1);
	assert(pcmMovAnalysis.tracks[0].sample_descriptions[0].format == "twos");
	assert(pcmMovAnalysis.tracks[0].sample_descriptions[0].channel_count == 1);
	assert(pcmMovAnalysis.tracks[0].sample_descriptions[0].sample_size_bits == 16);
	assert(pcmMovAnalysis.tracks[0].sample_descriptions[0].sample_rate_hz == 8000.0);
	assert(pcmMovAnalysis.tracks[0].sample_packets.size() == 2);
	std::span<const uint8_t> packetBytes;
	std::string packetError;
	assert(stackimport::mov2qt::sample_packet_bytes(std::span<const uint8_t>(pcmMovFixture.data(), pcmMovFixture.size()), pcmMovAnalysis.tracks[0].sample_packets[0], packetBytes, packetError));
	assert(packetBytes.size() == 2);
	assert(packetBytes[0] == 0x01);
	assert(packetBytes[1] == 0x02);
	std::vector<uint8_t> wavBytes;
	std::string wavError;
	assert(stackimport::mov2qt::decode_pcm_track_to_wav(std::span<const uint8_t>(pcmMovFixture.data(), pcmMovFixture.size()), pcmMovAnalysis.tracks[0], wavBytes, wavError));
	assert(wavBytes.size() == 48);
	assert(std::memcmp(wavBytes.data(), "RIFF", 4) == 0);
	assert(std::memcmp(wavBytes.data() + 8, "WAVE", 4) == 0);
	assert(wavBytes[44] == 0x02);
	assert(wavBytes[45] == 0x01);
	assert(wavBytes[46] == 0x04);
	assert(wavBytes[47] == 0x03);
	stackimport::mov2qt::Track signed8Track;
	signed8Track.handler_type = "soun";
	signed8Track.sample_count = 2;
	signed8Track.constant_sample_size = 1;
	signed8Track.chunk_offsets.push_back(28);
	signed8Track.sample_to_chunk.push_back(stackimport::mov2qt::SampleToChunkEntry{1, 2, 1});
	signed8Track.time_to_sample.push_back(stackimport::mov2qt::TimeToSampleEntry{2, 1});
	stackimport::mov2qt::SampleDescription signed8Description;
	signed8Description.format = "twos";
	signed8Description.channel_count = 1;
	signed8Description.sample_size_bits = 8;
	signed8Description.sample_rate_hz = 8000.0;
	signed8Track.sample_descriptions.push_back(signed8Description);
	assert(stackimport::mov2qt::decode_pcm_track_to_wav(std::span<const uint8_t>(pcmMovFixture.data(), pcmMovFixture.size()), signed8Track, wavBytes, wavError));
	assert(wavBytes[44] == 0x81);
	assert(wavBytes[45] == 0x82);

	const std::vector<uint8_t> rpzaFixture = TestShared::make_rpza_single_color_fixture();
	stackimport::mov2qt::RgbaFrame rpzaFrame;
	std::string rpzaError;
	assert(stackimport::mov2qt::decode_rpza_frame(std::span<const uint8_t>(rpzaFixture.data(), rpzaFixture.size()), 4, 4, rpzaFrame, rpzaError));
	assert(rpzaFrame.width == 4);
	assert(rpzaFrame.height == 4);
	assert(rpzaFrame.rgba.size() == 64);
	assert(rpzaFrame.rgba[0] == 255);
	assert(rpzaFrame.rgba[1] == 0);
	assert(rpzaFrame.rgba[2] == 0);
	assert(rpzaFrame.rgba[3] == 255);
	const std::vector<uint8_t> rpzaLiteralFixture = TestShared::make_rpza_literal_fixture();
	assert(stackimport::mov2qt::decode_rpza_frame(std::span<const uint8_t>(rpzaLiteralFixture.data(), rpzaLiteralFixture.size()), 4, 4, rpzaFrame, rpzaError));
	assert(rpzaFrame.rgba[2] == 255);

	const std::vector<uint8_t> cinepakFixture = TestShared::make_cinepak_fixture();
	stackimport::mov2qt::CinepakFrame cinepakFrame;
	stackimport::mov2qt::CinepakDecoderState cinepakState;
	std::string cinepakError;
	assert(stackimport::mov2qt::decode_cinepak_frame(std::span<const uint8_t>(cinepakFixture.data(), cinepakFixture.size()), cinepakFrame, cinepakError, &cinepakState));
	assert(cinepakFrame.width == 4);
	assert(cinepakFrame.height == 4);
	assert(cinepakFrame.rgba.size() == 64);
	assert(cinepakFrame.rgba[0] == 80);
	assert(cinepakFrame.rgba[1] == 80);
	assert(cinepakFrame.rgba[2] == 80);
	assert(cinepakFrame.rgba[3] == 255);
	const std::vector<uint8_t> cinepakInterSkipFixture = TestShared::make_cinepak_inter_skip_fixture();
	assert(stackimport::mov2qt::decode_cinepak_frame(std::span<const uint8_t>(cinepakInterSkipFixture.data(), cinepakInterSkipFixture.size()), cinepakFrame, cinepakError, &cinepakState));
	assert(cinepakFrame.rgba[0] == 80);
	assert(cinepakFrame.rgba[1] == 80);
	assert(cinepakFrame.rgba[2] == 80);
	assert(cinepakFrame.rgba[3] == 255);

	const std::vector<uint8_t> qtrleFixture = {
		0x00, 0x00, 0x00, 0x1A,
		0x00, 0x00,
		0x01, 0x08, 0x01, 0x02, 0x03, 0x04, 0x04, 0x03, 0x02, 0x01, 0xFF,
		0x01, 0xF8, 0x04, 0xFF,
	};
	const std::array<std::array<uint8_t, 4>, 5> qtrlePalette = {{
		{0, 0, 0, 255},
		{255, 0, 0, 255},
		{0, 255, 0, 255},
		{0, 0, 255, 255},
		{255, 255, 0, 255},
	}};
	stackimport::mov2qt::RgbaFrame qtrleFrame;
	std::string qtrleError;
	assert(stackimport::mov2qt::decode_qtrle_frame(std::span<const uint8_t>(qtrleFixture.data(), qtrleFixture.size()), 8, 2, 8, std::span<const std::array<uint8_t, 4>>(qtrlePalette.data(), qtrlePalette.size()), qtrleFrame, qtrleError));
	assert(qtrleFrame.width == 8);
	assert(qtrleFrame.height == 2);
	assert(qtrleFrame.rgba.size() == 64);
	assert(qtrleFrame.rgba[0] == 255);
	assert(qtrleFrame.rgba[1] == 0);
	assert(qtrleFrame.rgba[2] == 0);
	assert(qtrleFrame.rgba[32] == 255);
	assert(qtrleFrame.rgba[33] == 255);
	assert(qtrleFrame.rgba[34] == 0);
	assert(qtrleFrame.rgba[60] == 255);
	assert(qtrleFrame.rgba[61] == 255);
	assert(qtrleFrame.rgba[62] == 0);
}

}
