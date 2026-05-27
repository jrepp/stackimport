#include "StackImportCli.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#if defined(_WIN32) && !defined(PATH_MAX)
#define PATH_MAX _MAX_PATH
#endif

namespace stackimport::cli {

std::string json_escape(const std::string& text)
{
	std::string result;
	result.reserve(text.size() + 8);
	for(char ch : text)
	{
		switch(ch)
		{
			case '\\': result += "\\\\"; break;
			case '"': result += "\\\""; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default:
				if(static_cast<unsigned char>(ch) < 0x20)
				{
					char escaped[8] = {};
					snprintf(escaped, sizeof(escaped), "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(ch)));
					result += escaped;
				}
				else
				{
					result.push_back(ch);
				}
				break;
		}
	}
	return result;
}

bool read_entire_file(const std::string& path, std::vector<uint8_t>& out)
{
	out.clear();
	FILE* file = fopen(path.c_str(), "rb");
	if(!file)
		return false;
	if(fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return false;
	}
	const long fileSize = ftell(file);
	if(fileSize < 0 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return false;
	}
	out.resize(static_cast<size_t>(fileSize));
	const bool ok = fileSize == 0 || fread(out.data(), 1, out.size(), file) == out.size();
	const int closeStatus = fclose(file);
	return ok && closeStatus == 0;
}

bool write_text_file(const std::string& path, const std::string& text)
{
	FILE* file = fopen(path.c_str(), "wb");
	if(!file)
		return false;
	const bool ok = text.empty() || fwrite(text.data(), 1, text.size(), file) == text.size();
	const int closeStatus = fclose(file);
	return ok && closeStatus == 0;
}

std::string path_join(const std::string& dir, const std::string& name)
{
	if(dir.empty())
		return name;
	const char last = dir[dir.size() - 1];
	if(last == '/'
#if defined(_WIN32)
		|| last == '\\'
#endif
		)
		return dir + name;
#if defined(_WIN32)
	return dir + "\\" + name;
#else
	return dir + "/" + name;
#endif
}

namespace {

int make_directory(const char* path)
{
#if defined(_WIN32)
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}

bool make_directory_if_needed(const std::string& path)
{
	if(path.empty())
		return false;
	if(make_directory(path.c_str()) == 0)
		return true;
	return errno == EEXIST;
}

}

bool make_directories_recursive(const std::string& path)
{
	if(path.empty())
		return false;
	std::string partial;
	size_t start = 0;
#if defined(_WIN32)
	if(path.size() >= 2 && path[1] == ':')
	{
		partial = path.substr(0, 2);
		start = 2;
	}
#endif
	if(!path.empty() && (path[0] == '/'
#if defined(_WIN32)
		|| path[0] == '\\'
#endif
		))
	{
		partial = path.substr(0, 1);
		start = 1;
	}
	while(start < path.size())
	{
		const size_t slash = path.find_first_of("/\\", start);
		const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if(!part.empty())
		{
			partial = path_join(partial, part);
			if(!make_directory_if_needed(partial))
				return false;
		}
		if(slash == std::string::npos)
			break;
		start = slash + 1;
	}
	return make_directory_if_needed(path);
}

std::string current_utc_timestamp()
{
	std::time_t now = std::time(nullptr);
	std::tm tm_value = {};
#if defined(_WIN32)
	gmtime_s(&tm_value, &now);
#else
	gmtime_r(&now, &tm_value);
#endif
	char buffer[32] = {};
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_value);
	return buffer;
}

std::string absolute_path(const char* path)
{
	std::string result(path ? path : "");
	if(!result.empty() && (result[0] == '/'
#if defined(_WIN32)
		|| result[0] == '\\' || (result.size() > 1 && result[1] == ':')
#endif
		))
		return result;

	char cwd[PATH_MAX + 1] = {0};
#if defined(_WIN32)
	if(!_getcwd(cwd, sizeof(cwd)))
#else
	if(!getcwd(cwd, sizeof(cwd)))
#endif
		return std::string();

	std::string fullpath = cwd;
	if(!fullpath.empty() && fullpath[fullpath.size() - 1] != '/'
#if defined(_WIN32)
		&& fullpath[fullpath.size() - 1] != '\\'
#endif
		)
#if defined(_WIN32)
		fullpath += '\\';
#else
		fullpath += '/';
#endif
	fullpath += result;
	return fullpath;
}

std::string relative_to_cwd(const std::string& path)
{
	char cwd[PATH_MAX + 1] = {0};
#if defined(_WIN32)
	if(!_getcwd(cwd, sizeof(cwd)))
#else
	if(!getcwd(cwd, sizeof(cwd)))
#endif
		return path;
	std::string root = cwd;
	if(!root.empty() && root[root.size() - 1] != '/'
#if defined(_WIN32)
		&& root[root.size() - 1] != '\\'
#endif
		)
#if defined(_WIN32)
		root += '\\';
#else
		root += '/';
#endif
	if(path.rfind(root, 0) == 0)
		return path.substr(root.size());
	return path;
}

std::string default_output_package_path(const std::string& input_path)
{
	std::string package_path(input_path);
	const std::string::size_type slash = package_path.find_last_of("/\\");
	const std::string::size_type dot = package_path.find_last_of('.');
	if(dot != std::string::npos && (slash == std::string::npos || dot > slash))
	{
		package_path.resize(dot);
	}
	package_path.append(".xstk");
	return package_path;
}

std::string tsv_escape(const std::string& text)
{
	std::string result;
	result.reserve(text.size());
	for(char ch : text)
	{
		if(ch == '\t' || ch == '\r' || ch == '\n')
			result.push_back(' ');
		else
			result.push_back(ch);
	}
	return result;
}

std::string rom_id_from_info(const RomDasm::RomAnalysis& analysis)
{
	return "rom-" + analysis.info.sha256.substr(0, 12);
}

const char* rom_disassembly_mode()
{
#if defined(STACKIMPORT_HAS_RESOURCE_DASM) && STACKIMPORT_HAS_RESOURCE_DASM
	return "m68k";
#else
	return "fallback_words";
#endif
}

const char* initial_bytes_hypothesis(const std::vector<uint8_t>& buf, double& confidence)
{
	confidence = 0.20;
	if(buf.size() >= 4 && buf[0] == 0x4E && buf[1] == 0xF9)
	{
		confidence = 0.80;
		return "code";
	}
	if(buf.size() >= 4 &&
		((buf[0] >= 32 && buf[0] <= 126) || buf[0] == 0) &&
		((buf[1] >= 32 && buf[1] <= 126) || buf[1] == 0))
	{
		confidence = 0.45;
		return "header";
	}
	return "mixed";
}

int run_selected_mode(const Options& options)
{
	switch(options.mode)
	{
		case Mode::Scan:
			return run_scan_mode(options);
		case Mode::Rom:
			return run_rom_mode(options);
		case Mode::Import:
			return run_import_mode(options);
	}
	return 5;
}

} // namespace stackimport::cli
