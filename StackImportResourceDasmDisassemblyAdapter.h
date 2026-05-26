#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace stackimport {

bool DisassembleMac68kWithResourceDasm(
	const uint8_t* data,
	size_t size,
	uint32_t displayAddress,
	std::string& text);

bool DisassembleMac68kRomWithResourceDasm(
	const uint8_t* data,
	size_t size,
	uint32_t startAddress,
	size_t pageSize,
	std::string& text);

bool DisassemblePowerPCWithResourceDasm(
	const uint8_t* data,
	size_t size,
	uint32_t displayAddress,
	std::string& text);

const char* ResourceDasmTrapName(uint16_t trapNumber, uint8_t flags);

} // namespace stackimport
