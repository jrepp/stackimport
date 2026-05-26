#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stackimport {

bool DecodePictWithResourceDasmToPng(
	const uint8_t* data,
	size_t size,
	std::vector<uint8_t>& png,
	uint32_t& width,
	uint32_t& height,
	std::string& error);

} // namespace stackimport
