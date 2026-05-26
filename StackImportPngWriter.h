#pragma once

#include <string>

namespace stackimport {

bool WritePngFile(
	const std::string& path,
	int width,
	int height,
	int components,
	const void* data,
	int strideBytes);

} // namespace stackimport
