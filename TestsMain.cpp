/*
 *  TestsMain.cpp
 *  stackimport
 *
 *  Main entry point for the test suite. Calls all test suites.
 *
 */

#include "stackimport_logging.h"

namespace TestApi { void RunTests(); }
namespace TestBlockParser { void RunTests(); }
namespace TestResourceTransforms { void RunTests(); }
namespace TestResourceFork { void RunTests(); }
namespace TestRomAnalysis { void RunTests(); }
namespace TestQuickTime { void RunTests(); }
namespace TestSound { void RunTests(); }
namespace TestDisassembly { void RunTests(); }
namespace TestStackFile { void RunTests(); }
namespace TestPlatform { void RunTests(); }
namespace TestAllocator { void RunTests(); }

int main()
{
	stackimport_logging_init();

	TestApi::RunTests();
	TestBlockParser::RunTests();
	TestResourceTransforms::RunTests();
	TestResourceFork::RunTests();
	TestRomAnalysis::RunTests();
	TestQuickTime::RunTests();
	TestSound::RunTests();
	TestDisassembly::RunTests();
	TestStackFile::RunTests();
	TestPlatform::RunTests();
	TestAllocator::RunTests();

	stackimport_quill_log_message(STACKIMPORT_MESSAGE_INFO, "All tests passed.");
	stackimport_logging_shutdown();
	return 0;
}
