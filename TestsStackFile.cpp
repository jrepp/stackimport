/*
 *  TestsStackFile.cpp
 *  stackimport
 *
 *  Stack file loading tests.
 *
 */

#include "TestsShared.h"

namespace TestStackFile {

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
	assert(projectJson.find("\"kind\": \"pattern\"") == std::string::npos);
	assert(projectJson.find("\"patternCount\": 0") != std::string::npos);
	assert(stackJson.find("\"patternCount\": 0") != std::string::npos);
	assert(manifestJson.find("\"typeCode\":") != std::string::npos);

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
