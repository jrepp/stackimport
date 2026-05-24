#include "stackimport_logging.h"

#include "stackimport_platform_internal.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <rang.hpp>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogFunctions.h>
#include <quill/Logger.h>
#include <quill/backend/BackendOptions.h>
#include <quill/core/PatternFormatterOptions.h>
#include <quill/sinks/ConsoleSink.h>

namespace {

std::once_flag gQuillInit;
quill::Logger* gStdoutLogger = nullptr;
quill::Logger* gStderrLogger = nullptr;

bool env_var_is_set(const char* name)
{
	const char* value = std::getenv(name);
	return value && value[0] != '\0';
}

bool env_var_equals(const char* name, const char* expected)
{
	const char* value = std::getenv(name);
	return value && std::strcmp(value, expected) == 0;
}

bool output_supports_color(uint32_t severity)
{
	if(env_var_is_set("FORCE_COLOR"))
		return true;
	if(env_var_is_set("STACKIMPORT_NO_COLOR"))
		return false;
	if(env_var_equals("TERM", "dumb"))
		return false;

	FILE* stream = severity >= STACKIMPORT_MESSAGE_WARNING ? stderr : stdout;
#if defined(_WIN32)
	return _isatty(_fileno(stream)) != 0;
#else
	return isatty(fileno(stream)) != 0;
#endif
}

template<typename... Parts>
std::string styled(Parts&&... parts)
{
	static std::mutex rangMutex;
	std::lock_guard<std::mutex> lock(rangMutex);
	std::ostringstream output;
	rang::setControlMode(rang::control::Force);
	(output << ... << std::forward<Parts>(parts));
	output << rang::style::reset;
	rang::setControlMode(rang::control::Auto);
	return output.str();
}

bool append_colored_quoted_block(std::ostringstream& output, const std::string& message, size_t& cursor)
{
	const size_t quoteStart = message.find('\'', cursor);
	if(quoteStart == std::string::npos)
		return false;
	const size_t quoteEnd = message.find('\'', quoteStart + 1);
	if(quoteEnd == std::string::npos)
		return false;

	output << message.substr(cursor, quoteStart - cursor);
	output << styled(rang::fgB::yellow, rang::style::bold, message.substr(quoteStart, quoteEnd - quoteStart + 1));
	cursor = quoteEnd + 1;
	return true;
}

bool append_colored_id(std::ostringstream& output, const std::string& message, size_t& cursor)
{
	const size_t hash = message.find('#', cursor);
	if(hash == std::string::npos)
		return false;
	size_t end = hash + 1;
	if(end < message.size() && message[end] == '-')
		end++;
	while(end < message.size() && std::isdigit(static_cast<unsigned char>(message[end])))
		end++;
	if(end == hash + 1)
		return false;

	output << message.substr(cursor, hash - cursor);
	output << styled(rang::fgB::magenta, message.substr(hash, end - hash));
	cursor = end;
	return true;
}

std::string colorize_stack_message(uint32_t severity, const char* message)
{
	std::string text = message ? message : "";
	if(text.empty() || !output_supports_color(severity))
		return text;

	std::ostringstream output;
	if(text.rfind("Progress:", 0) == 0)
	{
		output << styled(rang::fgB::blue, rang::style::bold, "Progress:");
		output << styled(rang::fg::cyan, text.substr(9));
		return output.str();
	}

	if(text.rfind("Status:", 0) == 0)
	{
		output << styled(rang::fgB::cyan, rang::style::bold, "Status:");
		size_t cursor = 7;
		append_colored_quoted_block(output, text, cursor);
		append_colored_id(output, text, cursor);
		output << styled(rang::fg::gray, text.substr(cursor));
		return output.str();
	}

	if(text.rfind("Warning:", 0) == 0)
	{
		output << styled(rang::fgB::yellow, rang::style::bold, "Warning:");
		output << text.substr(8);
		return output.str();
	}

	if(text.rfind("Error:", 0) == 0)
	{
		output << styled(rang::fgB::red, rang::style::bold, "Error:");
		output << text.substr(6);
		return output.str();
	}

	if(text.rfind("Fatal:", 0) == 0)
	{
		output << styled(rang::bgB::red, rang::fgB::yellow, rang::style::bold, "Fatal:");
		output << text.substr(6);
		return output.str();
	}

	return text;
}

uint32_t severity_for_message(const char* message)
{
	if(!message)
		return STACKIMPORT_MESSAGE_INFO;
	if(std::strncmp(message, "Fatal:", 6) == 0)
		return STACKIMPORT_MESSAGE_FATAL;
	if(std::strncmp(message, "Error:", 6) == 0)
		return STACKIMPORT_MESSAGE_ERROR;
	if(std::strncmp(message, "Warning:", 8) == 0)
		return STACKIMPORT_MESSAGE_WARNING;
	return STACKIMPORT_MESSAGE_INFO;
}

std::string format_message(const char* format, va_list args)
{
	if(!format)
		return std::string();

	va_list argsCopy;
	va_copy(argsCopy, args);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
	int required = std::vsnprintf(nullptr, 0, format, argsCopy);
	va_end(argsCopy);
	if(required < 0)
		return std::string(format);

	std::vector<char> buffer(static_cast<size_t>(required) + 1);
	std::vsnprintf(buffer.data(), buffer.size(), format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	std::string result(buffer.data(), static_cast<size_t>(required));
	if(!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);
	return result;
}

quill::Logger* make_console_logger(const char* loggerName, const char* sinkName, const char* streamName)
{
	quill::ConsoleSinkConfig config;
	config.set_stream(streamName);
	config.set_colour_mode(quill::ConsoleSinkConfig::ColourMode::Automatic);

	auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>(sinkName, config);
	auto* logger = quill::Frontend::create_or_get_logger(
		loggerName,
		std::move(sink),
		quill::PatternFormatterOptions{"%(message)"});
	logger->set_log_level(quill::LogLevel::TraceL3);
	logger->set_immediate_flush(1);
	return logger;
}

quill::Logger* logger_for_severity(uint32_t severity)
{
	stackimport_logging_init();
	return severity >= STACKIMPORT_MESSAGE_WARNING ? gStderrLogger : gStdoutLogger;
}

}

void stackimport_logging_init()
{
	std::call_once(gQuillInit, [] {
		gStdoutLogger = make_console_logger("stackimport.stdout", "stackimport.stdout.sink", "stdout");
		gStderrLogger = make_console_logger("stackimport.stderr", "stackimport.stderr.sink", "stderr");
		quill::BackendOptions backendOptions;
		backendOptions.check_printable_char = {};
		quill::Backend::start<quill::FrontendOptions>(
			backendOptions,
			quill::SignalHandlerOptions{});
	});
}

void stackimport_logging_flush()
{
	if(gStdoutLogger)
		gStdoutLogger->flush_log();
	if(gStderrLogger)
		gStderrLogger->flush_log();
}

void stackimport_logging_shutdown()
{
	stackimport_logging_flush();
	quill::Backend::stop();
}

void stackimport_quill_log_message(uint32_t severity, const char* message)
{
	std::string colorizedMessage = colorize_stack_message(severity, message);
	quill::Logger* logger = logger_for_severity(severity);
	if(severity >= STACKIMPORT_MESSAGE_FATAL)
		quill::critical(logger, "{}", colorizedMessage);
	else if(severity >= STACKIMPORT_MESSAGE_ERROR)
		quill::error(logger, "{}", colorizedMessage);
	else if(severity >= STACKIMPORT_MESSAGE_WARNING)
		quill::warning(logger, "{}", colorizedMessage);
	else
		quill::info(logger, "{}", colorizedMessage);
}

void stackimport_quill_logf(uint32_t severity, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	std::string message = format_message(format, args);
	va_end(args);
	stackimport_quill_log_message(severity, message.c_str());
}

void stackimport_quill_diagnosticf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	std::string message = format_message(format, args);
	va_end(args);
	stackimport_quill_log_message(severity_for_message(message.c_str()), message.c_str());
}

void stackimport_emit_infof(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	std::string message = format_message(format, args);
	va_end(args);
	stackimport_internal_message(STACKIMPORT_MESSAGE_INFO, message.c_str());
}

void stackimport_emit_diagnosticf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	std::string message = format_message(format, args);
	va_end(args);
	stackimport_internal_message(severity_for_message(message.c_str()), message.c_str());
}
