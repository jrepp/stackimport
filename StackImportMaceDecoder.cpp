#include "StackImportMaceDecoder.h"

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
#include "StackImportMaceResourceDasmAdapter.h"
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
	return DecodeMaceWithResourceDasm(data, size, stereo, mace3, decoded);
#else
	(void)data;
	(void)size;
	(void)stereo;
	(void)mace3;
	return false;
#endif
}

} // namespace stackimport
