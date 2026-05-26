#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stackimport {

bool WritePngToMemory(
	std::vector<uint8_t>& png,
	int width,
	int height,
	int components,
	const void* data,
	int strideBytes);

bool WritePngFile(
	const std::string& path,
	int width,
	int height,
	int components,
	const void* data,
	int strideBytes);

} // namespace stackimport
