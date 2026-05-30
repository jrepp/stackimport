/*
 *  TestsApi.cpp
 *  stackimport
 *
 *  API/ABI tests: context creation, version info, import options.
 *
 */

#include "TestsShared.h"

namespace TestApi {

void RunTests()
{
	assert(stackimport_context_size() <= 4096);
	assert(stackimport_api_version() == STACKIMPORT_API_VERSION);
	assert(std::strcmp(stackimport_version_string(), STACKIMPORT_VERSION_STRING) == 0);
	assert(stackimport_version_packed() == STACKIMPORT_VERSION_PACKED);
	assert(stackimport_version_packed() == 0x00000300u);
	assert(STACKIMPORT_VERSION_MAJOR == 0);
	assert(STACKIMPORT_VERSION_MINOR == 3);
	assert(STACKIMPORT_VERSION_PATCH == 0);
	assert(stackimport_context_abi_signature() != 0);
	assert(std::strcmp(stackimport_status_string(STACKIMPORT_STATUS_ABI_MISMATCH), "ABI mismatch") == 0);
	alignas(std::max_align_t) unsigned char storage[4096] = {};
	stackimport_context* context = nullptr;
	assert(stackimport_context_init(storage, sizeof(storage), &context) == STACKIMPORT_STATUS_OK);
	stackimport_context_deinit(context);
	assert(stackimport_context_init(storage, sizeof(storage), &context) == STACKIMPORT_STATUS_OK);
	const uint32_t contextSignature = stackimport_context_abi_signature();
	const uint32_t badContextSignature = contextSignature ^ 0xFFFFFFFFu;
	std::memcpy(storage, &badContextSignature, sizeof(badContextSignature));
	stackimport_import_options abiMismatchOptions = {};
	stackimport_import_options_init(&abiMismatchOptions);
	abiMismatchOptions.input_path = "/tmp/missing-stack";
	abiMismatchOptions.output_package_path = "/tmp/missing-stack.xstk";
	assert(stackimport_import(context, &abiMismatchOptions) == STACKIMPORT_STATUS_ABI_MISMATCH);
	std::memcpy(storage, &contextSignature, sizeof(contextSignature));
	stackimport_context_deinit(context);

	stackimport_allocator allocator = {};
	stackimport_allocator_init(&allocator);
	allocator.allocate = TestShared::test_allocate;
	allocator.deallocate = TestShared::test_deallocate;
	assert(stackimport_context_create(&allocator, &context) == STACKIMPORT_STATUS_OK);
	stackimport_import_options options = {};
	stackimport_import_options_init(&options);
	assert(options.resource_payload_flags == STACKIMPORT_RESOURCE_PAYLOADS_ALL);
	options.resource_wants = TestShared::test_resource_wants;
	options.resource_payload = TestShared::test_resource_payload;
	assert(static_cast<uint32_t>(STACKIMPORT_RESOURCE_PAYLOAD_RGBA32) == 1u);
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_INVALID_ARGUMENT);
	options.input_path = "/tmp/missing-stack";
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_INVALID_ARGUMENT);
	options.output_package_path = "/tmp/missing-stack.xstk";
	options.flags = 1u << 31;
	assert(stackimport_import(context, &options) == STACKIMPORT_STATUS_UNSUPPORTED_OPTION);
	stackimport_context_destroy(context);
}

}
