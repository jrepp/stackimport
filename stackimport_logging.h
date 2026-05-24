#pragma once

#include "stackimport_c.h"

#ifdef __GNUC__
#define STACKIMPORT_PRINTF_FORMAT(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define STACKIMPORT_PRINTF_FORMAT(fmt_index, first_arg)
#endif

void stackimport_logging_init();
void stackimport_logging_flush();
void stackimport_logging_shutdown();

void stackimport_quill_log_message(uint32_t severity, const char* message);
void stackimport_quill_logf(uint32_t severity, const char* format, ...)
	STACKIMPORT_PRINTF_FORMAT(2, 3);
void stackimport_quill_diagnosticf(const char* format, ...)
	STACKIMPORT_PRINTF_FORMAT(1, 2);

void stackimport_emit_infof(const char* format, ...)
	STACKIMPORT_PRINTF_FORMAT(1, 2);
void stackimport_emit_diagnosticf(const char* format, ...)
	STACKIMPORT_PRINTF_FORMAT(1, 2);

#undef STACKIMPORT_PRINTF_FORMAT
