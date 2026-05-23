#include "Mac68kDisassembly.h"

#include <cinttypes>
#include <cstdio>
#include <limits>

namespace stackimport {

namespace {

Mac68kDisassemblyResult disassembly_error(const char* message)
{
	Mac68kDisassemblyResult result = {};
	result.ok = false;
	result.error = message;
	return result;
}

void append_word_line(std::string& text, uint32_t address, uint16_t word)
{
	char line[32] = {};
	const int len = snprintf(line, sizeof(line), "%08" PRIX32 ": dc.w $%04" PRIX16 "\n", address, word);
	if(len > 0)
		text.append(line, static_cast<size_t>(len));
}

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

	// Preserve every byte until this wrapper is wired to a full 68K decoder.
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

	return result;
}

}
