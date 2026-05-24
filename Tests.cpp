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
#include <span>

namespace {

void* test_allocate(size_t size, size_t alignment, void*)
{
	void* ptr = nullptr;
	if(posix_memalign(&ptr, alignment, size) != 0)
		return nullptr;
	return ptr;
}

void test_deallocate(void* ptr, void*)
{
	free(ptr);
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
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_INVALID_ARGUMENT);
	options.input_path = "/tmp/missing-stack";
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_INVALID_ARGUMENT);
	options.output_package_path = "/tmp/missing-stack.xstk";
	options.flags = 1u << 31;
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_UNSUPPORTED_OPTION);
	stackimport_context_destroy(context);

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
