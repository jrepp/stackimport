#include "StackImportMaceDecoder.h"

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
#include "vendor/resource_dasm/src/AudioCodecs.hh"
#endif

namespace stackimport {

bool DecodeMaceToS16Bytes(
	const uint8_t* data,
	size_t size,
	bool stereo,
	bool mace3,
	std::vector<uint8_t>& decoded)
{
	decoded.clear();
#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
	auto samples = ResourceDASM::decode_mace(data, size, stereo, mace3);
	const auto* sampleBytes = reinterpret_cast<const uint8_t*>(samples.data());
	decoded.assign(sampleBytes, sampleBytes + samples.size() * sizeof(samples[0]));
	return true;
#else
	(void)data;
	(void)size;
	(void)stereo;
	(void)mace3;
	return false;
#endif
}

} // namespace stackimport
