#ifndef __log_h__
#define __log_h__

#include <stdint.h>
#include <stdbool.h>

#define LOG_MAX_LINE_LEN 512

typedef enum {
	LOG_NONE,
	LOG_ERROR,
	LOG_INFO,
	LOG_DEBUG
} LogLevel;

typedef void (*LogCallback)(const char* msg, void* arg);

void log_init( LogLevel severity, LogCallback callback, void* arg );
void log_error ( const char* fmt, ... );
void log_info ( const char* fmt, ... );
void log_debug ( const char* fmt, ... );

#endif /* __log_h__ */
