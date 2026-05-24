#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace stackimport {

struct Mac68kDisassemblyResult {
	bool ok = false;
	std::string text;
	std::string error;
};

Mac68kDisassemblyResult DisassembleMac68kCodeResource(
	std::span<const uint8_t> resource,
	size_t start,
	size_t size,
	uint32_t displayAddress = 0);

Mac68kDisassemblyResult DisassemblePowerPCCodeResource(
	std::span<const uint8_t> resource,
	size_t start,
	size_t size,
	uint32_t displayAddress = 0);

}
