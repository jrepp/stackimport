#include "StackImportCli.h"
#include "stackimport_logging.h"

void	RunTests();

int main(int argc, char* const argv[])
{
	stackimport_logging_init();
#if defined(DEBUG) && DEBUG
	RunTests();
#endif

	stackimport::cli::Options options;
	const int parseStatus = stackimport::cli::parse_arguments(argc, argv, options);
	if(parseStatus != 0)
	{
		stackimport_logging_shutdown();
		return parseStatus;
	}

	const int status = stackimport::cli::run_selected_mode(options);
	stackimport_logging_shutdown();
	return status;
}
