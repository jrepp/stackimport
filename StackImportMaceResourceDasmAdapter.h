#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stackimport {

bool DecodeMaceWithResourceDasm(
	const uint8_t* data,
	size_t size,
	bool stereo,
	bool mace3,
	std::vector<uint8_t>& decoded);

} // namespace stackimport
