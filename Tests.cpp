/*
 *  Tests.cpp
 *  stackimport
 *
 *  Created by Mr. Z. on 10/06/06.
 *  Copyright 2006 Mr Z. All rights reserved.
 *
 */

#include "CStackFile.h"
#include "Mac68kDisassembly.h"
#include "stackimport_c.h"
#include <assert.h>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace {

void* test_allocate(size_t size, size_t alignment, void*)
{
	void* ptr = nullptr;
#if defined(_WIN32)
	if(alignment < sizeof(void*))
		alignment = sizeof(void*);
	ptr = _aligned_malloc(size, alignment);
	return ptr;
#else
	if(posix_memalign(&ptr, alignment, size) != 0)
		return nullptr;
	return ptr;
#endif
}

void test_deallocate(void* ptr, void*)
{
#if defined(_WIN32)
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

int test_resource_wants(const stackimport_resource_payload* payload, void*)
{
	return payload && payload->format == STACKIMPORT_RESOURCE_PAYLOAD_NATIVE;
}

int test_resource_payload(const stackimport_resource_payload* payload, const void* data, size_t size, void*)
{
	return payload && payload->payload_size == size && (size == 0 || data != nullptr);
}

class TestResourceOutput final : public stackimport::IResourceOutput {
public:
	auto wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		return payload.format == stackimport::ResourcePayloadFormat::Native;
	}

	auto on_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		seen_native = payload.format == stackimport::ResourcePayloadFormat::Native;
		return true;
	}

	bool seen_native = false;
};

class CountingResourceOutput final : public stackimport::IResourceOutput {
public:
	auto wants_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		wants_count++;
		return payload.format == stackimport::ResourcePayloadFormat::Native ||
			payload.format == stackimport::ResourcePayloadFormat::Rgba32;
	}

	auto on_resource_payload(const stackimport::ResourcePayload& payload) -> bool override
	{
		if(payload.format == stackimport::ResourcePayloadFormat::Native)
			native_count++;
		if(payload.format == stackimport::ResourcePayloadFormat::Rgba32)
		{
			rgba_count++;
			last_width = payload.width;
			last_height = payload.height;
			last_payload_size = payload.data.size;
		}
		return true;
	}

	int wants_count = 0;
	int native_count = 0;
	int rgba_count = 0;
	uint32_t last_width = 0;
	uint32_t last_height = 0;
	size_t last_payload_size = 0;
};

void write_basic_resource_fork_header(uint8_t* fork, uint32_t data_off, uint32_t map_off, uint32_t data_len, uint32_t map_len)
{
	rsrcd::write_u32be(fork + 0, data_off);
	rsrcd::write_u32be(fork + 4, map_off);
	rsrcd::write_u32be(fork + 8, data_len);
	rsrcd::write_u32be(fork + 12, map_len);
}

}


void	RunTests()
{
	CStackBlockIdentifier		a( "TEST", 1 );
	CStackBlockIdentifier		b( "TEST", 2 );
	CStackBlockIdentifier		wildcardYes( "TEST" );
	CStackBlockIdentifier		wildcardNo( "TOON" );
	
	// ==
	assert((a == b) == false);
	
	assert(a == wildcardYes);
	assert(wildcardYes == a);
	
	assert(b == wildcardYes);
	assert(b == wildcardYes);
	
	assert((a == wildcardNo) == false);
	assert((wildcardNo == a) == false);
	
	assert((b == wildcardNo) == false);
	assert((wildcardNo == b) == false);
	
	// <
	assert( a < b );
	
	assert( (a < wildcardYes) == false );
	assert( (wildcardYes < a) == false );
	
	assert( (b < wildcardYes) == false );
	assert( (wildcardYes < b) == false );
	
	assert( a < wildcardNo );
	assert( (wildcardNo < a) == false );
	
	assert( b < wildcardNo );
	assert( (wildcardNo < b) == false );
	
	// >
	assert( (a > b) == false );
	
	assert( (a > wildcardYes) == false );
	assert( (wildcardYes > a) == false );
	
	assert( (b > wildcardYes) == false );
	assert( (wildcardYes > b) == false );
	
	assert( (a > wildcardNo) == false );
	assert( wildcardNo > a );
	
	assert( (b > wildcardNo) == false );
	assert( wildcardNo > b );

	assert(stackimport_context_size() <= 4096);
	assert(stackimport_api_version() == STACKIMPORT_API_VERSION);
	alignas(std::max_align_t) unsigned char storage[4096] = {};
	stackimport_context* context = nullptr;
	assert(stackimport_context_init(storage, sizeof(storage), &context) == STACKIMPORT_STATUS_OK);
	stackimport_context_deinit(context);

	stackimport_allocator allocator = {};
	stackimport_allocator_init(&allocator);
	allocator.allocate = test_allocate;
	allocator.deallocate = test_deallocate;
	assert(stackimport_context_create(&allocator, &context) == STACKIMPORT_STATUS_OK);
	stackimport_import_options options = {};
	stackimport_import_options_init(&options);
	assert(options.resource_payload_flags == STACKIMPORT_RESOURCE_PAYLOADS_ALL);
	options.resource_wants = test_resource_wants;
	options.resource_payload = test_resource_payload;
	assert(static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_RGBA32) == 1u);
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_INVALID_ARGUMENT);
	options.input_path = "/tmp/missing-stack";
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_INVALID_ARGUMENT);
	options.output_package_path = "/tmp/missing-stack.xstk";
	options.flags = 1u << 31;
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_UNSUPPORTED_OPTION);
	stackimport_context_destroy(context);

	const uint8_t codeBytes[] = {0x4E, 0x75, 0xA9, 0xF0, 0x12};
	TestResourceOutput resourceOutput;
	stackimport::ResourcePayload payload = {};
	payload.format = stackimport::ResourcePayloadFormat::Native;
	payload.data = rsrcd::Bytes{codeBytes, sizeof(codeBytes)};
	assert(resourceOutput.wants_resource_payload(payload));
	assert(resourceOutput.on_resource_payload(payload));
	assert(resourceOutput.seen_native);

	uint8_t fork[256] = {};
	write_basic_resource_fork_header(fork, 16, 160, 132, 96);
	rsrcd::write_u32be(fork + 16, 128);
	for(size_t i = 0; i < 128; ++i)
		fork[20 + i] = 0xFF;
	uint8_t* map = fork + 160;
	rsrcd::write_u16be(map + 24, 28);
	rsrcd::write_u16be(map + 26, 64);
	uint8_t* type_list = map + 28;
	rsrcd::write_u16be(type_list, 0);
	std::memcpy(type_list + 2, "ICON", 4);
	rsrcd::write_u16be(type_list + 6, 0);
	rsrcd::write_u16be(type_list + 8, 10);
	uint8_t* ref = type_list + 10;
	rsrcd::write_u16be(ref + 0, 128);
	rsrcd::write_u16be(ref + 2, 0xFFFF);
	rsrcd::write_u32be(ref + 4, 0);
	rsrcd::write_u32be(ref + 8, 0);
	CountingResourceOutput countingOutput;
	assert(stackimport::ResourceForkParser{}.parse_fork(rsrcd::Bytes{fork, sizeof(fork)}, countingOutput));
	assert(countingOutput.native_count == 1);
	assert(countingOutput.rgba_count == 1);
	assert(countingOutput.last_width == 32);
	assert(countingOutput.last_height == 32);
	assert(countingOutput.last_payload_size == 32u * 32u * 4u);

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
