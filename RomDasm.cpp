/*
 * RomDasm.cpp
 * stackimport
 *
 * ROM disassembly for Old World Macintosh ROMs.
 */

#include "RomDasm.h"

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
#include "StackImportResourceDasmDisassemblyAdapter.h"
#endif

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <algorithm>
#include <set>
#include <unordered_map>

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
#include <phosg/Hash.hh>
#endif

namespace stackimport {
namespace RomDasm {

namespace {

bool parse_address_prefix(const std::string& line, uint32_t& address) {
  if(line.size() < 8)
    return false;
  for(size_t i = 0; i < 8; i++) {
    if(!std::isxdigit(static_cast<unsigned char>(line[i])))
      return false;
  }
  address = static_cast<uint32_t>(std::strtoul(line.substr(0, 8).c_str(), nullptr, 16));
  return true;
}

std::string trim_text(std::string text) {
  const std::string whitespace = " \t\r\n";
  const size_t start = text.find_first_not_of(whitespace);
  if(start == std::string::npos)
    return {};
  const size_t end = text.find_last_not_of(whitespace);
  return text.substr(start, end - start + 1);
}

bool parse_disassembly_text_line(
    const std::string& line,
    uint32_t& address,
    std::string& mnemonic,
    std::string& operands) {
  if(!parse_address_prefix(line, address))
    return false;

  size_t pos = line.find_first_not_of(' ', 8);
  if(pos == std::string::npos)
    return false;
  while(pos < line.size() && (std::isxdigit(static_cast<unsigned char>(line[pos])) || line[pos] == ' '))
    pos++;
  pos = line.find_first_not_of(' ', pos);
  if(pos == std::string::npos)
    return false;
  const size_t mnemonic_start = pos;
  while(pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])))
    pos++;
  mnemonic = line.substr(mnemonic_start, pos - mnemonic_start);
  operands = trim_text(pos < line.size() ? line.substr(pos) : std::string());
  std::transform(mnemonic.begin(), mnemonic.end(), mnemonic.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return true;
}

bool extract_comment_target(const std::string& operands, uint32_t& target) {
  const size_t marker = operands.find("/* ");
  if(marker == std::string::npos || marker + 11 > operands.size())
    return false;
  const std::string candidate = operands.substr(marker + 3, 8);
  for(char ch : candidate) {
    if(!std::isxdigit(static_cast<unsigned char>(ch)))
      return false;
  }
  target = static_cast<uint32_t>(std::strtoul(candidate.c_str(), nullptr, 16));
  return true;
}

std::string printable_context(std::span<const uint8_t> data, size_t offset) {
  const size_t start = offset > 16 ? offset - 16 : 0;
  const size_t end = std::min(data.size(), offset + 48);
  std::string result;
  result.reserve(end - start);
  for(size_t i = start; i < end; i++) {
    const uint8_t ch = data[i];
    result.push_back((ch >= 32 && ch <= 126) ? static_cast<char>(ch) : '.');
  }
  return result;
}

}

std::string format_address(uint32_t addr) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(addr));
  return std::string(buf);
}

uint32_t compute_crc32(std::span<const uint8_t> data) {
  uint32_t crc = 0xFFFFFFFF;
  static uint32_t table[256] = {0};
  static bool table_init = false;
  if(!table_init) {
    for(uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for(uint32_t j = 0; j < 8; j++)
        c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    table_init = true;
  }
  for(uint8_t b : data)
    crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFF;
}

std::string compute_sha256(std::span<const uint8_t> data) {
#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
  return phosg::SHA256(data.data(), data.size()).hex();
#else
  char hex[65] = {0};
  for(size_t i = 0; i < data.size() && i < 32; i++) {
    snprintf(hex + i * 2, 3, "%02x", data[i]);
  }
  return std::string(hex);
#endif
}

struct RomHeader {
  uint32_t magic1;
  uint16_t magic2;
  uint16_t size1;
  uint16_t size2;
  uint32_t entry_vector;
  uint16_t checksum;
  uint8_t version;
  uint8_t page_flag;
};

static bool detect_rom_type(std::span<const uint8_t> data) {
  if(data.size() < 32)
    return false;
  uint32_t suspect_magic =
      (static_cast<uint32_t>(data[0]) << 24u) |
      (static_cast<uint32_t>(data[1]) << 16u) |
      (static_cast<uint32_t>(data[2]) << 8u) |
      static_cast<uint32_t>(data[3]);
  if(suspect_magic == 0x424F4F54 || suspect_magic == 0x4A4F4C49 || suspect_magic == 0x504F5753)
    return false;
  bool has_nearest = false;
  for(size_t i = 0; i + 3 < data.size(); i += 2) {
    uint16_t w = static_cast<uint16_t>(
        (static_cast<uint16_t>(data[i]) << 8u) |
        static_cast<uint16_t>(data[i + 1]));
    if(w == 0x4E71)
      has_nearest = true;
  }
  return has_nearest;
}

RomInfo analyze_rom_header(std::span<const uint8_t> data, uint32_t base_address) {
  RomInfo info = {};
  info.base_address = base_address;
  info.size = static_cast<uint32_t>(data.size());
  info.crc32 = compute_crc32(data);
  info.sha256 = compute_sha256(data);
  if(data.size() >= 32) {
    if(data[0] == 0x4E && data[1] == 0xF9 && data[2] == 0x00 && data[3] == 0x20)
      info.machine_family = "68K";
    else if(detect_rom_type(data))
      info.machine_family = "68K";
  }
  if(data.size() >= 4) {
    char sigstr[5] = {
      static_cast<char>(data[0]),
      static_cast<char>(data[1]),
      static_cast<char>(data[2]),
      static_cast<char>(data[3]),
      0};
    if(std::isprint(static_cast<unsigned char>(data[0])) &&
       std::isprint(static_cast<unsigned char>(data[1])))
      info.model_names.push_back(sigstr);
  }
  return info;
}

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM

static std::string annotate_68k_line(const std::string& line, bool /* is_mac_environment */ = true) {
  std::string opcodeBytes;
  std::string mnemonic;
  std::string operands;
  if(!parse_disassembly_line(line, opcodeBytes, mnemonic, operands))
    return line;

  if(mnemonic == "syscall") {
    uint16_t op = first_opcode_word(opcodeBytes);
    uint16_t trapNumber = 0;
    bool autoPop = false;
    if((op & 0x0800u) != 0) {
      trapNumber = static_cast<uint16_t>(op & 0x0BFFu);
      autoPop = (op & 0x0400u) != 0;
    } else {
      trapNumber = static_cast<uint16_t>(op & 0x00FFu);
    }
    const char* trapSpace = trapNumber >= 0x0800u ? "Toolbox" : "OS";
    std::string trapName = operands;
    if(const char* name = ResourceDasmTrapName(trapNumber, 0))
      trapName = name;
    char suffix[160] = {};
    snprintf(suffix, sizeof(suffix), " ; %s trap %s ($A%03X%s)",
             trapSpace, trapName.c_str(), trapNumber, autoPop ? ", auto-pop" : "");
    return line + suffix;
  }
  if(mnemonic == "jsr" || mnemonic == "jmp" || mnemonic == "bsr") {
    if(operands.rfind("[PC ", 0) == 0)
      return line + " ; internal call";
    if(operands.rfind("[A", 0) == 0 || operands.rfind("[D", 0) == 0)
      return line + " ; indirect (possible extension exit)";
    return line + " ; control transfer";
  }
  if(mnemonic == "rts")
    return line + " ; return from subroutine";
  if(mnemonic == "nop" && operands.empty())
    return line + " ; padding or sync";
  if(mnemonic == "illegal")
    return line + " ; illegal instruction (padding or crash)";
  return line;
}

#endif

std::string export_disassembly(const RomAnalysis& analysis, bool /* annotate_traps */) {
  std::string result;
  char header[256];
  snprintf(header, sizeof(header),
           "; ROM Disassembly: %s\n"
           "; Size: %u bytes\n"
           "; CRC32: %08X\n"
           "; SHA256: %s\n"
           "; Machine: %s\n"
           "; Entry Point: %08X\n"
           "; Instructions: %zu\n\n",
           analysis.info.filename.c_str(),
           static_cast<unsigned>(analysis.info.size),
           static_cast<unsigned>(analysis.info.crc32),
           analysis.info.sha256.c_str(),
           analysis.info.machine_family.c_str(),
           static_cast<unsigned>(analysis.entry_point),
           analysis.total_instructions);
  result += header;
  for(const auto& region : analysis.code_regions) {
    char region_header[128];
    snprintf(region_header, sizeof(region_header),
             "; --- Region %08X-%08X (%.0f%% confidence) ---\n\n",
             static_cast<unsigned>(region.start_address),
             static_cast<unsigned>(region.end_address),
             region.confidence * 100);
    result += region_header;
    result += region.text;
    result += "\n\n";
  }
  if(!analysis.strings.empty()) {
    result += "; --- Strings ---\n";
    for(const auto& s : analysis.strings) {
      char str_line[256];
      if(s.is_pascal)
        snprintf(str_line, sizeof(str_line), "%08X: pstring \"%s\" (%zu bytes)\n",
                 static_cast<unsigned>(s.address), s.value.c_str(), s.length);
      else
        snprintf(str_line, sizeof(str_line), "%08X: cstring \"%s\" (%zu bytes)\n",
                 static_cast<unsigned>(s.address), s.value.c_str(), s.length);
      result += str_line;
    }
  }
  return result;
}

std::vector<StringRegion> scan_strings(
    std::span<const uint8_t> data,
    size_t min_length,
    bool scan_ascii,
    bool scan_pascal) {
  std::vector<StringRegion> result;

  if(scan_ascii) {
    size_t ascii_start = 0;
    size_t ascii_len = 0;
    for(size_t i = 0; i < data.size(); i++) {
      uint8_t byte = data[i];
      bool is_printable = (byte >= 32 && byte <= 126);
      if(is_printable) {
        if(ascii_start == 0 && i > 0)
          ascii_start = i;
        ascii_len++;
      } else {
        if(ascii_len >= min_length) {
          std::string value(reinterpret_cast<const char*>(data.data() + ascii_start), ascii_len);
          result.push_back({static_cast<uint32_t>(ascii_start), value, ascii_len, false});
        }
        ascii_start = 0;
        ascii_len = 0;
      }
    }
    if(ascii_len >= min_length) {
      std::string value(reinterpret_cast<const char*>(data.data() + ascii_start), ascii_len);
      result.push_back({static_cast<uint32_t>(ascii_start), value, ascii_len, false});
    }
  }

  if(scan_pascal) {
    for(size_t i = 0; i < data.size(); i++) {
      uint8_t len = data[i];
      if(len == 0 || len > 255)
        continue;
      if(i + 1 + len > data.size())
        continue;
      bool all_printable = true;
      for(size_t j = 1; j <= len; j++) {
        uint8_t b = data[i + j];
        if(b < 32 || b > 126) {
          all_printable = false;
          break;
        }
      }
      if(all_printable && len >= min_length) {
        std::string value(reinterpret_cast<const char*>(data.data() + i + 1), len);
        result.push_back({static_cast<uint32_t>(i), value, len, true});
        i += len;
      }
    }
  }

  std::sort(result.begin(), result.end(),
            [](const StringRegion& a, const StringRegion& b) {
              return a.address < b.address;
            });
  return result;
}

RomAnalysis disassemble_rom(
    std::span<const uint8_t> data,
    const RomInfo& info,
    uint32_t start_address,
    size_t page_size) {
  RomAnalysis analysis;
  analysis.info = info;
  analysis.entry_point = start_address;
  (void)page_size;

#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
  std::string raw_dasm;
  DisassembleMac68kRomWithResourceDasm(data.data(), data.size(), start_address, page_size, raw_dasm);
  std::string annotated;
  size_t start = 0;
  while(start < raw_dasm.size()) {
    size_t end = raw_dasm.find('\n', start);
    std::string line = raw_dasm.substr(start, (end != std::string::npos) ? end - start : raw_dasm.size() - start);
    annotated += annotate_68k_line(line);
    if(end != std::string::npos)
      annotated += '\n';
    start = (end != std::string::npos) ? end + 1 : raw_dasm.size();
  }
  DisassemblyRegion region;
  region.start_address = start_address;
  region.end_address = start_address + static_cast<uint32_t>(data.size()) - 1;
  region.text = annotated;
  region.is_code = true;
  region.confidence = 0.95;
  analysis.code_regions.push_back(region);
  size_t instr_count = 0;
  for(char c : annotated) {
    if(c == '\n')
      instr_count++;
  }
  analysis.total_instructions = instr_count / 2;
#else
  DisassemblyRegion region;
  region.start_address = start_address;
  region.end_address = start_address + static_cast<uint32_t>(data.size()) - 1;
  region.is_code = true;
  region.confidence = 0.3;
  char line_buf[64];
  for(size_t offset = 0; offset + 1 < data.size(); offset += 2) {
    uint32_t addr = start_address + static_cast<uint32_t>(offset);
    uint16_t word = static_cast<uint16_t>(
        (static_cast<uint16_t>(data[offset]) << 8) |
        static_cast<uint16_t>(data[offset + 1]));
    snprintf(line_buf, sizeof(line_buf), "%08X: dc.w $%04X\n", addr, word);
    region.text += line_buf;
  }
  analysis.code_regions.push_back(region);
  analysis.total_instructions = data.size() / 2;
#endif

  analysis.strings = scan_strings(data, 4, true, true);
  return analysis;
}

void classify_rom_structure(
    RomAnalysis& analysis,
    std::span<const uint8_t> data,
    uint32_t base_address) {
  if(data.empty())
    return;

  const uint32_t end_address = base_address + static_cast<uint32_t>(data.size());
  std::unordered_map<uint32_t, size_t> calls;
  std::unordered_map<uint32_t, size_t> jumps;
  std::unordered_map<uint32_t, std::set<uint32_t>> refs;
  std::map<uint32_t, std::string> nearest_labels;

  for(const auto& region : analysis.code_regions) {
    size_t pos = 0;
    std::string current_label;
    while(pos < region.text.size()) {
      const size_t end = region.text.find('\n', pos);
      const std::string line = region.text.substr(pos, (end != std::string::npos) ? end - pos : region.text.size() - pos);
      if(!line.empty() && line.back() == ':') {
        current_label = line.substr(0, line.size() - 1);
      }

      uint32_t address = 0;
      std::string mnemonic;
      std::string operands;
      if(parse_disassembly_text_line(line, address, mnemonic, operands)) {
        if(!current_label.empty())
          nearest_labels.emplace(address, current_label);
        uint32_t target = 0;
        if(extract_comment_target(operands, target) && target >= base_address && target < end_address) {
          refs[target].emplace(address);
          if(mnemonic == "bsr" || mnemonic == "jsr")
            calls[target]++;
          else if(mnemonic == "bra" || mnemonic == "jmp" || mnemonic.rfind("b", 0) == 0)
            jumps[target]++;
        }
      }

      pos = (end != std::string::npos) ? end + 1 : region.text.size();
    }
  }

  std::vector<FunctionCandidate> function_candidates;
  for(const auto& it : refs) {
    const uint32_t address = it.first;
    const size_t call_count = calls[address];
    const size_t jump_count = jumps[address];
    const size_t ref_count = it.second.size();
    const double score = static_cast<double>((call_count * 3) + jump_count + std::min<size_t>(ref_count, 8));
    if(call_count == 0 && score < 3.0)
      continue;
    FunctionCandidate candidate;
    candidate.address = address;
    candidate.calls = call_count;
    candidate.jumps = jump_count;
    candidate.references = ref_count;
    candidate.confidence = std::min(0.95, 0.30 + (score / 20.0));
    auto label_it = nearest_labels.upper_bound(address);
    if(label_it != nearest_labels.begin()) {
      --label_it;
      candidate.label = label_it->second;
    }
    function_candidates.push_back(candidate);
  }

  std::sort(function_candidates.begin(), function_candidates.end(),
            [](const FunctionCandidate& a, const FunctionCandidate& b) {
              const double score_a = (static_cast<double>(a.calls) * 3.0) + static_cast<double>(a.jumps) + static_cast<double>(a.references);
              const double score_b = (static_cast<double>(b.calls) * 3.0) + static_cast<double>(b.jumps) + static_cast<double>(b.references);
              if(score_a != score_b)
                return score_a > score_b;
              return a.address < b.address;
            });
  analysis.function_candidates = std::move(function_candidates);

  for(size_t offset = 0; offset + 16 <= data.size();) {
    std::vector<uint32_t> targets;
    size_t cursor = offset;
    while(cursor + 4 <= data.size()) {
      const uint32_t value =
          (static_cast<uint32_t>(data[cursor]) << 24u) |
          (static_cast<uint32_t>(data[cursor + 1]) << 16u) |
          (static_cast<uint32_t>(data[cursor + 2]) << 8u) |
          static_cast<uint32_t>(data[cursor + 3]);
      if(value < base_address || value >= end_address || (value & 1u))
        break;
      targets.push_back(value);
      cursor += 4;
    }
    if(targets.size() >= 4) {
      PointerTableRegion region;
      region.address = base_address + static_cast<uint32_t>(offset);
      region.entry_count = targets.size();
      if(targets.size() > 64)
        targets.resize(64);
      region.targets = std::move(targets);
      analysis.pointer_table_regions.push_back(std::move(region));
      offset = cursor;
    } else {
      offset += 2;
    }
  }

  if(!analysis.strings.empty()) {
    uint32_t start = 0;
    uint32_t end = 0;
    size_t count = 0;
    bool active = false;
    for(const auto& string_region : analysis.strings) {
      const uint32_t address = base_address + string_region.address;
      const uint32_t string_end = address + static_cast<uint32_t>(string_region.length);
      if(!active || address > end + 256) {
        if(active && count >= 3) {
          analysis.data_regions.push_back({start, end, "string_cluster", count, 0.70});
        }
        start = address;
        end = string_end;
        count = 1;
        active = true;
      } else {
        end = std::max(end, string_end);
        count++;
      }
    }
    if(active && count >= 3)
      analysis.data_regions.push_back({start, end, "string_cluster", count, 0.70});
  }

  const std::vector<std::string> marker_types = {
      "DRVR", "ndrv", "decl", "boot", "CODE", "PACK", "STR#", "STR ", "MENU", "ALRT", "DITL", "cfrg", "ptch", "rsrc"};
  for(const auto& type : marker_types) {
    if(type.empty() || type.size() > data.size())
      continue;
    for(size_t offset = 0; offset + type.size() <= data.size(); offset++) {
      bool match = true;
      for(size_t i = 0; i < type.size(); i++) {
        if(data[offset + i] != static_cast<uint8_t>(type[i])) {
          match = false;
          break;
        }
      }
      if(match) {
        ResourceMarker marker;
        marker.address = base_address + static_cast<uint32_t>(offset);
        marker.type = type;
        marker.context = printable_context(data, offset);
        analysis.resource_markers.push_back(std::move(marker));
        if(analysis.resource_markers.size() >= 1000)
          return;
      }
    }
  }
}

RomAnalysis scan_rom(
    std::span<const uint8_t> data,
    const RomInfo& info,
    const ScanOptions& options) {
  RomAnalysis analysis;
  analysis.info = info;
  analysis.entry_point = options.start_address;
  if(data.empty())
    return analysis;
  size_t scan_size = options.max_size > 0 ? std::min(options.max_size, data.size()) : data.size();
  std::span<const uint8_t> scan_data = data.subspan(0, scan_size);
  analysis.strings = scan_strings(scan_data, options.min_string_length,
                                   options.scan_ascii, options.scan_pascal);
  if(options.disassemble_code) {
    RomAnalysis dasm_result = disassemble_rom(scan_data, info, options.start_address);
    analysis.code_regions = std::move(dasm_result.code_regions);
    analysis.total_instructions = dasm_result.total_instructions;
  }
  if(options.detect_pointer_tables) {
    for(size_t i = 0; i + 4 < scan_size; i += 4) {
      uint32_t val = (static_cast<uint32_t>(scan_data[i]) << 24) |
                     (static_cast<uint32_t>(scan_data[i + 1]) << 16) |
                     (static_cast<uint32_t>(scan_data[i + 2]) << 8) |
                     static_cast<uint32_t>(scan_data[i + 3]);
      if(val >= options.start_address && val < options.start_address + scan_size) {
        PointerTableEntry entry;
        entry.address = static_cast<uint32_t>(i);
        entry.target = val;
        entry.is_relative = false;
        analysis.pointer_tables.push_back(entry);
      }
    }
  }
  classify_rom_structure(analysis, scan_data, options.start_address);
  return analysis;
}

}}
