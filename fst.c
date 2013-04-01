#include <stdio.h>
#include <stdarg.h>

#include "fst.h"

void fst_error (const char *fmt, ...) {
	va_list ap;
	char buffer[512];

	va_start (ap, fmt);
	vsnprintf (buffer, sizeof(buffer), fmt, ap);
	fst_error_callback (buffer);
	va_end (ap);
}

void default_fst_error_callback (const char *desc) {
	fprintf(stderr, "%s\n", desc);
}

void (*fst_error_callback)(const char *desc) = &default_fst_error_callback;

// most simple one :) could be sufficient.... 
intptr_t VSTCALLBACK
simple_master_callback( struct AEffect *fx, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt )
{
	printf("AMC: %d\n", opcode);
	if ( opcode == audioMasterVersion ) return 2;
	return 0;
}
