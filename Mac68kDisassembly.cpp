#include "Mac68kDisassembly.h"

#include <cinttypes>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
#include <resource_file/Emulators/M68KEmulator.hh>
#include <resource_file/Emulators/PPC32Emulator.hh>
#include <resource_file/TrapInfo.hh>
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

#if !defined(STACKIMPORT_HAS_RESOURCE_DASM) || !STACKIMPORT_HAS_RESOURCE_DASM
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

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM

std::string trim_copy(std::string text)
{
	const std::string whitespace = " \t\r\n";
	const size_t start = text.find_first_not_of(whitespace);
	if(start == std::string::npos)
		return {};
	const size_t end = text.find_last_not_of(whitespace);
	return text.substr(start, end - start + 1);
}

bool parse_disassembly_line(const std::string& line, std::string& opcodeBytes, std::string& mnemonic, std::string& operands)
{
	if(line.size() < 10)
		return false;
	for(size_t index = 0; index < 8; index++)
	{
		if(!std::isxdigit(static_cast<unsigned char>(line[index])))
			return false;
	}
	size_t pos = line.find_first_not_of(' ', 8);
	if(pos == std::string::npos)
		return false;
	const size_t bytesStart = pos;
	while(pos < line.size() && (std::isxdigit(static_cast<unsigned char>(line[pos])) || line[pos] == ' '))
		pos++;
	opcodeBytes = trim_copy(line.substr(bytesStart, pos - bytesStart));
	pos = line.find_first_not_of(' ', pos);
	if(pos == std::string::npos)
		return false;
	const size_t mnemonicStart = pos;
	while(pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])))
		pos++;
	mnemonic = line.substr(mnemonicStart, pos - mnemonicStart);
	operands = trim_copy(pos < line.size() ? line.substr(pos) : std::string());
	return true;
}

bool first_opcode_word(const std::string& opcodeBytes, uint16_t& word)
{
	std::string hex;
	for(char ch : opcodeBytes)
	{
		if(std::isxdigit(static_cast<unsigned char>(ch)))
			hex.push_back(ch);
		if(hex.size() == 4)
			break;
	}
	if(hex.size() != 4)
		return false;
	word = static_cast<uint16_t>(std::strtoul(hex.c_str(), nullptr, 16));
	return true;
}

bool is_indirect_68k_target(const std::string& operands)
{
	if(operands.empty())
		return false;
	if(operands == "D0" || operands == "D1" || operands == "A0" || operands == "A1")
		return true;
	if(operands.size() >= 3 && operands[0] == '[' && (operands[1] == 'A' || operands[1] == 'D'))
		return true;
	return operands.rfind("[0x", 0) == 0;
}

std::string annotate_mac68k_line(const std::string& line)
{
	std::string opcodeBytes;
	std::string mnemonic;
	std::string operands;
	if(!parse_disassembly_line(line, opcodeBytes, mnemonic, operands))
		return line;

	if(mnemonic == "syscall")
	{
		uint16_t op = 0;
		if(!first_opcode_word(opcodeBytes, op))
			return line;
		uint16_t trapNumber = 0;
		bool autoPop = false;
#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
		uint8_t flags = 0;
		if((op & 0x0800u) != 0)
		{
			trapNumber = static_cast<uint16_t>(op & 0x0BFFu);
			autoPop = (op & 0x0400u) != 0;
		}
		else
		{
			trapNumber = static_cast<uint16_t>(op & 0x00FFu);
			flags = static_cast<uint8_t>((op >> 8u) & 7u);
		}
#else
		if((op & 0x0800u) != 0)
		{
			trapNumber = static_cast<uint16_t>(op & 0x0BFFu);
			autoPop = (op & 0x0400u) != 0;
		}
		else
		{
			trapNumber = static_cast<uint16_t>(op & 0x00FFu);
		}
#endif
		const char* trapSpace = trapNumber >= 0x0800u ? "Toolbox" : "OS";
		std::string trapName = operands;
#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
		if(const ResourceDASM::TrapInfo* info = ResourceDASM::info_for_68k_trap(trapNumber, flags))
			trapName = info->name;
#endif
		char suffix[160] = {};
		snprintf(
			suffix,
			sizeof(suffix),
			" ; stackimport: exits extension via %s trap %s ($A%03X%s)",
			trapSpace,
			trapName.c_str(),
			trapNumber,
			autoPop ? ", auto-pop" : "");
		return line + suffix;
	}
	if(mnemonic == "jsr" || mnemonic == "jmp" || mnemonic == "bsr")
	{
		if(operands.rfind("[PC ", 0) == 0)
			return line + " ; stackimport: internal extension call";
		if(is_indirect_68k_target(operands))
			return line + " ; stackimport: possible extension exit via indirect code pointer";
		return line + " ; stackimport: control transfer";
	}
	if(mnemonic == "rts")
		return line + " ; stackimport: returns from extension entry point or helper";
	return line;
}

std::string annotate_powerpc_line(const std::string& line)
{
	std::string opcodeBytes;
	std::string mnemonic;
	std::string operands;
	if(!parse_disassembly_line(line, opcodeBytes, mnemonic, operands))
		return line;
	if(mnemonic == "bl" || mnemonic == "bla")
		return line + " ; stackimport: branch-link call; may exit extension if target is imported/out of range";
	if(mnemonic == "bctrl" || mnemonic == "bcctrl" || mnemonic == "blrl" || mnemonic == "bclrl")
		return line + " ; stackimport: likely extension exit via indirect branch-link";
	if(mnemonic == "blr")
		return line + " ; stackimport: returns from extension entry point or helper";
	if(mnemonic == "sc")
		return line + " ; stackimport: PowerPC system call";
	return line;
}

std::string annotate_disassembly_text(const std::string& text, bool powerpc)
{
	std::string result;
	size_t start = 0;
	while(start < text.size())
	{
		const size_t end = text.find('\n', start);
		const bool hasNewline = end != std::string::npos;
		const std::string line = text.substr(start, (hasNewline ? end : text.size()) - start);
		result += powerpc ? annotate_powerpc_line(line) : annotate_mac68k_line(line);
		if(hasNewline)
			result.push_back('\n');
		else
			break;
		start = end + 1;
	}
	if(text.empty())
		return {};
	return result;
}

#endif

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

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
	std::multimap<uint32_t, std::string> labels;
	labels.emplace(displayAddress, "start");
	result.text = annotate_disassembly_text(ResourceDASM::M68KEmulator::disassemble(code.data(), code.size(), displayAddress, &labels), false);
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

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
	std::multimap<uint32_t, std::string> labels;
	labels.emplace(displayAddress, "start");
	result.text = annotate_disassembly_text(ResourceDASM::PPC32Emulator::disassemble(code.data(), instructionBytes, displayAddress, &labels), true);
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
