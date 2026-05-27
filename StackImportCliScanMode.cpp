#include "StackImportCli.h"

#include "include/stackimport_sax.hpp"
#include "stackimport_logging.h"

namespace stackimport::cli {

int run_scan_mode(const Options& options)
{
	std::vector<uint8_t> buf;
	if(!read_entire_file(options.input_path, buf))
	{
		stackimport_quill_diagnosticf("Error: Failed to read '%s'.\n", options.input_path.c_str());
		return 5;
	}

	rsrcd::Bytes data{buf.data(), buf.size()};

	struct ScanOutput : stackimport::IBlockOutput {
		int count = 0;

		auto on_block(const stackimport::BlockRef& ref, stackimport::IStackReader&) -> BlockResult override {
			stackimport_quill_diagnosticf("  %08llx  %5u  %.*s  %5d  %7u\n",
				static_cast<unsigned long long>(ref.file_offset),
				ref.payload_bytes + 12u,
				4, ref.type.v,
				ref.id.get(),
				ref.payload_bytes);
			count++;
			return BlockResult::Continue;
		}
		auto on_error(const char* msg) -> bool override {
			stackimport_quill_diagnosticf("  ERROR: %s\n", msg);
			return false;
		}
	};

	ScanOutput scan_out;
	stackimport_quill_diagnosticf("Scanning: %s\n", options.input_path.c_str());
	stackimport_quill_diagnosticf("  Offset   Size  Type      ID  Payload\n");

	stackimport::BlockParser parser;
	auto err = parser.parse_view(data, scan_out);

	if(err != stackimport::BlockErr::None)
	{
		stackimport_quill_diagnosticf("Scan error: parse failed (code %d).\n", static_cast<int>(err));
		return 5;
	}

	stackimport_quill_diagnosticf("Total blocks: %d\n", scan_out.count);
	return 0;
}

} // namespace stackimport::cli
