/*
 *  TestsSound.cpp
 *  stackimport
 *
 *  Sound conversion tests: snd resource to WAV conversion.
 *
 */

#include "TestsShared.h"

namespace TestSound {

void RunTests()
{
	stackimport::PlatformByteVector wavData;
	std::string soundError;
	const std::vector<uint8_t> multiCommandSnd = TestShared::make_snd_format2_fixture(true, 22);
	assert(stackimport::ConvertSndResourceToWav(rsrcd::Bytes{multiCommandSnd.data(), multiCommandSnd.size()}, wavData, soundError));
	assert(wavData.size() == 48);
	assert(std::memcmp(wavData.data(), "RIFF", 4) == 0);
	assert(wavData[44] == 0x80);
	assert(wavData[47] == 0x83);
	{
		TestShared::CountingResourceOutput soundOutput = TestShared::parse_single_resource("snd ", 1, multiCommandSnd);
		assert(soundOutput.json_count == 1);
		assert(soundOutput.last_json.find("\"format\": 2") != std::string::npos);
		assert(soundOutput.last_json.find("\"wavConversionStatus\": \"converted\"") != std::string::npos);
		assert(soundOutput.last_json.find("\"commandName\": \"soundCmd\"") != std::string::npos);
	}
	{
		const std::vector<uint8_t> romPackedSnd = {0, 0, 0, 0, 1, 0, 2, 0, 1, 0};
		TestShared::CountingResourceOutput soundOutput = TestShared::parse_single_resource("snd ", 1, romPackedSnd);
		assert(soundOutput.json_count == 1);
		assert(soundOutput.last_json.find("\"format\": 0") != std::string::npos);
		assert(soundOutput.last_json.find("\"wavConversionStatus\": \"notConverted\"") != std::string::npos);
		assert(soundOutput.last_json.find("\"wavConversionError\": \"bad snd format\"") != std::string::npos);
		assert(soundOutput.last_json.find("\"firstBytesHex\": \"00000000010002000100\"") != std::string::npos);
	}
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

	const std::vector<uint8_t> badOffsetSnd = TestShared::make_snd_format2_fixture(false, 20);
	wavData.clear();
	soundError.clear();
	assert(stackimport::ConvertSndResourceToWav(rsrcd::Bytes{badOffsetSnd.data(), badOffsetSnd.size()}, wavData, soundError));
	assert(wavData.size() == 48);
	assert(std::memcmp(wavData.data(), "RIFF", 4) == 0);
	assert(wavData[44] == 0x80);
	assert(wavData[47] == 0x83);

	TestShared::CountingPlatformState failingSoundState;
	failingSoundState.fail_after_allocations = 0;
	stackimport_platform failingSoundPlatform = {};
	stackimport_platform_init(&failingSoundPlatform);
	failingSoundPlatform.allocate = TestShared::counting_allocate;
	failingSoundPlatform.deallocate = TestShared::counting_deallocate;
	failingSoundPlatform.user_data = &failingSoundState;
	stackimport_internal_platform failingSoundInternal = stackimport_internal_platform_from_api(&failingSoundPlatform);
	{
		stackimport_platform_scope failingSoundScope(failingSoundInternal);
		stackimport::PlatformByteVector failingWavData;
		soundError.clear();
		assert(!stackimport::ConvertSndResourceToWav(rsrcd::Bytes{multiCommandSnd.data(), multiCommandSnd.size()}, failingWavData, soundError));
		assert(soundError == "allocation failed");
	}
}

}
