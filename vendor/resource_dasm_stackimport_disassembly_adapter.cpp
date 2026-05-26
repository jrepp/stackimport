#include "../StackImportResourceDasmDisassemblyAdapter.h"

#include <cstdio>
#include <map>

#include "resource_file/Emulators/M68KEmulator.hh"
#include "resource_file/Emulators/PPC32Emulator.hh"
#include "resource_file/TrapInfo.hh"

namespace stackimport {

bool DisassembleMac68kWithResourceDasm(
	const uint8_t* data,
	size_t size,
	uint32_t displayAddress,
	std::string& text)
{
	std::multimap<uint32_t, std::string> labels;
	labels.emplace(displayAddress, "start");
	text = ResourceDASM::M68KEmulator::disassemble(data, size, displayAddress, &labels);
	return true;
}

bool DisassembleMac68kRomWithResourceDasm(
	const uint8_t* data,
	size_t size,
	uint32_t startAddress,
	size_t pageSize,
	std::string& text)
{
	std::multimap<uint32_t, std::string> labels;
	labels.emplace(startAddress, "start");
	if(pageSize > 0)
	{
		for(size_t offset = 0; offset < size; offset += pageSize)
		{
			const uint32_t address = startAddress + static_cast<uint32_t>(offset);
			char label[32] = {};
			snprintf(label, sizeof(label), "page_%08X", static_cast<unsigned>(address));
			labels.emplace(address, label);
		}
	}
	text = ResourceDASM::M68KEmulator::disassemble(data, size, startAddress, &labels);
	return true;
}

bool DisassemblePowerPCWithResourceDasm(
	const uint8_t* data,
	size_t size,
	uint32_t displayAddress,
	std::string& text)
{
	std::multimap<uint32_t, std::string> labels;
	labels.emplace(displayAddress, "start");
	text = ResourceDASM::PPC32Emulator::disassemble(data, size, displayAddress, &labels);
	return true;
}

const char* ResourceDasmTrapName(uint16_t trapNumber, uint8_t flags)
{
	const ResourceDASM::TrapInfo* info = ResourceDASM::info_for_68k_trap(trapNumber, flags);
	return info ? info->name : nullptr;
}

} // namespace stackimport
