/*
 *  TestsApi.cpp
 *  stackimport
 *
 *  API/ABI tests: context creation, version info, import options.
 *
 */

#include "TestsShared.h"

namespace TestApi {

struct ResourceConversionCapture {
	int payloads = 0;
	int native_payloads = 0;
	int converted_payloads = 0;
	uint32_t last_format = STACKIMPORT_RESOURCE_PAYLOAD_NATIVE;
	uint32_t last_width = 0;
	uint32_t last_height = 0;
	std::string last_text;
};

int STACKIMPORT_CALL capture_converted_payloads(
	const stackimport_resource_payload* payload,
	const void* data,
	size_t size,
	void* user_data)
{
	assert(payload);
	auto* capture = static_cast<ResourceConversionCapture*>(user_data);
	capture->payloads++;
	capture->last_format = payload->format;
	capture->last_width = payload->width;
	capture->last_height = payload->height;
	if(payload->format == STACKIMPORT_RESOURCE_PAYLOAD_NATIVE)
		capture->native_payloads++;
	else
		capture->converted_payloads++;
	if(payload->format == STACKIMPORT_RESOURCE_PAYLOAD_TEXT_UTF8)
		capture->last_text.assign(static_cast<const char*>(data), size);
	return payload->payload_size == size ? 1 : 0;
}

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

	stackimport_resource_conversion_options conversion = {};
	stackimport_resource_conversion_options_init(&conversion);
	assert(conversion.resource_payload_flags == STACKIMPORT_RESOURCE_PAYLOADS_ALL);
	std::memcpy(conversion.type, "TEXT", 4);
	const std::vector<uint8_t> looseText = {'H', 0x8E, 'l', 'l', 'o'};
	ResourceConversionCapture capture;
	conversion.data = looseText.data();
	conversion.data_size = looseText.size();
	conversion.resource_payload_flags = STACKIMPORT_RESOURCE_PAYLOADS_CONVERTED;
	conversion.resource_payload = capture_converted_payloads;
	conversion.resource_user_data = &capture;
	assert(stackimport_convert_resource(&conversion) == STACKIMPORT_STATUS_OK);
	assert(capture.payloads == 1);
	assert(capture.converted_payloads == 1);
	assert(capture.last_format == STACKIMPORT_RESOURCE_PAYLOAD_TEXT_UTF8);
	assert(capture.last_text == "H\xC3\xA9llo");

	const std::vector<uint8_t> iconPayload = TestShared::make_icon_payload();
	ResourceConversionCapture iconCapture;
	stackimport_resource_conversion_options iconConversion = {};
	stackimport_resource_conversion_options_init(&iconConversion);
	std::memcpy(iconConversion.type, "ICON", 4);
	iconConversion.id = 128;
	iconConversion.data = iconPayload.data();
	iconConversion.data_size = iconPayload.size();
	iconConversion.resource_payload_flags = STACKIMPORT_RESOURCE_PAYLOADS_CONVERTED;
	iconConversion.resource_payload = capture_converted_payloads;
	iconConversion.resource_user_data = &iconCapture;
	assert(stackimport_convert_resource(&iconConversion) == STACKIMPORT_STATUS_OK);
	assert(iconCapture.payloads == 1);
	assert(iconCapture.last_format == STACKIMPORT_RESOURCE_PAYLOAD_RGBA32);
	assert(iconCapture.last_width == 32);
	assert(iconCapture.last_height == 32);

	const uint8_t macRomanText[] = {'H', 0x8E, 'l', 'l', 'o'};
	const char* conversionError = nullptr;
	const size_t utf8Size = stackimport_mac_roman_to_utf8(macRomanText, sizeof(macRomanText), nullptr, 0, &conversionError);
	assert(utf8Size == 6);
	assert(conversionError == nullptr);
	char utf8Buffer[8] = {};
	assert(stackimport_mac_roman_to_utf8(macRomanText, sizeof(macRomanText), utf8Buffer, sizeof(utf8Buffer), &conversionError) == 6);
	assert(conversionError == nullptr);
	assert(std::string(utf8Buffer, 6) == "H\xC3\xA9llo");
	assert(stackimport_mac_roman_to_utf8(macRomanText, sizeof(macRomanText), utf8Buffer, 3, &conversionError) == 0);
	assert(std::strcmp(conversionError, "output buffer too small") == 0);
}

}
