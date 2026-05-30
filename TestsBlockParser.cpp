/*
 *  TestsBlockParser.cpp
 *  stackimport
 *
 *  Block parser tests: truncated blocks, block identifiers.
 *
 */

#include "TestsShared.h"

namespace TestBlockParser {

void RunBlockIdentifierTests();

void RunTests()
{
	std::vector<uint8_t> truncatedBlock;
	TestShared::append_block_header(truncatedBlock, 16, "STAK", -1);
	truncatedBlock.insert(truncatedBlock.end(), {0xAA, 0xBB});
	TestShared::CapturingBlockOutput streamingCapture;
	TestShared::MemoryStackReader truncatedReader(rsrcd::Bytes{truncatedBlock.data(), truncatedBlock.size()});
	assert(stackimport::BlockParser{}.parse(truncatedReader, streamingCapture) == stackimport::BlockErr::TruncatedPayload);
	assert(streamingCapture.block_count == 0);
	assert(streamingCapture.error == "truncated payload");

	TestShared::CapturingBlockOutput viewCapture;
	assert(stackimport::BlockParser{}.parse_view(rsrcd::Bytes{truncatedBlock.data(), truncatedBlock.size()}, viewCapture) == stackimport::BlockErr::TruncatedPayload);
	assert(viewCapture.block_count == 0);
	assert(viewCapture.error == "truncated payload");

	const uint8_t codeBytes[] = {0x4E, 0x75, 0xA9, 0xF0, 0x12};
	TestShared::TestResourceOutput resourceOutput;
	stackimport::ResourcePayload payload = {};
	payload.format = stackimport::ResourcePayloadFormat::Native;
	payload.data = rsrcd::Bytes{codeBytes, sizeof(codeBytes)};
	assert(resourceOutput.wants_resource_payload(payload));
	assert(resourceOutput.on_resource_payload(payload));
	assert(resourceOutput.seen_native);

	RunBlockIdentifierTests();
}

void RunBlockIdentifierTests()
{
	CStackBlockIdentifier a("TEST", 1);
	CStackBlockIdentifier b("TEST", 2);
	CStackBlockIdentifier wildcardYes("TEST");
	CStackBlockIdentifier wildcardNo("TOON");

	assert((a == b) == false);
	assert(a == wildcardYes);
	assert(wildcardYes == a);
	assert(b == wildcardYes);
	assert(b == wildcardYes);
	assert((a == wildcardNo) == false);
	assert((wildcardNo == a) == false);
	assert((b == wildcardNo) == false);
	assert((wildcardNo == b) == false);

	assert(a < b);
	assert((a < wildcardYes) == false);
	assert((wildcardYes < a) == false);
	assert((b < wildcardYes) == false);
	assert((wildcardYes < b) == false);
	assert(a < wildcardNo);
	assert((wildcardNo < a) == false);
	assert(b < wildcardNo);
	assert((wildcardNo < b) == false);

	assert((a > b) == false);
	assert((a > wildcardYes) == false);
	assert((wildcardYes > a) == false);
	assert((b > wildcardYes) == false);
	assert((wildcardYes > b) == false);
	assert((a > wildcardNo) == false);
	assert(wildcardNo > a);
	assert((b > wildcardNo) == false);
	assert(wildcardNo > b);
}

}
