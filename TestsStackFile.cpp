/*
 *  TestsStackFile.cpp
 *  stackimport
 *
 *  Stack file loading tests.
 *
 */

#include "TestsShared.h"

namespace TestStackFile {

namespace {

bool file_exists(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0;
}

void assert_package_file(const std::string& packagePath, const char* relativePath)
{
	assert(file_exists(packagePath + "/" + relativePath));
}

}

void RunTests()
{
	const std::string shortStackPath = std::string("/tmp/stackimport-short-stak-") + std::to_string(std::rand());
	const std::string shortStackPackage = shortStackPath + ".xstk";
	TestShared::write_minimal_short_stak(shortStackPath);
	CStackFile shortStack;
	shortStack.SetStatusMessages(false);
	shortStack.SetProgressMessages(false);
	assert(shortStack.LoadFile(shortStackPath, shortStackPackage));
	const std::string projectJson = TestShared::read_text_file(shortStackPackage + "/project.json");
	const std::string stackJson = TestShared::read_text_file(shortStackPackage + "/stack_-1.json");
	const std::string manifestJson = TestShared::read_text_file(shortStackPackage + "/source-manifest.json");
	assert_package_file(shortStackPackage, "project.json");
	assert_package_file(shortStackPackage, "stack_-1.json");
	assert_package_file(shortStackPackage, "stylesheet_-1.css");
	assert_package_file(shortStackPackage, "script-index.json");
	assert_package_file(shortStackPackage, "source-manifest.json");
	assert(projectJson.find("\"format\": \"stackimport.project\"") != std::string::npos);
	assert(projectJson.find("\"stackFile\": \"stack_-1.json\"") != std::string::npos);
	assert(projectJson.find("\"kind\": \"pattern\"") == std::string::npos);
	assert(projectJson.find("\"patternCount\": 0") != std::string::npos);
	assert(projectJson.find("\"kind\": \"stylesheet\"") != std::string::npos);
	assert(projectJson.find("\"kind\": \"scriptIndex\"") != std::string::npos);
	assert(stackJson.find("\"patternCount\": 0") != std::string::npos);
	assert(manifestJson.find("\"bytes\": 92") != std::string::npos);
	assert(manifestJson.find("\"sha256\": \"8F7F7F0A0D06C45DBB7F878F03A046A635D86ABEB7F0022523A2FA7808BE5C65\"") != std::string::npos);
	assert(manifestJson.find("\"type\": \"STAK\"") != std::string::npos);
	assert(manifestJson.find("\"offset\": 0") != std::string::npos);
	assert(manifestJson.find("\"size\": 80") != std::string::npos);
	assert(manifestJson.find("\"type\": \"TAIL\"") != std::string::npos);
	assert(manifestJson.find("\"offset\": 80") != std::string::npos);
	assert(manifestJson.find("\"typeCode\":") != std::string::npos);
	assert(manifestJson.find("\"STAK\": 1") != std::string::npos);
	assert(manifestJson.find("\"TAIL\": 1") != std::string::npos);
	assert(manifestJson.find("\"type\": \"LIST\"") != std::string::npos);
	assert(manifestJson.find("\"referencedBy\": \"STAK.listBlockId\"") != std::string::npos);
	assert(manifestJson.find("\"status\": \"missing_or_empty\"") != std::string::npos);

	const std::string freeStackPath = std::string("/tmp/stackimport-free-before-stak-") + std::to_string(std::rand());
	const std::string freeStackPackage = freeStackPath + ".xstk";
	TestShared::write_minimal_free_then_short_stak(freeStackPath);
	CStackFile freeStack;
	freeStack.SetStatusMessages(false);
	freeStack.SetProgressMessages(false);
	assert(freeStack.LoadFile(freeStackPath, freeStackPackage));
	const std::string freeManifestJson = TestShared::read_text_file(freeStackPackage + "/source-manifest.json");
	assert(freeManifestJson.find("\"type\": \"FREE\"") != std::string::npos);
	assert(freeManifestJson.find("\"status\": \"skipped\"") != std::string::npos);
	assert(freeManifestJson.find("\"type\": \"TAIL\"") != std::string::npos);
	assert(freeManifestJson.find("\"status\": \"terminal\"") != std::string::npos);
	assert(freeManifestJson.find("\"offset\": 16") != std::string::npos);
}

}
