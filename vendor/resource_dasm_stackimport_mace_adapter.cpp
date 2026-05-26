#include "../StackImportMaceResourceDasmAdapter.h"

#include "resource_dasm/src/AudioCodecs.hh"

namespace stackimport {

bool DecodeMaceWithResourceDasm(
	const uint8_t* data,
	size_t size,
	bool stereo,
	bool mace3,
	std::vector<uint8_t>& decoded)
{
	auto samples = ResourceDASM::decode_mace(data, size, stereo, mace3);
	const auto* sampleBytes = reinterpret_cast<const uint8_t*>(samples.data());
	decoded.assign(sampleBytes, sampleBytes + samples.size() * sizeof(samples[0]));
	return true;
}

} // namespace stackimport
