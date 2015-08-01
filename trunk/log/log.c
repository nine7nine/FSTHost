#include <stdio.h>
#include <stdarg.h>

#include "log.h"

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
	va_list ap;
	va_start (ap, fmt);
	logprintf( LOG_ERROR, fmt, ap );
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
