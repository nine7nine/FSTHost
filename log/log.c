#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "log.h"

#define ERROR_STRING "ERROR: "

static LogSeverity log_severity = LOG_ERROR;
static void* user_ptr = NULL;

static void default_log_callback (const char *msg, void* arg) {
	fprintf(stderr, "%s\n", msg);
}

static LogCallback log_callback = &default_log_callback;

static void logprintf (LogSeverity severity, const char *fmt, va_list ap ) {
	if ( severity > log_severity ) return;

	char buffer[LOG_MAX_LINE_LEN];
	vsnprintf (buffer, sizeof buffer, fmt, ap);
	log_callback (buffer, user_ptr);
}

void log_error ( const char* fmt, ... ) {
	size_t fmt_size = strlen(fmt);
	size_t err_size = strlen(ERROR_STRING);
	char new_fmt[fmt_size + err_size];
	sprintf( new_fmt, "%s%s", ERROR_STRING, fmt );

	va_list ap;
	va_start (ap, fmt);
	logprintf( LOG_ERROR, new_fmt, ap );
	va_end (ap);
}

void log_info ( const char* fmt, ... ) {
	va_list ap;
	va_start (ap, fmt);
	logprintf( LOG_INFO, fmt, ap );
	va_end (ap);
}

void log_debug ( const char* fmt, ... ) {
	va_list ap;
	va_start (ap, fmt);
	logprintf( LOG_DEBUG, fmt, ap );
	va_end (ap);
}

void log_init( LogSeverity severity, LogCallback callback, void* arg ) {
	log_severity = severity;
	if ( callback != NULL ) log_callback = callback;
	user_ptr = arg;
}
