#include "StackImportSoundConverter.h"

#include <cmath>
#include <cstddef>

namespace stackimport {
namespace {

class SndToWavConverter {
public:
	explicit SndToWavConverter(rsrcd::Bytes input)
		: input_(input)
	{}

	bool convert(PlatformByteVector& wav, std::string& error)
	{
		uint16_t channels = 1;
		const int16_t sndFormat = read_s16(error);
		if(!error.empty())
			return false;
		if(sndFormat != 1 && sndFormat != 2)
			return fail(error, "bad snd format");

		uint32_t sndHeaderOffset = 20;
		int32_t opts = 0;
		if(sndFormat == 2)
		{
			(void)read_s16(error);
			sndHeaderOffset = 14;
		}
		else
		{
			if(read_s16(error) != 1)
				return fail(error, "too many data types");
			if(read_s16(error) != 5)
				return fail(error, "not sampled sound");
			opts = read_s32(error);
			if(opts != initMono && opts != 0xA0 && opts != initStereo)
				return fail(error, "unhandled sampled-sound options");
		}
		if(!error.empty())
			return false;
		if(read_s16(error) != 1)
			return fail(error, "too many commands");
		const uint16_t sndCommand = read_u16(error);
		if(sndCommand != 0x8051 && sndCommand != 0x8050)
			return fail(error, "not a bufferCmd or sndCmd");
		if(read_s16(error) != 0)
			return fail(error, "bad param1");
		if(read_s32(error) != static_cast<int32_t>(sndHeaderOffset))
			return fail(error, "bad param2");
		if(read_s32(error) != 0)
			return fail(error, "bad data pointer");

		uint32_t numBytes = read_u32(error);
		const double sampleRate = read_ufixed(error);
		(void)read_u32(error);
		(void)read_u32(error);
		uint8_t sampleEncoding = read_u8(error);
		if(!error.empty())
			return false;
		if(sampleEncoding != 0 && sampleEncoding != extSH)
			return fail(error, "unsupported sample encoding");
		if(sampleEncoding == extSH)
		{
			channels = static_cast<uint16_t>(numBytes);
			numBytes = 0;
		}
		else if(opts == initStereo)
			channels = 2;

		const uint8_t baseFrequency = read_u8(error);
		double factor = 1.0;
		if(baseFrequency != 60)
			factor = 0.8;

		uint32_t dataSize = numBytes;
		uint32_t rate = static_cast<uint32_t>(sampleRate);
		uint32_t bytesPerSample = 1;
		if(sampleEncoding == extSH)
		{
			const uint32_t numFrames = read_u32(error);
			const double aiffSampleRate = read_extended80(error);
			(void)read_u32(error);
			(void)read_u32(error);
			(void)read_u32(error);
			const uint16_t sampleSize = read_u16(error);
			bytesPerSample = sampleSize / 8;
			(void)read_u16(error);
			(void)read_u32(error);
			(void)read_u32(error);
			(void)read_u32(error);
			if(bytesPerSample == 0)
				return fail(error, "invalid sample size");
			dataSize = numFrames * bytesPerSample * channels;
			numBytes = dataSize;
			rate = static_cast<uint32_t>(aiffSampleRate);
		}
		if(!error.empty())
			return false;
		if(pos_ + numBytes > input_.size)
			return fail(error, "truncated sample data");

		wav.clear();
		wav.reserve(static_cast<size_t>(44) + dataSize);
		append_ascii(wav, "RIFF");
		append_u32le(wav, 36 + dataSize);
		append_ascii(wav, "WAVE");
		append_ascii(wav, "fmt ");
		append_u32le(wav, 16);
		append_u16le(wav, 1);
		append_u16le(wav, channels);
		append_u32le(wav, static_cast<uint32_t>(rate * factor));
		append_u32le(wav, rate * channels * bytesPerSample);
		append_u16le(wav, static_cast<uint16_t>(channels * bytesPerSample));
		append_u16le(wav, static_cast<uint16_t>(bytesPerSample * 8));
		append_ascii(wav, "data");
		append_u32le(wav, dataSize);

		if(bytesPerSample == 2)
		{
			for(uint32_t i = 0; i < numBytes / 2; i++)
				append_u16le(wav, read_u16(error));
		}
		else
		{
			wav.insert(wav.end(), input_.data + pos_, input_.data + pos_ + numBytes);
			pos_ += numBytes;
		}
		return error.empty();
	}

private:
	static constexpr int32_t initMono = 0x0080;
	static constexpr int32_t initStereo = 0x00C0;
	static constexpr uint8_t extSH = 0xFF;

	rsrcd::Bytes input_;
	size_t pos_ = 0;

	static bool fail(std::string& error, const char* message)
	{
		error = message;
		return false;
	}

	bool require(size_t count, std::string& error)
	{
		if(pos_ + count <= input_.size)
			return true;
		error = "truncated snd resource";
		return false;
	}

	uint8_t read_u8(std::string& error)
	{
		if(!require(1, error))
			return 0;
		return input_.data[pos_++];
	}

	uint16_t read_u16(std::string& error)
	{
		if(!require(2, error))
			return 0;
		uint16_t value = static_cast<uint16_t>(
			(static_cast<uint16_t>(input_.data[pos_]) << 8) |
			static_cast<uint16_t>(input_.data[pos_ + 1]));
		pos_ += 2;
		return value;
	}

	int16_t read_s16(std::string& error)
	{
		return static_cast<int16_t>(read_u16(error));
	}

	uint32_t read_u32(std::string& error)
	{
		if(!require(4, error))
			return 0;
		uint32_t value = static_cast<uint32_t>(input_.data[pos_]) << 24 |
			static_cast<uint32_t>(input_.data[pos_ + 1]) << 16 |
			static_cast<uint32_t>(input_.data[pos_ + 2]) << 8 |
			static_cast<uint32_t>(input_.data[pos_ + 3]);
		pos_ += 4;
		return value;
	}

	int32_t read_s32(std::string& error)
	{
		return static_cast<int32_t>(read_u32(error));
	}

	double read_ufixed(std::string& error)
	{
		return static_cast<double>(read_u32(error)) / 65536.0;
	}

	double read_extended80(std::string& error)
	{
		if(!require(10, error))
			return 0;
		uint8_t f[10] = {};
		for(uint8_t& byte : f)
			byte = input_.data[pos_++];

		const bool sign = (f[0] & 0x80) != 0;
		int exponent = ((f[0] & 0x7F) << 8) | f[1];
		exponent -= 16383;

		double value = 0;
		for(int i = 2; i < 10; i++)
		{
			for(int bit = 7; bit >= 0; bit--)
			{
				if((f[i] & (1 << bit)) != 0)
					value += exponent >= 0 ? std::pow(2.0, exponent) : 1.0 / std::pow(2.0, -exponent);
				exponent--;
			}
		}
		return sign ? -value : value;
	}

	static void append_ascii(PlatformByteVector& out, const char* text)
	{
		for(const char* p = text; *p; ++p)
			out.push_back(static_cast<uint8_t>(*p));
	}

	static void append_u16le(PlatformByteVector& out, uint16_t value)
	{
		out.push_back(static_cast<uint8_t>(value & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
	}

	static void append_u32le(PlatformByteVector& out, uint32_t value)
	{
		out.push_back(static_cast<uint8_t>(value & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
	}
};

} // namespace

bool ConvertSndResourceToWav(rsrcd::Bytes snd, PlatformByteVector& wav, std::string& error)
{
	error.clear();
	return SndToWavConverter(snd).convert(wav, error);
}

} // namespace stackimport
