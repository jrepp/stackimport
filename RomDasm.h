/*
 * RomDasm.h
 * stackimport
 *
 * ROM disassembly for Old World Macintosh ROMs.
 * Uses resource_dasm's M68KEmulator for actual disassembly.
 */

#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace stackimport {
namespace RomDasm {

struct RomInfo {
  std::string filename;
  uint32_t size;
  uint32_t crc32;
  std::string sha256;
  std::string machine_family;
  std::vector<std::string> model_names;
  uint32_t base_address;
};

struct DisassemblyRegion {
  uint32_t start_address;
  uint32_t end_address;
  std::string text;
  bool is_code;
  double confidence;
};

struct StringRegion {
  uint32_t address;
  std::string value;
  size_t length;
  bool is_pascal;
};

struct PointerTableEntry {
  uint32_t address;
  uint32_t target;
  bool is_relative;
};

struct PointerTableRegion {
  uint32_t address;
  size_t entry_count;
  std::vector<uint32_t> targets;
};

struct FunctionCandidate {
  uint32_t address;
  std::string label;
  size_t calls;
  size_t jumps;
  size_t references;
  double confidence;
};

struct DataRegion {
  uint32_t start_address;
  uint32_t end_address;
  std::string kind;
  size_t item_count;
  double confidence;
};

struct ResourceMarker {
  uint32_t address;
  std::string type;
  std::string context;
};

struct Xref {
  uint32_t from;
  uint32_t to;
  std::string kind;
  std::string mnemonic;
  size_t line;
  double confidence;
  std::string source;
};

struct TrapCall {
  uint32_t address;
  uint16_t trap_number;
  std::string trap_name;
  std::string space;
  double confidence;
  std::string source;
};

struct RomAnalysis {
  RomInfo info;
  std::vector<DisassemblyRegion> code_regions;
  std::vector<StringRegion> strings;
  std::vector<PointerTableEntry> pointer_tables;
  std::vector<PointerTableRegion> pointer_table_regions;
  std::vector<FunctionCandidate> function_candidates;
  std::vector<DataRegion> data_regions;
  std::vector<ResourceMarker> resource_markers;
  std::vector<Xref> xrefs;
  std::vector<TrapCall> traps;
  std::map<uint32_t, std::string> labels;
  uint32_t entry_point;
  size_t total_instructions;
};

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM

inline std::string trim_copy(std::string text) {
  const std::string whitespace = " \t\r\n";
  const size_t start = text.find_first_not_of(whitespace);
  if(start == std::string::npos)
    return {};
  const size_t end = text.find_last_not_of(whitespace);
  return text.substr(start, end - start + 1);
}

inline bool parse_disassembly_line(const std::string& line, std::string& opcodeBytes, std::string& mnemonic, std::string& operands) {
  if(line.size() < 10)
    return false;
  for(size_t index = 0; index < 8; index++) {
    if(!std::isxdigit(static_cast<unsigned char>(line[index])))
      return false;
  }
  size_t pos = line.find_first_not_of(' ', 8);
  if(pos == std::string::npos)
    return false;
  const size_t bytesStart = pos;
  while(pos < line.size() && (std::isxdigit(static_cast<unsigned char>(line[pos])) || line[pos] == ' '))
    pos++;
  opcodeBytes = trim_copy(line.substr(bytesStart, pos - bytesStart));
  pos = line.find_first_not_of(' ', pos);
  if(pos == std::string::npos)
    return false;
  const size_t mnemonicStart = pos;
  while(pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])))
    pos++;
  mnemonic = line.substr(mnemonicStart, pos - mnemonicStart);
  operands = trim_copy(pos < line.size() ? line.substr(pos) : std::string());
  return true;
}

inline uint16_t first_opcode_word(const std::string& opcodeBytes) {
  std::string hex;
  for(char ch : opcodeBytes) {
    if(std::isxdigit(static_cast<unsigned char>(ch))) {
      hex.push_back(ch);
      if(hex.size() == 4)
        break;
    }
  }
  if(hex.size() != 4)
    return 0;
  return static_cast<uint16_t>(std::strtoul(hex.c_str(), nullptr, 16));
}

#endif

std::string format_address(uint32_t addr);

uint32_t compute_crc32(std::span<const uint8_t> data);

std::string compute_sha256(std::span<const uint8_t> data);

RomInfo analyze_rom_header(std::span<const uint8_t> data, uint32_t base_address = 0);

RomAnalysis disassemble_rom(
    std::span<const uint8_t> data,
    const RomInfo& info,
    uint32_t start_address = 0,
    size_t page_size = 4096);

std::string export_disassembly(const RomAnalysis& analysis, bool annotate_traps = true);

std::vector<StringRegion> scan_strings(
    std::span<const uint8_t> data,
    size_t min_length = 4,
    bool scan_ascii = true,
    bool scan_pascal = true);

struct ScanOptions {
  size_t min_string_length = 4;
  bool scan_ascii = true;
  bool scan_pascal = true;
  bool disassemble_code = true;
  bool detect_pointer_tables = true;
  uint32_t start_address = 0;
  size_t max_size = 0;
};

RomAnalysis scan_rom(
    std::span<const uint8_t> data,
    const RomInfo& info,
    const ScanOptions& options = ScanOptions());

void classify_rom_structure(
    RomAnalysis& analysis,
    std::span<const uint8_t> data,
    uint32_t base_address);

}}
