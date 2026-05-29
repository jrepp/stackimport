#include "StackImportCli.h"

#include "RomDasm.h"
#include "stackimport_logging.h"
#include "stackimport_version.h"
#include "stackimport/mov2qt.hpp"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <span>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <malloc.h>
#elif defined(__APPLE__)
#include <sys/xattr.h>
#endif

namespace stackimport::cli {

namespace {

namespace fs = std::filesystem;

void* cli_allocate(size_t size, size_t alignment, void*)
{
	void* ptr = nullptr;
	if(alignment < sizeof(void*))
		alignment = sizeof(void*);
#if defined(_WIN32)
	ptr = _aligned_malloc(size, alignment);
	return ptr;
#else
	if(posix_memalign(&ptr, alignment, size) != 0)
		return nullptr;
	return ptr;
#endif
}

void cli_deallocate(void* ptr, void*)
{
#if defined(_WIN32)
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

void cli_message(uint32_t severity, const char* message, void*)
{
	stackimport_quill_log_message(severity, message);
}

stackimport_file_handle cli_open_file(const char* path, const char* mode, void*)
{
	return fopen(path, mode);
}

size_t cli_read_file(stackimport_file_handle file, void* data, size_t size, void*)
{
	return fread(data, 1, size, static_cast<FILE*>(file));
}

size_t cli_write_file(stackimport_file_handle file, const void* data, size_t size, void*)
{
	return fwrite(data, 1, size, static_cast<FILE*>(file));
}

int cli_close_file(stackimport_file_handle file, void*)
{
	return fclose(static_cast<FILE*>(file));
}

int cli_make_directory(const char* path, void*)
{
#if defined(_WIN32)
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}

std::string fourcc_string(const char* bytes)
{
	std::string text;
	for(size_t i = 0; i < 4; i++)
	{
		if(bytes[i] == '\0')
			break;
		text.push_back(bytes[i]);
	}
	return text;
}

struct FinderInfo {
	std::string type;
	std::string creator;
};

FinderInfo read_finder_info(const fs::path& path)
{
	FinderInfo info;
#if defined(__APPLE__)
	char bytes[32] = {};
	const ssize_t size = getxattr(path.c_str(), "com.apple.FinderInfo", bytes, sizeof(bytes), 0, 0);
	if(size >= 8)
	{
		info.type = fourcc_string(bytes);
		info.creator = fourcc_string(bytes + 4);
	}
#else
	(void)path;
#endif
	return info;
}

std::string lowercase_ascii(std::string value)
{
	for(char& ch : value)
	{
		if(ch >= 'A' && ch <= 'Z')
			ch = static_cast<char>(ch - 'A' + 'a');
	}
	return value;
}

std::string extension_without_dot(const fs::path& path)
{
	std::string ext = path.extension().string();
	if(!ext.empty() && ext[0] == '.')
		ext.erase(ext.begin());
	return ext;
}

std::string classify_external_asset(const fs::path& path, const FinderInfo& finderInfo, uintmax_t size)
{
	const std::string ext = lowercase_ascii(extension_without_dot(path));
	if(size == 0)
		return "empty_placeholder";
	if(finderInfo.type == "MYag")
		return "myst_resource";
	if(finderInfo.type == "MYqt" || finderInfo.type == "MooV" || finderInfo.type == "Moov" ||
		ext == "mov" || ext == "moov" || ext == "movie")
		return "movie";
	if(finderInfo.type == "PICT" || ext == "pict" || ext == "pic")
		return "picture";
	if(finderInfo.type == "snd " || ext == "snd" || ext == "aif" || ext == "aiff" || ext == "wav")
		return "sound";
	if(finderInfo.type == "TEXT" || ext == "txt" || ext == "text" || ext == "md")
		return "text";
	if(finderInfo.type == "APPL")
		return "application";
	if(finderInfo.type == "INIT")
		return "system_extension";
	if(finderInfo.type == "STAK" || ext == "stak" || ext == "stack" || ext == "stk")
		return "stack_like";
	return "unknown_binary";
}

std::string media_type_for_asset_kind(const std::string& kind)
{
	if(kind == "movie")
		return "video/quicktime";
	if(kind == "picture")
		return "image/x-pict";
	if(kind == "sound")
		return "audio/x-mac-sound";
	if(kind == "text")
		return "text/plain";
	if(kind == "application")
		return "application/x-mac-application";
	return "application/octet-stream";
}

bool path_has_xstk_component(const fs::path& path)
{
	for(const fs::path& part : path)
	{
		const std::string text = part.string();
		if(text.size() >= 5 && text.substr(text.size() - 5) == ".xstk")
			return true;
	}
	return false;
}

bool path_has_ignored_asset_component(const fs::path& path)
{
	for(const fs::path& part : path)
	{
		const std::string text = part.string();
		if(text == ".git" || text == "__pycache__" || text == "scripts")
			return true;
	}
	return false;
}

bool same_path(const fs::path& lhs, const fs::path& rhs)
{
	std::error_code ec;
	if(fs::equivalent(lhs, rhs, ec))
		return true;
	return fs::absolute(lhs).lexically_normal() == fs::absolute(rhs).lexically_normal();
}

bool is_inside_path(const fs::path& path, const fs::path& root)
{
	const fs::path normalizedPath = fs::absolute(path).lexically_normal();
	const fs::path normalizedRoot = fs::absolute(root).lexically_normal();
	auto pathIt = normalizedPath.begin();
	for(auto rootIt = normalizedRoot.begin(); rootIt != normalizedRoot.end(); ++rootIt, ++pathIt)
	{
		if(pathIt == normalizedPath.end() || *pathIt != *rootIt)
			return false;
	}
	return true;
}

std::string relative_path_string(const fs::path& root, const fs::path& path)
{
	std::error_code ec;
	fs::path rel = fs::relative(path, root, ec);
	if(ec)
		rel = path.filename();
	return rel.generic_string();
}

std::string sha256_for_file(const fs::path& path)
{
	std::vector<uint8_t> bytes;
	if(!read_entire_file(path.string(), bytes))
		return std::string();
	return RomDasm::compute_sha256(std::span<const uint8_t>(bytes.data(), bytes.size()));
}

bool copy_asset_file(const fs::path& source, const fs::path& destination)
{
	std::error_code ec;
	fs::create_directories(destination.parent_path(), ec);
	if(ec)
		return false;
	fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
	return !ec;
}

bool read_resource_fork(const fs::path& source, std::vector<uint8_t>& out)
{
#if defined(__APPLE__)
	return read_entire_file((source / "..namedfork" / "rsrc").string(), out);
#else
	(void)source;
	out.clear();
	return false;
#endif
}

std::string movie_analysis_package_path(const fs::path& relPath)
{
	fs::path path = fs::path("external-assets") / "quicktime-analysis" / relPath;
	path += ".json";
	return path.generic_string();
}

bool write_movie_analysis(const fs::path& source, const fs::path& packageRoot, const fs::path& relPath, std::string& packagePath)
{
	std::vector<uint8_t> bytes;
	if(!read_entire_file(source.string(), bytes))
		return false;
	std::vector<uint8_t> resourceFork;
	read_resource_fork(source, resourceFork);
	const stackimport::mov2qt::Analysis analysis = stackimport::mov2qt::analyze(
		std::span<const uint8_t>(bytes.data(), bytes.size()),
		std::span<const uint8_t>(resourceFork.data(), resourceFork.size()));
	packagePath = movie_analysis_package_path(relPath);
	const fs::path outputPath = packageRoot / fs::path(packagePath);
	std::error_code ec;
	fs::create_directories(outputPath.parent_path(), ec);
	if(ec)
		return false;
	return write_text_file(outputPath.string(), stackimport::mov2qt::analysis_to_json(analysis, 0) + "\n");
}

bool write_external_assets_manifest(const Options& options, const std::string& outputPath)
{
	if(options.media_root_path.empty())
		return true;

	const fs::path mediaRoot(options.media_root_path);
	const fs::path inputPath(options.input_path);
	const fs::path packageRoot(outputPath);
	std::error_code ec;
	if(!fs::is_directory(mediaRoot, ec))
	{
		stackimport_quill_diagnosticf("Error: --media-root is not a directory: %s\n", options.media_root_path.c_str());
		return false;
	}

	const fs::path assetRoot = packageRoot / "external-assets";
	const fs::path assetFilesRoot = assetRoot / "files";
	fs::create_directories(assetFilesRoot, ec);
	if(ec)
	{
		stackimport_quill_diagnosticf("Error: Could not create external asset directory '%s'.\n", assetFilesRoot.string().c_str());
		return false;
	}

	std::string manifest = "{\n";
	manifest += "  \"format\": \"stackimport.externalAssetsManifest\",\n";
	manifest += "  \"schemaVersion\": 1,\n";
	manifest += "  \"generator\": \"stackimport " STACKIMPORT_VERSION_STRING "\",\n";
	manifest += "  \"sourceStack\": \"" + json_escape(options.input_path) + "\",\n";
	manifest += "  \"mediaRoot\": \"" + json_escape(options.media_root_path) + "\",\n";
	manifest += "  \"assets\": [\n";

	size_t assetCount = 0;
	bool ok = true;
	for(fs::recursive_directory_iterator it(mediaRoot, ec), end; it != end && !ec; it.increment(ec))
	{
		if(ec)
			break;
		if(!it->is_regular_file(ec))
			continue;
		const fs::path source = it->path();
		if(same_path(source, inputPath) || path_has_xstk_component(source) || is_inside_path(source, packageRoot))
			continue;
		if(path_has_ignored_asset_component(fs::relative(source, mediaRoot, ec)))
		{
			ec.clear();
			continue;
		}
		if(source.filename().string() == ".DS_Store")
			continue;

		const FinderInfo finderInfo = read_finder_info(source);
		if(source.parent_path() == inputPath.parent_path() &&
			(finderInfo.type == "STAK" || finderInfo.type == "MYag" || finderInfo.type == "APPL"))
			continue;

		const std::string relPath = relative_path_string(mediaRoot, source);
		const fs::path packagedRelPath = fs::path("external-assets") / "files" / fs::path(relPath);
		const fs::path destination = packageRoot / packagedRelPath;
		if(!copy_asset_file(source, destination))
		{
			stackimport_quill_diagnosticf("Warning: Could not package external asset '%s'.\n", source.string().c_str());
			ok = false;
			continue;
		}

		const uintmax_t size = fs::file_size(source, ec);
		const uintmax_t safeSize = ec ? 0 : size;
		ec.clear();
		const std::string kind = classify_external_asset(source, finderInfo, safeSize);
		std::string quicktimeAnalysisPath;
		if(kind == "movie" && !write_movie_analysis(source, packageRoot, fs::path(relPath), quicktimeAnalysisPath))
			stackimport_quill_diagnosticf("Warning: Could not parse QuickTime atoms for external asset '%s'.\n", source.string().c_str());

		if(assetCount > 0)
			manifest += ",\n";
		manifest += "    {";
		manifest += "\"sourcePath\":\"" + json_escape(source.string()) + "\",";
		manifest += "\"relativePath\":\"" + json_escape(relPath) + "\",";
		manifest += "\"packagePath\":\"" + json_escape(packagedRelPath.generic_string()) + "\",";
		manifest += "\"bytes\":" + std::to_string(safeSize) + ",";
		manifest += "\"sha256\":\"" + sha256_for_file(source) + "\",";
		manifest += "\"finderType\":\"" + json_escape(finderInfo.type) + "\",";
		manifest += "\"creator\":\"" + json_escape(finderInfo.creator) + "\",";
		manifest += "\"extension\":\"" + json_escape(extension_without_dot(source)) + "\",";
		manifest += "\"kind\":\"" + json_escape(kind) + "\",";
		manifest += "\"mediaType\":\"" + json_escape(media_type_for_asset_kind(kind)) + "\"";
		if(!quicktimeAnalysisPath.empty())
			manifest += ",\"quicktimeAnalysis\":\"" + json_escape(quicktimeAnalysisPath) + "\"";
		manifest += "}";
		assetCount++;
	}
	if(ec)
	{
		stackimport_quill_diagnosticf("Error: Failed while walking --media-root '%s': %s.\n", options.media_root_path.c_str(), ec.message().c_str());
		return false;
	}

	manifest += "\n  ],\n";
	manifest += "  \"assetCount\": " + std::to_string(assetCount) + "\n";
	manifest += "}\n";

	if(!write_text_file((assetRoot / "manifest.json").string(), manifest))
	{
		stackimport_quill_diagnosticf("Error: Could not write external assets manifest.\n");
		return false;
	}
	return ok;
}

}

int run_import_mode(const Options& options)
{
	const std::string outputPath = !options.output_path.empty() ?
		options.output_path :
		default_output_package_path(options.input_path);
	if(outputPath.empty())
	{
		stackimport_quill_diagnosticf("Error: Could not resolve output package path.\n");
		return 5;
	}

	stackimport_platform platform = {};
	stackimport_platform_init(&platform);
	platform.allocate = cli_allocate;
	platform.deallocate = cli_deallocate;
	platform.message = cli_message;
	platform.open_file = cli_open_file;
	platform.read_file = cli_read_file;
	platform.write_file = cli_write_file;
	platform.close_file = cli_close_file;
	platform.make_directory = cli_make_directory;

	stackimport_context* context = nullptr;
	stackimport_status status = stackimport_context_create_with_platform(&platform, &context);
	if(status != STACKIMPORT_STATUS_OK)
	{
		stackimport_quill_diagnosticf("Error: Could not create import context: %s.\n", stackimport_status_string(status));
		return 5;
	}

	stackimport_import_options importOptions = {};
	stackimport_import_options_init(&importOptions);
	importOptions.flags = options.flags;
	importOptions.input_path = options.input_path.c_str();
	importOptions.output_package_path = outputPath.c_str();

	status = stackimport_import(context, &importOptions);
	stackimport_context_destroy(context);
	if(status != STACKIMPORT_STATUS_OK)
	{
		stackimport_quill_diagnosticf("Error: Conversion of '%s' incomplete/failed: %s.\n", options.input_path.c_str(), stackimport_status_string(status));
		return 5;
	}
	if(!write_external_assets_manifest(options, outputPath))
		return 5;

	return 0;
}

} // namespace stackimport::cli
