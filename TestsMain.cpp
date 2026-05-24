#include "stackimport_logging.h"

void RunTests();

int main()
{
	stackimport_logging_init();
	RunTests();
	stackimport_quill_log_message(STACKIMPORT_MESSAGE_INFO, "All tests passed.");
	stackimport_logging_shutdown();
	return 0;
}
