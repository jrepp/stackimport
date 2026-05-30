/*
 *  TestsResourceTransforms.cpp
 *  stackimport
 *
 *  Resource transform tests: image, text, metadata, code, color/ui transforms.
 *
 */

#include "TestsShared.h"

namespace TestResourceTransforms {

void test_resource_image_transforms()
{
	TestShared::assert_rgba_resource("ICON", 128, TestShared::make_icon_payload(), 32, 32);
	std::vector<uint8_t> wrappedIconPayload(12, 0);
	const std::vector<uint8_t> iconPayload = TestShared::make_icon_payload();
	wrappedIconPayload.insert(wrappedIconPayload.end(), iconPayload.begin(), iconPayload.end());
	TestShared::assert_rgba_resource("ICON", 129, wrappedIconPayload, 32, 32);

	std::vector<uint8_t> icnPayload(256);
	icnPayload[0] = 0x80;
	icnPayload[128] = 0x80;
	TestShared::assert_rgba_resource("ICN#", 130, icnPayload, 32, 32);

	TestShared::assert_rgba_resource("PAT ", 9, {0x80, 0, 0, 0, 0, 0, 0, 0}, 8, 8);
	TestShared::assert_rgba_resource("pixs", 1, TestShared::make_compact_4bit_pixmap_payload(false), 4, 2);
	TestShared::assert_rgba_resource("ppat", 18, TestShared::make_compact_4bit_pixmap_payload(true), 4, 2);

	std::vector<uint8_t> sicnPayload(32);
	sicnPayload[0] = 0x80;
	TestShared::assert_rgba_resource("SICN", 10, sicnPayload, 16, 16);

	struct IndexedIconCase
	{
		const char* type;
		uint16_t width;
		uint16_t height;
		uint8_t bits_per_pixel;
	};
	const IndexedIconCase indexedIconCases[] = {
		{"icl4", 32, 32, 4},
		{"icl8", 32, 32, 8},
		{"icm4", 16, 12, 4},
		{"icm8", 16, 12, 8},
		{"ics4", 16, 16, 4},
		{"ics8", 16, 16, 8},
	};
	for(const IndexedIconCase& c : indexedIconCases)
	{
		const size_t bytesPerIcon = static_cast<size_t>(c.width) * c.height * c.bits_per_pixel / 8u;
		std::vector<uint8_t> payload(bytesPerIcon);
		payload[0] = c.bits_per_pixel == 4 ? 0x0Fu : 0xFFu;
		TestShared::assert_rgba_resource(c.type, 11, payload, c.width, c.height);
	}

	struct MonoIconCase
	{
		const char* type;
		uint16_t width;
		uint16_t height;
	};
	const MonoIconCase monoIconCases[] = {
		{"icm#", 16, 12},
		{"ics#", 16, 16},
	};
	for(const MonoIconCase& c : monoIconCases)
	{
		const size_t bytesPerIcon = static_cast<size_t>(c.width) * c.height / 8u;
		std::vector<uint8_t> payload(bytesPerIcon * 2u);
		payload[0] = 0x80;
		payload[bytesPerIcon] = 0x80;
		TestShared::assert_rgba_resource(c.type, 12, payload, c.width, c.height);
	}

	{
		TestShared::CountingResourceOutput cursOutput = TestShared::parse_single_resource("CURS", 13, TestShared::make_curs_payload());
		assert(cursOutput.rgba_count == 1);
		assert(cursOutput.json_count == 1);
		assert(cursOutput.last_json.find("\"hotspotX\": 3") != std::string::npos);
		assert(cursOutput.last_json.find("\"hotspotY\": 2") != std::string::npos);
	}
}

void test_resource_text_transforms()
{
	TestShared::assert_text_resource_equals("STR ", 12, {5, 'H', 0x8E, 'l', 'l', 'o'}, "H\xC3\xA9llo");

	std::vector<uint8_t> stringListPayload;
	TestShared::append_u16be(stringListPayload, 2);
	stringListPayload.insert(stringListPayload.end(), {3, 'O', 'n', 'e'});
	stringListPayload.insert(stringListPayload.end(), {3, 'T', 'w', 'o'});
	TestShared::assert_json_resource_contains("STR#", 44, stringListPayload, {"\"One\"", "\"Two\""});

	TestShared::assert_json_resource_contains("TwCS", 1, {0, 1, 0, 5, 'T', 'w', 'C', 'S', '!'}, {"\"TwCS!\""});

	std::vector<uint8_t> txstPayload;
	txstPayload.push_back(0x03);
	txstPayload.push_back(0x00);
	TestShared::append_u16be(txstPayload, 12);
	TestShared::append_u16be(txstPayload, 0x1111);
	TestShared::append_u16be(txstPayload, 0x2222);
	TestShared::append_u16be(txstPayload, 0x3333);
	txstPayload.insert(txstPayload.end(), {9, 'H', 'e', 'l', 'v', 'e', 't', 'i', 'c', 'a'});
	TestShared::assert_json_resource_contains("TxSt", 1, txstPayload, {
		"\"fontStyle\": 3",
		"\"fontSize\": 12",
		"\"fontName\": \"Helvetica\"",
	});

	std::vector<uint8_t> stylPayload;
	TestShared::append_u16be(stylPayload, 1);
	TestShared::append_u32be(stylPayload, 5);
	TestShared::append_u16be(stylPayload, 14);
	TestShared::append_u16be(stylPayload, 11);
	TestShared::append_u16be(stylPayload, 128);
	TestShared::append_u16be(stylPayload, 0x0001);
	TestShared::append_u16be(stylPayload, 12);
	TestShared::append_u16be(stylPayload, 0x1111);
	TestShared::append_u16be(stylPayload, 0x2222);
	TestShared::append_u16be(stylPayload, 0x3333);
	TestShared::assert_json_resource_contains("styl", 1, stylPayload, {
		"\"offset\": 5",
		"\"fontId\": 128",
		"\"green\": 8738",
	});

	std::vector<uint8_t> kchrPayload(2 + 256 + 2 + 128 + 2 + 8);
	rsrcd::write_u16be(kchrPayload.data(), 0);
	rsrcd::write_u16be(kchrPayload.data() + 258, 1);
	kchrPayload[260] = 'a';
	rsrcd::write_u16be(kchrPayload.data() + 388, 1);
	kchrPayload[390] = 0;
	kchrPayload[391] = 12;
	rsrcd::write_u16be(kchrPayload.data() + 392, 1);
	kchrPayload[394] = '`';
	kchrPayload[395] = 'a';
	kchrPayload[396] = 0;
	kchrPayload[397] = '`';
	TestShared::assert_json_resource_contains("KCHR", 1, kchrPayload, {
		"\"modifierTableIndexes\"",
		"\"charsHex\": \"61",
		"\"virtualKeyCode\": 12",
		"\"substituteChar\": 97",
	});
}

void test_resource_metadata_transforms()
{
	std::vector<uint8_t> addColorData;
	addColorData.push_back(0x03);
	TestShared::append_u16be(addColorData, 10);
	TestShared::append_u16be(addColorData, 20);
	TestShared::append_u16be(addColorData, 30);
	TestShared::append_u16be(addColorData, 40);
	TestShared::append_u16be(addColorData, 2);
	TestShared::append_u16be(addColorData, 0x1111);
	TestShared::append_u16be(addColorData, 0x2222);
	TestShared::append_u16be(addColorData, 0x3333);
	TestShared::assert_json_resource_contains("HCbg", 77, addColorData, {
		"\"targetKind\": \"background\"",
		"\"targetId\": 77",
		"\"type\": \"rect\"",
		"\"red\": 4369",
	});

	std::vector<uint8_t> versionPayload = {0x01, 0x23, 0x80, 0x00, 0x00, 0x00};
	versionPayload.insert(versionPayload.end(), {3, '1', '.', '2'});
	versionPayload.insert(versionPayload.end(), {7, 'R', 'e', 'l', 'e', 'a', 's', 'e'});
	TestShared::assert_json_resource_contains("vers", 1, versionPayload, {
		"\"majorRevision\": 1",
		"\"minorAndBugRevision\": 35",
		"\"shortVersion\": \"1.2\"",
	});

	std::vector<uint8_t> cfrgPayload(79);
	rsrcd::write_u16be(cfrgPayload.data() + 10, 1);
	rsrcd::write_u16be(cfrgPayload.data() + 30, 1);
	size_t cfrgOffset = 32;
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset, 0x70777063);
	cfrgPayload[cfrgOffset + 7] = 2;
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 8, 0x01020304);
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 12, 0x00010000);
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 16, 0x00002000);
	rsrcd::write_u16be(cfrgPayload.data() + cfrgOffset + 20, 7);
	cfrgPayload[cfrgOffset + 22] = 1;
	cfrgPayload[cfrgOffset + 23] = 2;
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 24, 0x434F4445);
	rsrcd::write_u32be(cfrgPayload.data() + cfrgOffset + 28, 128);
	rsrcd::write_u16be(cfrgPayload.data() + cfrgOffset + 36, 1);
	rsrcd::write_u16be(cfrgPayload.data() + cfrgOffset + 40, 47);
	cfrgPayload[cfrgOffset + 42] = 4;
	cfrgPayload[cfrgOffset + 43] = 'M';
	cfrgPayload[cfrgOffset + 44] = 'a';
	cfrgPayload[cfrgOffset + 45] = 'i';
	cfrgPayload[cfrgOffset + 46] = 'n';
	TestShared::assert_json_resource_contains("cfrg", 1, cfrgPayload, {
		"\"architectureType\": \"pwpc\"",
		"\"usageName\": \"application\"",
		"\"whereName\": \"resourceFork\"",
		"\"name\": \"Main\"",
	});

	std::vector<uint8_t> mbarPayload;
	TestShared::append_u16be(mbarPayload, 2);
	TestShared::append_u16be(mbarPayload, 128);
	TestShared::append_u16be(mbarPayload, 129);
	TestShared::assert_json_resource_contains("MBAR", 1, mbarPayload, {"\"menuIds\"", "128", "129"});

	std::vector<uint8_t> alrtPayload;
	TestShared::append_u16be(alrtPayload, 1);
	TestShared::append_u16be(alrtPayload, 2);
	TestShared::append_u16be(alrtPayload, 101);
	TestShared::append_u16be(alrtPayload, 202);
	TestShared::append_u16be(alrtPayload, 300);
	alrtPayload.push_back(0x12);
	alrtPayload.push_back(0x34);
	TestShared::append_u16be(alrtPayload, 0x0A00);
	TestShared::assert_json_resource_contains("ALRT", 1, alrtPayload, {"\"itemListId\": 300", "\"autoPosition\": 2560"});

	std::vector<uint8_t> frefPayload;
	TestShared::append_u32be(frefPayload, 0x4150504C);
	TestShared::append_u16be(frefPayload, 42);
	frefPayload.insert(frefPayload.end(), {4, 'A', 'p', 'p', 's'});
	TestShared::assert_json_resource_contains("FREF", 1, frefPayload, {"\"fileTypeString\": \"APPL\"", "\"fileName\": \"Apps\""});

	std::vector<uint8_t> bndlPayload;
	TestShared::append_u32be(bndlPayload, 0x4F574E52);
	TestShared::append_u16be(bndlPayload, 128);
	TestShared::append_u16be(bndlPayload, 0);
	TestShared::append_u32be(bndlPayload, 0x49434F4E);
	TestShared::append_u16be(bndlPayload, 1);
	TestShared::append_u16be(bndlPayload, 0);
	TestShared::append_u16be(bndlPayload, 1000);
	TestShared::append_u16be(bndlPayload, 1);
	TestShared::append_u16be(bndlPayload, 1001);
	TestShared::assert_json_resource_contains("BNDL", 1, bndlPayload, {
		"\"ownerNameString\": \"OWNR\"",
		"\"typeString\": \"ICON\"",
		"\"resourceId\": 1001",
	});

	std::vector<uint8_t> rovPayload;
	TestShared::append_u16be(rovPayload, 0x0750);
	TestShared::append_u16be(rovPayload, 1);
	TestShared::append_u32be(rovPayload, 0x4D454E55);
	TestShared::append_u16be(rovPayload, 128);
	TestShared::assert_json_resource_contains("ROv#", 1, rovPayload, {
		"\"romVersion\": 1872",
		"\"typeString\": \"MENU\"",
		"\"id\": 128",
	});

	std::vector<uint8_t> rsscPayload(24);
	rsrcd::write_u32be(rsscPayload.data(), 0x52535343);
	rsrcd::write_u16be(rsscPayload.data() + 4, 22);
	rsscPayload[22] = 0x4E;
	rsscPayload[23] = 0x75;
	TestShared::assert_json_resource_contains("RSSC", 1, rsscPayload, {
		"\"typeSignatureString\": \"RSSC\"",
		"\"codeStartOffset\": 22",
		"\"codeSize\": 2",
	});

	TestShared::assert_json_resource_contains("RECT", 1, {0, 1, 0, 2, 0, 101, 0, 202}, {"\"top\": 1", "\"right\": 202"});

	std::vector<uint8_t> toolPayload;
	TestShared::append_u16be(toolPayload, 2);
	TestShared::append_u16be(toolPayload, 1);
	TestShared::append_u16be(toolPayload, 128);
	TestShared::append_u16be(toolPayload, 129);
	TestShared::append_u16be(toolPayload, 130);
	TestShared::assert_json_resource_contains("TOOL", 1, toolPayload, {"\"toolsPerRow\": 2", "\"cursorIds\"", "130"});

	std::vector<uint8_t> pickPayload;
	TestShared::append_u32be(pickPayload, 0x50494354);
	pickPayload.insert(pickPayload.end(), {1, 2, 3, 0});
	TestShared::append_u16be(pickPayload, 16);
	TestShared::append_u16be(pickPayload, 1);
	TestShared::append_u32be(pickPayload, 0x49434F4E);
	TestShared::append_u16be(pickPayload, 128);
	TestShared::append_u32be(pickPayload, 0x50494354);
	TestShared::append_u16be(pickPayload, 129);
	TestShared::assert_json_resource_contains("PICK", 1, pickPayload, {
		"\"typeString\": \"PICT\"",
		"\"verticalCellSize\": 16",
		"\"id\": 129",
	});

	TestShared::assert_json_resource_contains("KBDN", 1, {3, 'U', 'S', 'A'}, {"\"name\": \"USA\""});

	std::vector<uint8_t> papaPayload = {
		7, 'P', 'r', 'i', 'n', 't', 'e', 'r',
		3, 'L', 'W', 'R',
		4, 'Z', 'o', 'n', 'e',
	};
	TestShared::append_u32be(papaPayload, 0x12345678);
	papaPayload.push_back(0xAA);
	TestShared::assert_json_resource_contains("PAPA", 1, papaPayload, {
		"\"name\": \"Printer\"",
		"\"addressBlock\": 305419896",
		"\"dataHex\": \"AA\"",
	});

	std::vector<uint8_t> layoPayload;
	TestShared::append_u16be(layoPayload, 128);
	TestShared::append_u16be(layoPayload, 12);
	TestShared::append_u16be(layoPayload, 20);
	TestShared::append_u16be(layoPayload, 1);
	TestShared::append_u16be(layoPayload, 2);
	TestShared::append_u16be(layoPayload, 30);
	TestShared::append_u16be(layoPayload, 40);
	TestShared::append_u16be(layoPayload, 10);
	TestShared::append_u16be(layoPayload, 20);
	TestShared::append_u16be(layoPayload, 110);
	TestShared::append_u16be(layoPayload, 220);
	TestShared::append_u16be(layoPayload, 0);
	TestShared::append_u16be(layoPayload, 1);
	TestShared::append_u16be(layoPayload, 2);
	TestShared::append_u16be(layoPayload, 3);
	TestShared::append_u16be(layoPayload, 4);
	TestShared::assert_json_resource_contains("LAYO", 1, layoPayload, {"\"fontId\": 128", "\"right\": 220", "\"rectangles\""});
}

void test_resource_code_transforms()
{
	std::vector<uint8_t> code0Payload;
	TestShared::append_u32be(code0Payload, 0x1000);
	TestShared::append_u32be(code0Payload, 0x2000);
	TestShared::append_u32be(code0Payload, 8);
	TestShared::append_u32be(code0Payload, 16);
	TestShared::append_u16be(code0Payload, 0x0010);
	TestShared::append_u16be(code0Payload, 0x3F3C);
	TestShared::append_u16be(code0Payload, 1);
	TestShared::append_u16be(code0Payload, 0xA9F0);
	TestShared::assert_json_resource_contains("CODE", 0, code0Payload, {"\"kind\": \"jumpTable\"", "\"loadSegmentTrap\": true"});

	std::vector<uint8_t> codePayload;
	TestShared::append_u16be(codePayload, 2);
	TestShared::append_u16be(codePayload, 3);
	codePayload.insert(codePayload.end(), {0x4E, 0x75});
	TestShared::assert_json_resource_contains("CODE", 1, codePayload, {"\"kind\": \"nearSegment\"", "\"codeSize\": 2"});
	{
		TestShared::CountingResourceOutput codeOutput = TestShared::parse_single_resource("CODE", 1, codePayload);
		assert(codeOutput.json_count == 1);
		assert(codeOutput.text_count == 1);
		assert(codeOutput.last_text.find("rts") != std::string::npos ||
			codeOutput.last_text.find("dc.w $4E75") != std::string::npos);
	}

	std::vector<uint8_t> drvrPayload(26);
	rsrcd::write_u16be(drvrPayload.data(), 0x0100);
	rsrcd::write_u16be(drvrPayload.data() + 2, 60);
	rsrcd::write_u16be(drvrPayload.data() + 4, 0xFFFF);
	rsrcd::write_u16be(drvrPayload.data() + 6, 128);
	rsrcd::write_u16be(drvrPayload.data() + 8, 24);
	drvrPayload[18] = 4;
	drvrPayload[19] = 'D';
	drvrPayload[20] = 'r';
	drvrPayload[21] = 'v';
	drvrPayload[22] = 'r';
	drvrPayload[24] = 0x4E;
	drvrPayload[25] = 0x75;
	TestShared::assert_json_resource_contains("DRVR", 1, drvrPayload, {
		"\"name\": \"Drvr\"",
		"\"codeStartOffset\": 24",
		"\"codeSize\": 2",
	});
	{
		TestShared::CountingResourceOutput drvrOutput = TestShared::parse_single_resource("DRVR", 1, drvrPayload);
		assert(drvrOutput.json_count == 1);
		assert(drvrOutput.text_count == 1);
		assert(drvrOutput.last_text.find("rts") != std::string::npos ||
			drvrOutput.last_text.find("dc.w $4E75") != std::string::npos);
	}

	std::vector<uint8_t> dcmpPayload;
	TestShared::append_u16be(dcmpPayload, 10);
	TestShared::append_u16be(dcmpPayload, 12);
	TestShared::append_u16be(dcmpPayload, 14);
	dcmpPayload.insert(dcmpPayload.end(), {0x4E, 0x75});
	TestShared::assert_json_resource_contains("dcmp", 1, dcmpPayload, {
		"\"initLabel\": 10",
		"\"pcOffset\": 6",
		"\"codeSize\": 2",
	});
	{
		TestShared::CountingResourceOutput dcmpOutput = TestShared::parse_single_resource("dcmp", 1, dcmpPayload);
		assert(dcmpOutput.json_count == 1);
		assert(dcmpOutput.text_count == 1);
		assert(dcmpOutput.last_text.find("rts") != std::string::npos ||
			dcmpOutput.last_text.find("dc.w $4E75") != std::string::npos);
	}

	const std::vector<uint8_t> inlineCodePayload = {0x4E, 0x75};
	for(const char* type : {"PACK", "boot", "ptch"})
	{
		TestShared::CountingResourceOutput inlineOutput = TestShared::parse_single_resource(type, 1, inlineCodePayload);
		assert(inlineOutput.text_count == 1);
		assert(inlineOutput.last_text.find("rts") != std::string::npos ||
			inlineOutput.last_text.find("dc.w $4E75") != std::string::npos);
	}

	std::vector<uint8_t> fontPayload(26);
	rsrcd::write_u16be(fontPayload.data() + 0, 0x2000);
	rsrcd::write_u16be(fontPayload.data() + 2, 32);
	rsrcd::write_u16be(fontPayload.data() + 4, 33);
	rsrcd::write_u16be(fontPayload.data() + 6, 8);
	rsrcd::write_u16be(fontPayload.data() + 8, 0xFFFF);
	rsrcd::write_u16be(fontPayload.data() + 10, 2);
	rsrcd::write_u16be(fontPayload.data() + 12, 16);
	rsrcd::write_u16be(fontPayload.data() + 14, 12);
	rsrcd::write_u16be(fontPayload.data() + 16, 64);
	rsrcd::write_u16be(fontPayload.data() + 18, 9);
	rsrcd::write_u16be(fontPayload.data() + 20, 3);
	rsrcd::write_u16be(fontPayload.data() + 22, 1);
	rsrcd::write_u16be(fontPayload.data() + 24, 2);
	fontPayload.insert(fontPayload.end(), 48, 0x80);
	TestShared::assert_json_resource_contains("FONT", 128, fontPayload, {
		"\"bitDepth\": \"1\"",
		"\"fixedWidth\": true",
		"\"glyphCountIncludingMissing\": 3",
		"\"bitmapByteCount\": 48",
	});
	TestShared::assert_json_resource_contains("NFNT", 128, fontPayload, {"\"rectHeight\": 12", "\"bitmapFitsResource\": true"});
	{
		TestShared::CountingResourceOutput fontOutput = TestShared::parse_single_resource("FONT", 128, fontPayload);
		assert(fontOutput.json_count == 1);
		assert(fontOutput.rgba_count == 1);
		assert(fontOutput.last_width == 32);
		assert(fontOutput.last_height == 12);
	}

	std::vector<uint8_t> fondPayload(30);
	rsrcd::write_u16be(fondPayload.data() + 0, 0x1234);
	rsrcd::write_u16be(fondPayload.data() + 2, 128);
	rsrcd::write_u16be(fondPayload.data() + 4, 32);
	rsrcd::write_u16be(fondPayload.data() + 6, 126);
	rsrcd::write_u16be(fondPayload.data() + 8, 10);
	rsrcd::write_u16be(fondPayload.data() + 10, 3);
	rsrcd::write_u16be(fondPayload.data() + 12, 1);
	rsrcd::write_u16be(fondPayload.data() + 14, 18);
	rsrcd::write_u32be(fondPayload.data() + 16, 0);
	rsrcd::write_u32be(fondPayload.data() + 20, 0);
	rsrcd::write_u32be(fondPayload.data() + 24, 0);
	rsrcd::write_u16be(fondPayload.data() + 28, 1);
	TestShared::assert_json_resource_contains("FOND", 128, fondPayload, {
		"\"familyId\": 128",
		"\"lastChar\": 126",
		"\"version\": 1",
	});

	TestShared::assert_json_resource_contains("decl", 1, {0x12, 0x34, 0x56}, {
		"\"decodeStatus\": \"preserved\"",
		"\"byteLength\": 3",
		"\"resourceType\": \"decl\"",
	});
}

void test_resource_color_and_ui_transforms()
{
	std::vector<uint8_t> colorTablePayload;
	TestShared::append_u32be(colorTablePayload, 0x12345678);
	TestShared::append_u16be(colorTablePayload, 0x8000);
	TestShared::append_u16be(colorTablePayload, 0);
	TestShared::append_u16be(colorTablePayload, 7);
	TestShared::append_u16be(colorTablePayload, 0x1111);
	TestShared::append_u16be(colorTablePayload, 0x2222);
	TestShared::append_u16be(colorTablePayload, 0x3333);
	TestShared::assert_json_resource_contains("clut", 8, colorTablePayload, {
		"\"seed\": 305419896",
		"\"flags\": 32768",
		"\"green\": 8738",
	});
	{
		TestShared::CountingResourceOutput output = TestShared::parse_single_resource("clut", 8, colorTablePayload);
		assert(output.json_count == 1);
		assert(output.rgba_count == 1);
		assert(output.last_width == 256);
		assert(output.last_height == 16);
		assert(output.last_payload_size == 256 * 16 * 4);
	}
	TestShared::assert_json_resource_contains("wctb", 0, colorTablePayload, {"\"green\": 8738"});

	std::vector<uint8_t> plttPayload(32);
	rsrcd::write_u16be(plttPayload.data(), 1);
	rsrcd::write_u16be(plttPayload.data() + 18, 0x1111);
	rsrcd::write_u16be(plttPayload.data() + 20, 0x2222);
	rsrcd::write_u16be(plttPayload.data() + 22, 0x3333);
	TestShared::assert_json_resource_contains("pltt", 3, plttPayload, {"\"green\": 8738"});

	std::vector<uint8_t> ppatPayload(28);
	rsrcd::write_u16be(ppatPayload.data(), 0);
	rsrcd::write_u32be(ppatPayload.data() + 20, 0x80000000);
	rsrcd::write_u32be(ppatPayload.data() + 24, 0x00000001);
	TestShared::assert_json_resource_contains("ppat", 3, ppatPayload, {
		"\"type\": 0",
		"\"monochromePatternHex\": \"8000000000000001\"",
	});
	{
		TestShared::CountingResourceOutput ppatOutput = TestShared::parse_single_resource("ppat", 3, ppatPayload);
		assert(ppatOutput.json_count == 1);
		assert(ppatOutput.rgba_count == 4);
	}

	std::vector<uint8_t> pptPayload(34);
	rsrcd::write_u16be(pptPayload.data(), 1);
	rsrcd::write_u32be(pptPayload.data() + 2, 6);
	rsrcd::write_u16be(pptPayload.data() + 6, 0);
	rsrcd::write_u32be(pptPayload.data() + 26, 0x11111111);
	rsrcd::write_u32be(pptPayload.data() + 30, 0x22222222);
	TestShared::assert_json_resource_contains("ppt#", 3, pptPayload, {
		"\"patterns\"",
		"\"offset\": 6",
		"\"monochromePatternHex\": \"1111111122222222\"",
	});
	{
		TestShared::CountingResourceOutput pptOutput = TestShared::parse_single_resource("ppt#", 3, pptPayload);
		assert(pptOutput.json_count == 1);
		assert(pptOutput.rgba_count == 4);
	}

	std::vector<uint8_t> cicnPayload = TestShared::make_cicn_payload();
	TestShared::assert_json_resource_contains("cicn", 3, cicnPayload, {
		"\"pixelSize\": 1",
		"\"maskDataOffset\": 82",
		"\"colorTableOffset\": 83",
	});
	TestShared::assert_rgba_resource("cicn", 3, cicnPayload, 1, 1);

	{
		TestShared::CountingResourceOutput crsrOutput = TestShared::parse_single_resource("crsr", 4, TestShared::make_crsr_payload());
		assert(crsrOutput.json_count == 1);
		assert(crsrOutput.rgba_count == 2);
		assert(crsrOutput.last_width == 16);
		assert(crsrOutput.last_height == 16);
	}

	std::vector<uint8_t> sizePayload;
	TestShared::append_u16be(sizePayload, 0xD048);
	TestShared::append_u32be(sizePayload, 0x00100000);
	TestShared::append_u32be(sizePayload, 0x00080000);
	TestShared::assert_json_resource_contains("SIZE", -1, sizePayload, {
		"\"preferredSize\": 1048576",
		"\"minimumSize\": 524288",
		"\"saveScreen\": true",
	});

	std::vector<uint8_t> finfPayload;
	TestShared::append_u16be(finfPayload, 1);
	TestShared::append_u16be(finfPayload, 128);
	TestShared::append_u16be(finfPayload, 1);
	TestShared::append_u16be(finfPayload, 12);
	TestShared::assert_json_resource_contains("finf", 1, finfPayload, {"\"fontId\": 128", "\"size\": 12"});

	std::vector<uint8_t> dlogPayload;
	TestShared::append_u16be(dlogPayload, 1);
	TestShared::append_u16be(dlogPayload, 2);
	TestShared::append_u16be(dlogPayload, 30);
	TestShared::append_u16be(dlogPayload, 40);
	TestShared::append_u16be(dlogPayload, 4);
	TestShared::append_u16be(dlogPayload, 1);
	TestShared::append_u16be(dlogPayload, 1);
	TestShared::append_u32be(dlogPayload, 0x12345678);
	TestShared::append_u16be(dlogPayload, 128);
	dlogPayload.insert(dlogPayload.end(), {5, 'H', 'e', 'l', 'l', 'o'});
	TestShared::append_u16be(dlogPayload, 0xABCD);
	TestShared::assert_json_resource_contains("DLOG", 128, dlogPayload, {
		"\"itemsId\": 128",
		"\"title\": \"Hello\"",
		"\"autoPosition\": 43981",
	});

	std::vector<uint8_t> menuPayload;
	TestShared::append_u16be(menuPayload, 128);
	TestShared::append_u16be(menuPayload, 0);
	TestShared::append_u16be(menuPayload, 0);
	TestShared::append_u16be(menuPayload, 0);
	TestShared::append_u16be(menuPayload, 0);
	TestShared::append_u32be(menuPayload, 0x00000003);
	menuPayload.insert(menuPayload.end(), {4, 'F', 'i', 'l', 'e'});
	menuPayload.insert(menuPayload.end(), {4, 'O', 'p', 'e', 'n', 0, 'O', 0, 0});
	menuPayload.push_back(0);
	TestShared::assert_json_resource_contains("MENU", 128, menuPayload, {
		"\"title\": \"File\"",
		"\"name\": \"Open\"",
		"\"keyEquivalent\": 79",
	});

	std::vector<uint8_t> ditlPayload;
	TestShared::append_u16be(ditlPayload, 1);
	ditlPayload.insert(ditlPayload.end(), {0, 0, 0, 0});
	TestShared::append_u16be(ditlPayload, 1);
	TestShared::append_u16be(ditlPayload, 2);
	TestShared::append_u16be(ditlPayload, 30);
	TestShared::append_u16be(ditlPayload, 40);
	ditlPayload.insert(ditlPayload.end(), {0x04, 2, 'O', 'K'});
	ditlPayload.insert(ditlPayload.end(), {0, 0, 0, 0});
	TestShared::append_u16be(ditlPayload, 1);
	TestShared::append_u16be(ditlPayload, 2);
	TestShared::append_u16be(ditlPayload, 30);
	TestShared::append_u16be(ditlPayload, 40);
	ditlPayload.insert(ditlPayload.end(), {0x20, 2});
	TestShared::append_u16be(ditlPayload, 128);
	if((ditlPayload.size() & 1u) != 0)
		ditlPayload.push_back(0);
	TestShared::assert_json_resource_contains("DITL", 128, ditlPayload, {
		"\"kind\": \"button\"",
		"\"info\": \"OK\"",
		"\"resourceId\": 128",
	});
}

void RunTests()
{
	test_resource_image_transforms();
	test_resource_text_transforms();
	test_resource_metadata_transforms();
	test_resource_code_transforms();
	test_resource_color_and_ui_transforms();
}

}
