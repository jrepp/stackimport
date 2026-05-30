/*
 *  TestsRomAnalysis.cpp
 *  stackimport
 *
 *  ROM analysis tests.
 *
 */

#include "TestsShared.h"

namespace TestRomAnalysis {

void RunTests()
{
	const uint8_t crcFixture[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
	const std::span<const uint8_t> crcSpan(crcFixture, sizeof(crcFixture));
	assert(stackimport::RomDasm::compute_crc32(crcSpan) == 0xCBF43926u);
	assert(stackimport::RomDasm::format_address(0xCBF43926u) == "CBF43926");
	assert(stackimport::RomDasm::format_address(0x0000007Au) == "0000007A");

	const uint32_t baseAddress = 0x40800000u;
	const std::vector<uint8_t> rom = TestShared::make_synthetic_rom_fixture();
	const std::span<const uint8_t> romSpan(rom.data(), rom.size());
	stackimport::RomDasm::RomInfo info = stackimport::RomDasm::analyze_rom_header(romSpan, baseAddress);
	info.filename = "synthetic-old-world-rom.bin";
	assert(info.size == static_cast<uint32_t>(rom.size()));
	assert(info.crc32 == stackimport::RomDasm::compute_crc32(romSpan));
	assert(info.base_address == baseAddress);
	assert(info.sha256.size() == 64);
	assert(info.machine_family == "68K");

	stackimport::RomDasm::ScanOptions options;
	options.start_address = baseAddress;
	options.disassemble_code = false;
	const stackimport::RomDasm::RomAnalysis analysis = stackimport::RomDasm::scan_rom(romSpan, info, options);
	assert(analysis.info.filename == "synthetic-old-world-rom.bin");
	assert(analysis.entry_point == baseAddress);
	assert(analysis.pointer_tables.size() >= 4);
	assert(!analysis.pointer_table_regions.empty());
	assert(analysis.pointer_table_regions[0].address == baseAddress + 0x20);
	assert(analysis.pointer_table_regions[0].entry_count == 4);
	assert(analysis.pointer_table_regions[0].targets[0] == baseAddress + 0x40);

	bool sawBootRom = false;
	bool sawPascalHello = false;
	bool sawTableMap = false;
	for(const auto& stringRegion : analysis.strings) {
		if(!stringRegion.is_pascal && stringRegion.value == "BootROM")
			sawBootRom = true;
		if(stringRegion.is_pascal && stringRegion.value == "Hello")
			sawPascalHello = true;
		if(!stringRegion.is_pascal && stringRegion.value == "TableMap")
			sawTableMap = true;
	}
	assert(sawBootRom);
	assert(sawPascalHello);
	assert(sawTableMap);

	bool sawDriverMarker = false;
	bool sawCodeMarker = false;
	for(const auto& marker : analysis.resource_markers) {
		if(marker.type == "DRVR" && marker.address == baseAddress + 0x90)
			sawDriverMarker = true;
		if(marker.type == "CODE" && marker.address == baseAddress + 0x94)
			sawCodeMarker = true;
	}
	assert(sawDriverMarker);
	assert(sawCodeMarker);
	assert(analysis.resource_maps.size() == 1);
	assert(analysis.resource_maps[0].address == baseAddress + 0xC0);
	assert(analysis.resource_maps[0].resource_count == 1);
	assert(analysis.resources.size() == 1);
	assert(analysis.resources[0].address == baseAddress + 0xC0 + 20);
	assert(analysis.resources[0].map_address == baseAddress + 0xC0 + 26);
	assert(analysis.resources[0].type == "STR ");
	assert(analysis.resources[0].id == 128);
	assert(analysis.resources[0].length == 6);

	const std::vector<uint8_t> kurtRom = TestShared::make_synthetic_kurt_rom_fixture();
	const std::span<const uint8_t> kurtRomSpan(kurtRom.data(), kurtRom.size());
	stackimport::RomDasm::RomInfo kurtInfo = stackimport::RomDasm::analyze_rom_header(kurtRomSpan, baseAddress);
	kurtInfo.filename = "synthetic-kurt-rom.bin";
	kurtInfo.base_address = baseAddress;
	const stackimport::RomDasm::RomAnalysis kurtAnalysis = stackimport::RomDasm::scan_rom(kurtRomSpan, kurtInfo, options);
	assert(kurtAnalysis.resources.size() == 1);
	assert(kurtAnalysis.resources[0].type == "STR ");
	assert(kurtAnalysis.resources[0].id == 129);
	assert(kurtAnalysis.resources[0].name == "Test");
	assert(kurtAnalysis.resources[0].address == baseAddress + 0x60);
	assert(kurtAnalysis.resources[0].length == 6);
	assert(kurtAnalysis.resources[0].stored_length == 6);
	assert(kurtAnalysis.resources[0].expected_length == 6);
	assert(kurtAnalysis.resources[0].wrapper_format == "Kurt");
	assert(kurtAnalysis.resources[0].source == "rom-kurt-resource");

	const std::vector<uint8_t> inlineRom = TestShared::make_synthetic_inline_rom_fixture();
	const std::span<const uint8_t> inlineRomSpan(inlineRom.data(), inlineRom.size());
	stackimport::RomDasm::RomInfo inlineInfo = stackimport::RomDasm::analyze_rom_header(inlineRomSpan, baseAddress);
	inlineInfo.filename = "synthetic-inline-rom.bin";
	inlineInfo.base_address = baseAddress;
	const stackimport::RomDasm::RomAnalysis inlineAnalysis = stackimport::RomDasm::scan_rom(inlineRomSpan, inlineInfo, options);
	bool sawInlineClut = false;
	for(const auto& resource : inlineAnalysis.resources) {
		if(resource.type == "clut" && resource.id == 4) {
			assert(resource.name == "4BitStd");
			assert(resource.address == baseAddress + 0x78);
			assert(resource.length == 16);
			assert(resource.stored_length == 16);
			assert(resource.expected_length == 16);
			assert(resource.wrapper_format.empty());
			assert(resource.source == "rom-inline-resource");
			sawInlineClut = true;
		}
	}
	assert(sawInlineClut);

	bool sawStringCluster = false;
	bool sawResourceMapRegion = false;
	bool sawResourceDataRegion = false;
	for(const auto& region : analysis.data_regions) {
		if(region.kind == "string_cluster" && region.item_count >= 3)
			sawStringCluster = true;
		if(region.kind == "resource_map" && region.item_count == 1)
			sawResourceMapRegion = true;
		if(region.kind == "resource_data" && region.item_count == 1)
			sawResourceDataRegion = true;
	}
	assert(sawStringCluster);
	assert(sawResourceMapRegion);
	assert(sawResourceDataRegion);

	stackimport::RomDasm::RomAnalysis controlFlow;
	controlFlow.info = info;
	stackimport::RomDasm::DisassemblyRegion codeRegion;
	codeRegion.start_address = baseAddress;
	codeRegion.end_address = baseAddress + 0x80;
	codeRegion.is_code = true;
	codeRegion.confidence = 0.95;
	codeRegion.text =
		"40800000  6100 001E                bsr        +0x20 /* 40800020 */\n"
		"40800004  60FA                     bra        -0x4 /* 40800000 */\n"
		"40800006  4EFA 0018                jmp        [PC + 0x18 /* 40800020 */]\n"
		"4080000A  41FA 0020                lea.l      A0, [PC + 0x20 /* 4080002C */]\n"
		"4080000E  A9F0                     syscall    SysBeep\n";
	controlFlow.code_regions.push_back(codeRegion);
	std::vector<uint8_t> controlFlowBytes(0x100, 0);
	stackimport::RomDasm::classify_rom_structure(
		controlFlow,
		std::span<const uint8_t>(controlFlowBytes.data(), controlFlowBytes.size()),
		baseAddress);
	bool sawCallXref = false;
	bool sawBranchXref = false;
	bool sawJumpXref = false;
	bool sawMemoryXref = false;
	for(const auto& xref : controlFlow.xrefs) {
		if(xref.from == baseAddress && xref.to == baseAddress + 0x20 && xref.kind == "call")
			sawCallXref = true;
		if(xref.from == baseAddress + 0x04 && xref.to == baseAddress && xref.kind == "branch")
			sawBranchXref = true;
		if(xref.from == baseAddress + 0x06 && xref.to == baseAddress + 0x20 && xref.kind == "jump")
			sawJumpXref = true;
		if(xref.from == baseAddress + 0x0A && xref.to == baseAddress + 0x2C && xref.kind == "memory")
			sawMemoryXref = true;
	}
	assert(sawCallXref);
	assert(sawBranchXref);
	assert(sawJumpXref);
	assert(sawMemoryXref);
	assert(!controlFlow.traps.empty());
	assert(controlFlow.traps[0].address == baseAddress + 0x0E);
	assert(controlFlow.traps[0].trap_number == 0x09F0);
	bool sawCallCandidate = false;
	for(const auto& fn : controlFlow.function_candidates) {
		if(fn.address == baseAddress + 0x20 && fn.calls == 1)
			sawCallCandidate = true;
	}
	assert(sawCallCandidate);
}

}
