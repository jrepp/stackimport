/*
 *  TestsAllocator.cpp
 *  stackimport
 *
 *  Allocator tests: RapidJson allocator and CBuf tests.
 *
 */

#include "TestsShared.h"

namespace TestAllocator {

void RunTests()
{
	TestShared::CountingPlatformState rapidJsonFailingState;
	rapidJsonFailingState.fail_after_allocations = 0;
	stackimport_platform rapidJsonFailingPlatform = {};
	stackimport_platform_init(&rapidJsonFailingPlatform);
	rapidJsonFailingPlatform.allocate = TestShared::counting_allocate;
	rapidJsonFailingPlatform.deallocate = TestShared::counting_deallocate;
	rapidJsonFailingPlatform.user_data = &rapidJsonFailingState;
	const stackimport_internal_platform rapidJsonInternalPlatform = stackimport_internal_platform_from_api(&rapidJsonFailingPlatform);
	{
		stackimport_platform_scope scope(rapidJsonInternalPlatform);
		StackImportRapidJsonAllocator rapidJsonAllocator;
		stackimport_internal_reset_allocation_failure();
		assert(rapidJsonAllocator.Malloc(16) == nullptr);
		assert(stackimport_internal_had_allocation_failure());
		stackimport_internal_reset_allocation_failure();
		assert(rapidJsonAllocator.Realloc(nullptr, 0, 16) == nullptr);
		assert(stackimport_internal_had_allocation_failure());
	}

	CBuf sharedBuffer(4);
	sharedBuffer[0] = 'A';
	assert(sharedBuffer.checked_buf(0, 1) != nullptr);
	assert(sharedBuffer.checked_buf(4, 1) == nullptr);
	CBuf copiedBuffer(sharedBuffer);
	{
		stackimport_platform_scope scope(rapidJsonInternalPlatform);
		stackimport_internal_reset_allocation_failure();
		copiedBuffer[0] = 'B';
		assert(stackimport_internal_had_allocation_failure());
		const CBuf& sharedView = sharedBuffer;
		const CBuf& copiedView = copiedBuffer;
		assert(sharedView[0] == 'A');
		assert(copiedView[0] == 'A');
		stackimport_internal_reset_allocation_failure();
		char* copiedBytes = copiedBuffer.buf(0, 1);
		copiedBytes[0] = 'C';
		assert(stackimport_internal_had_allocation_failure());
		assert(sharedView[0] == 'A');
		assert(copiedView[0] == 'A');
		stackimport_internal_reset_allocation_failure();
		assert(copiedBuffer.checked_buf(0, 1) == nullptr);
		assert(stackimport_internal_had_allocation_failure());
		stackimport_internal_reset_allocation_failure();
		sharedBuffer.resize(8);
		assert(stackimport_internal_had_allocation_failure());
		assert(sharedView.size() == 4);
		assert(sharedView[0] == 'A');
		stackimport_internal_reset_allocation_failure();
		copiedBuffer.resize(8);
		assert(stackimport_internal_had_allocation_failure());
		assert(copiedView.size() == 4);
		assert(copiedView[0] == 'A');
	}
}

}
