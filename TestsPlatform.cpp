/*
 *  TestsPlatform.cpp
 *  stackimport
 *
 *  Platform abstraction tests: custom platform callbacks, error handling.
 *
 */

#include "TestsShared.h"

namespace TestPlatform {

void RunTests()
{
	const std::string platformStackPath = std::string("/tmp/stackimport-platform-read-") + std::to_string(std::rand());
	const std::string platformStackPackage = platformStackPath + ".xstk";
	TestShared::write_minimal_short_stak(platformStackPath);
	TestShared::CountingPlatformState platformState;
	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	platform.open_file = TestShared::counting_open_file;
	platform.read_file = TestShared::counting_read_file;
	platform.write_file = TestShared::counting_write_file;
	platform.close_file = TestShared::counting_close_file;
	platform.make_directory = TestShared::counting_make_directory;
	platform.user_data = &platformState;
	stackimport_context* platformContext = nullptr;
	assert(stackimport_context_create_with_platform(&platform, &platformContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options platformOptions = {};
	stackimport_import_options_init(&platformOptions);
	platformOptions.input_path = platformStackPath.c_str();
	platformOptions.output_package_path = platformStackPackage.c_str();
	assert(stackimport_import(platformContext, &platformOptions) == STACKIMPORT_STATUS_OK);
	stackimport_context_destroy(platformContext);
	assert(platformState.opens > 0);
	assert(platformState.reads > 0);
	assert(platformState.writes > 0);

	stackimport_log_handler invalidLogHandler = {};
	stackimport_log_handler_init(&invalidLogHandler);
	stackimport_context* context = nullptr;
	assert(stackimport_context_create_with_log_handler(&invalidLogHandler, &context) == STACKIMPORT_STATUS_INVALID_ARGUMENT);

	TestShared::LogHandlerState logHandlerState;
	stackimport_log_handler logHandler = {};
	stackimport_log_handler_init(&logHandler);
	logHandler.log = TestShared::test_log_record;
	logHandler.user_data = &logHandlerState;
	stackimport_context* logHandlerContext = nullptr;
	assert(stackimport_context_create_with_log_handler(&logHandler, &logHandlerContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options logHandlerOptions = {};
	stackimport_import_options_init(&logHandlerOptions);
	const std::string logHandlerPackage = platformStackPath + ".log-handler.xstk";
	logHandlerOptions.input_path = platformStackPath.c_str();
	logHandlerOptions.output_package_path = logHandlerPackage.c_str();
	assert(stackimport_import(logHandlerContext, &logHandlerOptions) == STACKIMPORT_STATUS_OK);
	stackimport_context_destroy(logHandlerContext);
	assert(logHandlerState.messages > 0);
	assert(logHandlerState.saw_status_or_progress);

	TestShared::LogHandlerState storageLogHandlerState;
	stackimport_log_handler storageLogHandler = {};
	stackimport_log_handler_init(&storageLogHandler);
	storageLogHandler.log = TestShared::test_log_record;
	storageLogHandler.user_data = &storageLogHandlerState;
	alignas(std::max_align_t) unsigned char logHandlerStorage[4096] = {};
	stackimport_context* storageLogHandlerContext = nullptr;
	assert(stackimport_context_init_with_log_handler(
		logHandlerStorage,
		sizeof(logHandlerStorage),
		&storageLogHandler,
		&storageLogHandlerContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options storageLogHandlerOptions = {};
	stackimport_import_options_init(&storageLogHandlerOptions);
	const std::string storageLogHandlerPackage = platformStackPath + ".storage-log-handler.xstk";
	storageLogHandlerOptions.input_path = platformStackPath.c_str();
	storageLogHandlerOptions.output_package_path = storageLogHandlerPackage.c_str();
	assert(stackimport_import(storageLogHandlerContext, &storageLogHandlerOptions) == STACKIMPORT_STATUS_OK);
	stackimport_context_deinit(storageLogHandlerContext);
	assert(storageLogHandlerState.messages > 0);
	assert(storageLogHandlerState.saw_status_or_progress);

	TestShared::LogHandlerState legacyLogHandlerState;
	stackimport_log_handler legacyLogHandler = {};
	stackimport_log_handler_init(&legacyLogHandler);
	legacyLogHandler.message = TestShared::test_log_message;
	legacyLogHandler.user_data = &legacyLogHandlerState;
	stackimport_context* legacyLogHandlerContext = nullptr;
	assert(stackimport_context_create_with_log_handler(&legacyLogHandler, &legacyLogHandlerContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options legacyLogHandlerOptions = {};
	stackimport_import_options_init(&legacyLogHandlerOptions);
	const std::string legacyLogHandlerPackage = platformStackPath + ".legacy-log-handler.xstk";
	legacyLogHandlerOptions.input_path = platformStackPath.c_str();
	legacyLogHandlerOptions.output_package_path = legacyLogHandlerPackage.c_str();
	assert(stackimport_import(legacyLogHandlerContext, &legacyLogHandlerOptions) == STACKIMPORT_STATUS_OK);
	stackimport_context_destroy(legacyLogHandlerContext);
	assert(legacyLogHandlerState.messages > 0);
	assert(legacyLogHandlerState.saw_status_or_progress);

	const std::string failingWritePackage = platformStackPath + ".write-failed.xstk";
	TestShared::CountingPlatformState failingWriteState;
	failingWriteState.fail_writes = true;
	stackimport_platform failingWritePlatform = platform;
	failingWritePlatform.user_data = &failingWriteState;
	stackimport_context* failingWriteContext = nullptr;
	assert(stackimport_context_create_with_platform(&failingWritePlatform, &failingWriteContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options failingWriteOptions = {};
	stackimport_import_options_init(&failingWriteOptions);
	failingWriteOptions.input_path = platformStackPath.c_str();
	failingWriteOptions.output_package_path = failingWritePackage.c_str();
	assert(stackimport_import(failingWriteContext, &failingWriteOptions) == STACKIMPORT_STATUS_IMPORT_FAILED);
	stackimport_context_destroy(failingWriteContext);
	assert(failingWriteState.writes > 0);

	const std::string failingClosePackage = platformStackPath + ".close-failed.xstk";
	TestShared::CountingPlatformState failingCloseState;
	failingCloseState.fail_closes = true;
	stackimport_platform failingClosePlatform = platform;
	failingClosePlatform.user_data = &failingCloseState;
	stackimport_context* failingCloseContext = nullptr;
	assert(stackimport_context_create_with_platform(&failingClosePlatform, &failingCloseContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options failingCloseOptions = {};
	stackimport_import_options_init(&failingCloseOptions);
	failingCloseOptions.input_path = platformStackPath.c_str();
	failingCloseOptions.output_package_path = failingClosePackage.c_str();
	assert(stackimport_import(failingCloseContext, &failingCloseOptions) == STACKIMPORT_STATUS_IMPORT_FAILED);
	stackimport_context_destroy(failingCloseContext);
	assert(failingCloseState.writes > 0);
	assert(failingCloseState.closes > 0);

	const std::string failingStackPackage = platformStackPath + ".allocation-failed.xstk";
	TestShared::CountingPlatformState failingPlatformState;
	failingPlatformState.fail_after_allocations = 0;
	stackimport_platform failingPlatform = {};
	stackimport_platform_init(&failingPlatform);
	failingPlatform.allocate = TestShared::counting_allocate;
	failingPlatform.deallocate = TestShared::counting_deallocate;
	failingPlatform.open_file = TestShared::counting_open_file;
	failingPlatform.read_file = TestShared::counting_read_file;
	failingPlatform.write_file = TestShared::counting_write_file;
	failingPlatform.close_file = TestShared::counting_close_file;
	failingPlatform.make_directory = TestShared::counting_make_directory;
	failingPlatform.user_data = &failingPlatformState;
	alignas(std::max_align_t) unsigned char failingStorage[4096] = {};
	stackimport_context* failingContext = nullptr;
	assert(stackimport_context_init_with_platform(failingStorage, sizeof(failingStorage), &failingPlatform, &failingContext) == STACKIMPORT_STATUS_OK);
	stackimport_import_options failingOptions = {};
	stackimport_import_options_init(&failingOptions);
	failingOptions.input_path = platformStackPath.c_str();
	failingOptions.output_package_path = failingStackPackage.c_str();
	assert(stackimport_import(failingContext, &failingOptions) == STACKIMPORT_STATUS_ALLOCATION_FAILED);
	stackimport_context_deinit(failingContext);
	assert(failingPlatformState.allocations > 0);
}

}
