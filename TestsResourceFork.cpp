/*
 *  TestsResourceFork.cpp
 *  stackimport
 *
 *  Resource fork loading and package write tests.
 *
 */

#include "TestsShared.h"

namespace TestResourceFork {

void RunTests()
{
	const std::string resourceForkRoot = std::string("/tmp/stackimport-rsrc-root-") + std::to_string(std::rand());
	const std::string resourceForkOutput = std::string("/tmp/stackimport-rsrc-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(resourceForkOutput.c_str(), nullptr) == 0);
	const std::vector<uint8_t> iconFork = TestShared::make_single_resource_fork("ICON", 128, TestShared::make_icon_payload());
	TestShared::ResourceForkPlatformState resourceForkPlatformState;
	resourceForkPlatformState.resource_fork_data = iconFork.data();
	resourceForkPlatformState.resource_fork_size = iconFork.size();
	stackimport_platform resourceForkPlatform = {};
	stackimport_platform_init(&resourceForkPlatform);
	resourceForkPlatform.open_file = TestShared::resource_fork_open_file;
	resourceForkPlatform.read_file = TestShared::resource_fork_read_file;
	resourceForkPlatform.write_file = TestShared::resource_fork_write_file;
	resourceForkPlatform.close_file = TestShared::resource_fork_close_file;
	resourceForkPlatform.make_directory = TestShared::counting_make_directory;
	resourceForkPlatform.user_data = &resourceForkPlatformState;
	const stackimport_internal_platform resourceForkInternalPlatform = stackimport_internal_platform_from_api(&resourceForkPlatform);
	TestShared::CountingResourceOutput packageResourceOutput;
	std::vector<CResourceSummary> resourceSummaries;
	std::string resourceForkStatus;
	uint64_t resourceForkBytes = 0;
	{
		stackimport_platform_scope resourceForkScope(resourceForkInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			resourceForkOutput,
			"Stack",
			&packageResourceOutput,
			resourceSummaries,
			resourceForkStatus,
			resourceForkBytes));
	}
	assert(resourceForkStatus == "ok");
	assert(resourceForkBytes == iconFork.size());
	assert(resourceSummaries.size() == 1);
	assert(resourceSummaries[0].type == "ICON");
	assert(resourceSummaries[0].status == "exported");
	assert(resourceSummaries[0].outputFile == "ICON_128.png");
	assert(resourceSummaries[0].outputArtifacts.size() == 1);
	assert(resourceSummaries[0].outputArtifacts[0].path == "ICON_128.png");
	assert(resourceSummaries[0].outputArtifacts[0].format == "png");
	assert(resourceSummaries[0].outputArtifacts[0].mediaType == "image/png");
	assert(packageResourceOutput.native_count == 1);
	assert(packageResourceOutput.rgba_count == 1);
	assert(resourceForkPlatformState.opens > 0);
	assert(resourceForkPlatformState.reads > 0);
	assert(resourceForkPlatformState.writes > 0);

	const std::vector<uint8_t> cursFork = TestShared::make_single_resource_fork("CURS", 24, TestShared::make_curs_payload());
	const std::string cursOutputPath = std::string("/tmp/stackimport-curs-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(cursOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState cursPlatformState;
	cursPlatformState.resource_fork_data = cursFork.data();
	cursPlatformState.resource_fork_size = cursFork.size();
	stackimport_platform cursPlatform = resourceForkPlatform;
	cursPlatform.user_data = &cursPlatformState;
	const stackimport_internal_platform cursInternalPlatform = stackimport_internal_platform_from_api(&cursPlatform);
	TestShared::CountingResourceOutput cursOutput;
	std::vector<CResourceSummary> cursSummaries;
	std::string cursStatus;
	uint64_t cursBytes = 0;
	{
		stackimport_platform_scope cursScope(cursInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			cursOutputPath,
			"Stack",
			&cursOutput,
			cursSummaries,
			cursStatus,
			cursBytes));
	}
	assert(cursStatus == "ok");
	assert(cursSummaries.size() == 1);
	assert(cursSummaries[0].type == "CURS");
	assert(cursSummaries[0].status == "exported");
	assert(cursSummaries[0].outputFile == "CURS_24.png");
	assert(cursSummaries[0].outputArtifacts.size() == 2);
	assert(cursSummaries[0].outputArtifacts[0].path == "CURS_24.png");
	assert(cursSummaries[0].outputArtifacts[1].path == "CURS_24.json");

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
	const std::vector<uint8_t> addColorFork = TestShared::make_single_resource_fork("HCbg", 77, addColorData);
	const std::string addColorOutputPath = std::string("/tmp/stackimport-addcolor-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(addColorOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState addColorPlatformState;
	addColorPlatformState.resource_fork_data = addColorFork.data();
	addColorPlatformState.resource_fork_size = addColorFork.size();
	stackimport_platform addColorPlatform = resourceForkPlatform;
	addColorPlatform.user_data = &addColorPlatformState;
	const stackimport_internal_platform addColorInternalPlatform = stackimport_internal_platform_from_api(&addColorPlatform);
	TestShared::CountingResourceOutput addColorOutput;
	std::vector<CResourceSummary> addColorSummaries;
	std::string addColorStatus;
	uint64_t addColorBytes = 0;
	{
		stackimport_platform_scope addColorScope(addColorInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			addColorOutputPath,
			"Stack",
			&addColorOutput,
			addColorSummaries,
			addColorStatus,
			addColorBytes));
	}
	assert(addColorStatus == "ok");
	assert(addColorSummaries.size() == 1);
	assert(addColorSummaries[0].type == "HCbg");
	assert(addColorSummaries[0].status == "exported");
	assert(addColorSummaries[0].outputFile == "HCbg_77.json");
	assert(addColorSummaries[0].outputArtifacts.size() == 1);
	assert(addColorSummaries[0].outputArtifacts[0].path == "HCbg_77.json");
	assert(addColorSummaries[0].outputArtifacts[0].format == "json");
	assert(addColorSummaries[0].outputArtifacts[0].mediaType == "application/json");
	const std::string addColorJson = TestShared::read_text_file(addColorOutputPath + "/HCbg_77.json");
	assert(addColorJson.find("\"targetKind\": \"background\"") != std::string::npos);

	const std::vector<uint8_t> cicnFork = TestShared::make_single_resource_fork("cicn", 22, TestShared::make_cicn_payload());
	const std::string cicnOutputPath = std::string("/tmp/stackimport-cicn-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(cicnOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState cicnPlatformState;
	cicnPlatformState.resource_fork_data = cicnFork.data();
	cicnPlatformState.resource_fork_size = cicnFork.size();
	stackimport_platform cicnPlatform = resourceForkPlatform;
	cicnPlatform.user_data = &cicnPlatformState;
	const stackimport_internal_platform cicnInternalPlatform = stackimport_internal_platform_from_api(&cicnPlatform);
	TestShared::CountingResourceOutput cicnOutput;
	std::vector<CResourceSummary> cicnSummaries;
	std::string cicnStatus;
	uint64_t cicnBytes = 0;
	{
		stackimport_platform_scope cicnScope(cicnInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			cicnOutputPath,
			"Stack",
			&cicnOutput,
			cicnSummaries,
			cicnStatus,
			cicnBytes));
	}
	assert(cicnStatus == "ok");
	assert(cicnSummaries.size() == 1);
	assert(cicnSummaries[0].type == "cicn");
	assert(cicnSummaries[0].status == "exported");
	assert(cicnSummaries[0].outputFile == "cicn_22.json");
	assert(cicnSummaries[0].outputArtifacts.size() == 2);
	assert(cicnSummaries[0].outputArtifacts[0].path == "cicn_22.json");
	assert(cicnSummaries[0].outputArtifacts[0].format == "json");
	assert(cicnSummaries[0].outputArtifacts[1].path == "cicn_22.png");
	assert(cicnSummaries[0].outputArtifacts[1].format == "png");
	assert(cicnSummaries[0].outputArtifacts[1].mediaType == "image/png");

	const std::vector<uint8_t> crsrFork = TestShared::make_single_resource_fork("crsr", 23, TestShared::make_crsr_payload());
	const std::string crsrOutputPath = std::string("/tmp/stackimport-crsr-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(crsrOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState crsrPlatformState;
	crsrPlatformState.resource_fork_data = crsrFork.data();
	crsrPlatformState.resource_fork_size = crsrFork.size();
	stackimport_platform crsrPlatform = resourceForkPlatform;
	crsrPlatform.user_data = &crsrPlatformState;
	const stackimport_internal_platform crsrInternalPlatform = stackimport_internal_platform_from_api(&crsrPlatform);
	TestShared::CountingResourceOutput crsrOutput;
	std::vector<CResourceSummary> crsrSummaries;
	std::string crsrStatus;
	uint64_t crsrBytes = 0;
	{
		stackimport_platform_scope crsrScope(crsrInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			crsrOutputPath,
			"Stack",
			&crsrOutput,
			crsrSummaries,
			crsrStatus,
			crsrBytes));
	}
	assert(crsrStatus == "ok");
	assert(crsrSummaries.size() == 1);
	assert(crsrSummaries[0].type == "crsr");
	assert(crsrSummaries[0].status == "exported");
	assert(crsrSummaries[0].outputFile == "crsr_23.json");
	assert(crsrSummaries[0].outputArtifacts.size() == 3);
	assert(crsrSummaries[0].outputArtifacts[0].path == "crsr_23.json");
	assert(crsrSummaries[0].outputArtifacts[1].path == "crsr_23.png");
	assert(crsrSummaries[0].outputArtifacts[2].path == "crsr_23_bitmap.png");

	std::vector<uint8_t> packagePpatPayload(28);
	rsrcd::write_u16be(packagePpatPayload.data(), 0);
	rsrcd::write_u32be(packagePpatPayload.data() + 20, 0x80000000);
	rsrcd::write_u32be(packagePpatPayload.data() + 24, 0x00000001);
	const std::vector<uint8_t> ppatFork = TestShared::make_single_resource_fork("ppat", 44, packagePpatPayload);
	const std::string ppatOutputPath = std::string("/tmp/stackimport-ppat-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(ppatOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState ppatPlatformState;
	ppatPlatformState.resource_fork_data = ppatFork.data();
	ppatPlatformState.resource_fork_size = ppatFork.size();
	stackimport_platform ppatPlatform = resourceForkPlatform;
	ppatPlatform.user_data = &ppatPlatformState;
	const stackimport_internal_platform ppatInternalPlatform = stackimport_internal_platform_from_api(&ppatPlatform);
	TestShared::CountingResourceOutput ppatOutput;
	std::vector<CResourceSummary> ppatSummaries;
	std::string ppatStatus;
	uint64_t ppatBytes = 0;
	{
		stackimport_platform_scope ppatScope(ppatInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			ppatOutputPath,
			"Stack",
			&ppatOutput,
			ppatSummaries,
			ppatStatus,
			ppatBytes));
	}
	assert(ppatStatus == "ok");
	assert(ppatSummaries.size() == 1);
	assert(ppatSummaries[0].type == "ppat");
	assert(ppatSummaries[0].status == "exported");
	assert(ppatSummaries[0].outputFile == "ppat_44.json");
	assert(ppatSummaries[0].outputArtifacts.size() == 5);
	assert(ppatSummaries[0].outputArtifacts[0].path == "ppat_44.json");
	assert(ppatSummaries[0].outputArtifacts[1].path == "ppat_44_color.png");
	assert(ppatSummaries[0].outputArtifacts[2].path == "ppat_44_color_tiled.png");
	assert(ppatSummaries[0].outputArtifacts[3].path == "ppat_44_bitmap.png");
	assert(ppatSummaries[0].outputArtifacts[4].path == "ppat_44_bitmap_tiled.png");

	std::vector<uint8_t> sicnPayload(64, 0x00);
	for(size_t row = 0; row < 16; row++)
	{
		sicnPayload[row * 2] = 0xFF;
		sicnPayload[32 + row * 2 + 1] = 0xFF;
	}
	const std::vector<uint8_t> sicnFork = TestShared::make_single_resource_fork("SICN", 33, sicnPayload);
	const std::string sicnOutputPath = std::string("/tmp/stackimport-sicn-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(sicnOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState sicnPlatformState;
	sicnPlatformState.resource_fork_data = sicnFork.data();
	sicnPlatformState.resource_fork_size = sicnFork.size();
	stackimport_platform sicnPlatform = resourceForkPlatform;
	sicnPlatform.user_data = &sicnPlatformState;
	const stackimport_internal_platform sicnInternalPlatform = stackimport_internal_platform_from_api(&sicnPlatform);
	TestShared::CountingResourceOutput sicnOutput;
	std::vector<CResourceSummary> sicnSummaries;
	std::string sicnStatus;
	uint64_t sicnBytes = 0;
	{
		stackimport_platform_scope sicnScope(sicnInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			sicnOutputPath,
			"Stack",
			&sicnOutput,
			sicnSummaries,
			sicnStatus,
			sicnBytes));
	}
	assert(sicnStatus == "ok");
	assert(sicnSummaries.size() == 1);
	assert(sicnSummaries[0].type == "SICN");
	assert(sicnSummaries[0].status == "exported");
	assert(sicnSummaries[0].outputFile == "SICN_33_00.png");
	assert(sicnSummaries[0].outputArtifacts.size() == 2);
	assert(sicnSummaries[0].outputArtifacts[0].path == "SICN_33_00.png");
	assert(sicnSummaries[0].outputArtifacts[0].variantIndex == 0);
	assert(sicnSummaries[0].outputArtifacts[1].path == "SICN_33_01.png");
	assert(sicnSummaries[0].outputArtifacts[1].variantIndex == 1);

	const std::vector<uint8_t> strFork = TestShared::make_single_resource_fork("STR ", 12, {5, 'H', 0x8E, 'l', 'l', 'o'});
	const std::string textOutputPath = std::string("/tmp/stackimport-text-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(textOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState textPlatformState;
	textPlatformState.resource_fork_data = strFork.data();
	textPlatformState.resource_fork_size = strFork.size();
	stackimport_platform textPlatform = resourceForkPlatform;
	textPlatform.user_data = &textPlatformState;
	const stackimport_internal_platform textInternalPlatform = stackimport_internal_platform_from_api(&textPlatform);
	TestShared::CountingResourceOutput textOutput;
	std::vector<CResourceSummary> textSummaries;
	std::string textStatus;
	uint64_t textBytes = 0;
	{
		stackimport_platform_scope textScope(textInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			textOutputPath,
			"Stack",
			&textOutput,
			textSummaries,
			textStatus,
			textBytes));
	}
	assert(textStatus == "ok");
	assert(textSummaries.size() == 1);
	assert(textSummaries[0].type == "STR ");
	assert(textSummaries[0].status == "exported");
	assert(textSummaries[0].outputFile == "resource-text/Stack_STR%20_12.txt");
	assert(textSummaries[0].outputArtifacts.size() == 1);
	assert(textSummaries[0].outputArtifacts[0].path == "resource-text/Stack_STR%20_12.txt");
	assert(textSummaries[0].outputArtifacts[0].format == "text");
	const std::string convertedText = TestShared::read_text_file(textOutputPath + "/resource-text/Stack_STR%20_12.txt");
	assert(convertedText == "H\xC3\xA9llo");

	std::vector<uint8_t> packageCodePayload;
	TestShared::append_u16be(packageCodePayload, 2);
	TestShared::append_u16be(packageCodePayload, 3);
	packageCodePayload.insert(packageCodePayload.end(), {0x4E, 0x75});
	const std::vector<uint8_t> codeFork = TestShared::make_single_resource_fork("CODE", 1, packageCodePayload);
	const std::string codeOutputPath = std::string("/tmp/stackimport-code-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(codeOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState codePlatformState;
	codePlatformState.resource_fork_data = codeFork.data();
	codePlatformState.resource_fork_size = codeFork.size();
	stackimport_platform codePlatform = resourceForkPlatform;
	codePlatform.user_data = &codePlatformState;
	const stackimport_internal_platform codeInternalPlatform = stackimport_internal_platform_from_api(&codePlatform);
	TestShared::CountingResourceOutput codeOutput;
	std::vector<CResourceSummary> codeSummaries;
	std::string codeStatus;
	uint64_t codeBytes = 0;
	{
		stackimport_platform_scope codeScope(codeInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			codeOutputPath,
			"Stack",
			&codeOutput,
			codeSummaries,
			codeStatus,
			codeBytes));
	}
	assert(codeStatus == "ok");
	assert(codeSummaries.size() == 1);
	assert(codeSummaries[0].type == "CODE");
	assert(codeSummaries[0].status == "exported");
	assert(codeSummaries[0].outputFile == "CODE_1.json");
	assert(codeSummaries[0].disassemblyFile == "resource-disassembly/Stack_CODE_mac68k_1.s");
	assert(codeSummaries[0].outputArtifacts.size() == 2);
	assert(codeSummaries[0].outputArtifacts[0].path == "resource-disassembly/Stack_CODE_mac68k_1.s");
	assert(codeSummaries[0].outputArtifacts[0].format == "text");
	assert(codeSummaries[0].outputArtifacts[1].path == "CODE_1.json");

	const std::vector<uint8_t> packFork = TestShared::make_single_resource_fork("PACK", 7, {0x4E, 0x75});
	const std::string packOutputPath = std::string("/tmp/stackimport-pack-output-") + std::to_string(std::rand());
	assert(TestShared::counting_make_directory(packOutputPath.c_str(), nullptr) == 0);
	TestShared::ResourceForkPlatformState packPlatformState;
	packPlatformState.resource_fork_data = packFork.data();
	packPlatformState.resource_fork_size = packFork.size();
	stackimport_platform packPlatform = resourceForkPlatform;
	packPlatform.user_data = &packPlatformState;
	const stackimport_internal_platform packInternalPlatform = stackimport_internal_platform_from_api(&packPlatform);
	TestShared::CountingResourceOutput packOutput;
	std::vector<CResourceSummary> packSummaries;
	std::string packStatus;
	uint64_t packBytes = 0;
	{
		stackimport_platform_scope packScope(packInternalPlatform);
		assert(stackimport_load_resource_fork(
			resourceForkRoot,
			packOutputPath,
			"Stack",
			&packOutput,
			packSummaries,
			packStatus,
			packBytes));
	}
	assert(packStatus == "ok");
	assert(packSummaries.size() == 1);
	assert(packSummaries[0].type == "PACK");
	assert(packSummaries[0].status == "disassembled");
	assert(packSummaries[0].disassemblyFile == "resource-disassembly/Stack_PACK_mac68k_7.s");
	assert(packSummaries[0].outputArtifacts.size() == 1);
	assert(packSummaries[0].outputArtifacts[0].path == "resource-disassembly/Stack_PACK_mac68k_7.s");
}

}
