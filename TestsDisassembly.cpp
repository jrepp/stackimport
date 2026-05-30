/*
 *  TestsDisassembly.cpp
 *  stackimport
 *
 *  Mac68k disassembly tests.
 *
 */

#include "TestsShared.h"

namespace TestDisassembly {

void RunTests()
{
	const uint8_t codeBytes[] = {0x4E, 0x75, 0xA9, 0xF0, 0x12};
	const std::span<const uint8_t> code(codeBytes, sizeof(codeBytes));
	const auto fullDisassembly = stackimport::DisassembleMac68kCodeResource(code, 0, 4, 0x1000);
	assert(fullDisassembly.ok);
	assert(fullDisassembly.text.find("rts") != std::string::npos || fullDisassembly.text.find("dc.w $4E75") != std::string::npos);

	const auto slicedDisassembly = stackimport::DisassembleMac68kCodeResource(code, 2, 3, 0x200);
	assert(slicedDisassembly.ok);
	assert(slicedDisassembly.text.find("A9F0") != std::string::npos || slicedDisassembly.text.find("_SysBeep") != std::string::npos);

	const auto badStart = stackimport::DisassembleMac68kCodeResource(code, 6, 0, 0);
	assert(!badStart.ok);
	const auto badSize = stackimport::DisassembleMac68kCodeResource(code, 4, 2, 0);
	assert(!badSize.ok);
}

}
