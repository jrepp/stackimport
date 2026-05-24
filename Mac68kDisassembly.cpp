#include "Mac68kDisassembly.h"

#include <cinttypes>
#include <cstdio>
#include <limits>
#include <map>

#if STACKIMPORT_HAS_RESOURCE_DASM
#include <resource_file/Emulators/M68KEmulator.hh>
#include <resource_file/Emulators/PPC32Emulator.hh>
#endif

namespace stackimport {

namespace {

Mac68kDisassemblyResult disassembly_error(const char* message)
{
	Mac68kDisassemblyResult result = {};
	result.ok = false;
	result.error = message;
	return result;
}

#if !STACKIMPORT_HAS_RESOURCE_DASM
void append_word_line(std::string& text, uint32_t address, uint16_t word)
{
	char line[32] = {};
	const int len = snprintf(line, sizeof(line), "%08" PRIX32 ": dc.w $%04" PRIX16 "\n", address, word);
	if(len > 0)
		text.append(line, static_cast<size_t>(len));
}
#endif

void append_byte_line(std::string& text, uint32_t address, uint8_t byte)
{
	char line[32] = {};
	const int len = snprintf(line, sizeof(line), "%08" PRIX32 ": dc.b $%02" PRIX8 "\n", address, byte);
	if(len > 0)
		text.append(line, static_cast<size_t>(len));
}

}

Mac68kDisassemblyResult DisassembleMac68kCodeResource(
	std::span<const uint8_t> resource,
	size_t start,
	size_t size,
	uint32_t displayAddress)
{
	if(start > resource.size())
		return disassembly_error("start is beyond resource size");
	if(size > resource.size() - start)
		return disassembly_error("size extends beyond resource size");
	if(size > 0 && size - 1 > static_cast<size_t>(std::numeric_limits<uint32_t>::max() - displayAddress))
		return disassembly_error("selected range exceeds 32-bit 68K address space");

	const std::span<const uint8_t> code = resource.subspan(start, size);
	Mac68kDisassemblyResult result = {};
	result.ok = true;

#if STACKIMPORT_HAS_RESOURCE_DASM
	std::multimap<uint32_t, std::string> labels;
	labels.emplace(displayAddress, "start");
	result.text = ResourceDASM::M68KEmulator::disassemble(code.data(), code.size(), displayAddress, &labels);
#else
	for(size_t offset = 0; offset < code.size();)
	{
		const auto address = displayAddress + static_cast<uint32_t>(offset);
		if(offset + 1 < code.size())
		{
			const auto high = static_cast<uint16_t>(code[offset]);
			const auto low = static_cast<uint16_t>(code[offset + 1]);
			append_word_line(result.text, address, static_cast<uint16_t>((high << 8u) | low));
			offset += 2;
		}
		else
		{
			append_byte_line(result.text, address, code[offset]);
			offset += 1;
		}
	}
#endif

	return result;
}

Mac68kDisassemblyResult DisassemblePowerPCCodeResource(
	std::span<const uint8_t> resource,
	size_t start,
	size_t size,
	uint32_t displayAddress)
{
	if(start > resource.size())
		return disassembly_error("start is beyond resource size");
	if(size > resource.size() - start)
		return disassembly_error("size extends beyond resource size");
	if(size > 0 && size - 1 > static_cast<size_t>(std::numeric_limits<uint32_t>::max() - displayAddress))
		return disassembly_error("selected range exceeds 32-bit PowerPC address space");

	const std::span<const uint8_t> code = resource.subspan(start, size);
	const size_t instructionBytes = code.size() - (code.size() % 4);
	Mac68kDisassemblyResult result = {};
	result.ok = true;

#if STACKIMPORT_HAS_RESOURCE_DASM
	std::multimap<uint32_t, std::string> labels;
	labels.emplace(displayAddress, "start");
	result.text = ResourceDASM::PPC32Emulator::disassemble(code.data(), instructionBytes, displayAddress, &labels);
#else
	for(size_t offset = 0; offset < instructionBytes; offset += 4)
	{
		const uint32_t address = displayAddress + static_cast<uint32_t>(offset);
		const uint32_t op =
			(static_cast<uint32_t>(code[offset]) << 24u) |
			(static_cast<uint32_t>(code[offset + 1]) << 16u) |
			(static_cast<uint32_t>(code[offset + 2]) << 8u) |
			static_cast<uint32_t>(code[offset + 3]);
		char line[48] = {};
		const int len = snprintf(line, sizeof(line), "%08" PRIX32 ": dc.l $%08" PRIX32 "\n", address, op);
		if(len > 0)
			result.text.append(line, static_cast<size_t>(len));
	}
#endif
	for(size_t offset = instructionBytes; offset < code.size(); offset++)
		append_byte_line(result.text, displayAddress + static_cast<uint32_t>(offset), code[offset]);

	return result;
}

}
