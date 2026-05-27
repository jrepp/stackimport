#pragma once

#include "stackimport_c.h"
#include "RomDasm.h"

#include <cstdint>
#include <string>
#include <vector>

namespace stackimport::cli {

enum class Mode {
	Import,
	Scan,
	Rom,
};

struct Options {
	Mode mode = Mode::Import;
	uint32_t flags = STACKIMPORT_IMPORT_DUMP_RAW_BLOCKS;
	std::string input_path;
	std::string output_path;
	std::string atlas_output_path;
	std::string source_root_path;
	bool emit_atlas = false;
	bool emit_json = false;
	bool emit_assets = false;
	bool exit_after_parse = false;
	uint32_t rom_base_address = 0;
};

const char* syntax_string();

int parse_arguments(int argc, char* const argv[], Options& options);
int run_scan_mode(const Options& options);
int run_rom_mode(const Options& options);
int run_import_mode(const Options& options);
int run_selected_mode(const Options& options);

std::string absolute_path(const char* path);
std::string default_output_package_path(const std::string& input_path);
std::string path_join(const std::string& dir, const std::string& name);
std::string relative_to_cwd(const std::string& path);
std::string current_utc_timestamp();
std::string json_escape(const std::string& text);
std::string tsv_escape(const std::string& text);

bool read_entire_file(const std::string& path, std::vector<uint8_t>& out);
bool write_text_file(const std::string& path, const std::string& text);
bool make_directories_recursive(const std::string& path);

const char* rom_disassembly_mode();
const char* initial_bytes_hypothesis(const std::vector<uint8_t>& buf, double& confidence);
std::string rom_id_from_info(const RomDasm::RomAnalysis& analysis);

} // namespace stackimport::cli
