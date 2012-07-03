/*
   Sysex praser - part of FSTHost
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#include "sysex.h"

void
sysex_makeASCII(uint8_t* ascii_midi_dest, char* name, size_t size_dest)
{
	size_t i;
	for (i=0; i < strlen(name) && i < size_dest - 1; i++) {
		if ( ! isprint( toascii( name[i]) ) )
			continue;

		ascii_midi_dest[i] = name[i];

	}

	/* Set rest to 0 */
	memset(ascii_midi_dest + i, 0, size_dest - i - 1);
}

// Send SysEx
SysExDumpV1*
sysex_dump_v1_new() {
	SysExDumpV1* sysex = calloc(1, sizeof(SysExDumpV1));

	sysex->begin   = SYSEX_BEGIN;
	sysex->id      = SYSEX_MYID;
	sysex->version = SYSEX_VERSION;
	sysex->type    = SYSEX_TYPE_DUMP;
	sysex->end     = SYSEX_END;

	return sysex;
}

SysExDumpRequestV1*
sysex_dump_request_v1_new()
{
	SysExDumpRequestV1* sysex = calloc(1, sizeof(SysExDumpV1));

	sysex->begin   = SYSEX_BEGIN;
	sysex->id      = SYSEX_MYID;
	sysex->version = SYSEX_VERSION;
	sysex->type    = SYSEX_TYPE_RQST;
	sysex->end     = SYSEX_END;

	return sysex;
}

SysExIdentRqst*
sysex_ident_request_new() {
	SysExIdentRqst* sysex = calloc(1, sizeof(SysExIdentRqst));

	sysex->begin	= SYSEX_BEGIN;
	sysex->type	= SYSEX_NON_REALTIME;
	sysex->target_id = 0x7F; // All Devices
	sysex->gi	= SYSEX_GENERAL_INFORMATION;
	sysex->ir	= SYSEX_IDENTITY_REQUEST;
	sysex->end	= SYSEX_END;

	return sysex;
}

SysExIdentReply*
sysex_ident_reply_new()
{
	SysExIdentReply* sysex = calloc(1, sizeof(SysExIdentReply));

	sysex->begin	= SYSEX_BEGIN;
	sysex->type	= SYSEX_NON_REALTIME;
	sysex->target_id = 0x7F; // All Devices
	sysex->gi	= SYSEX_GENERAL_INFORMATION;
	sysex->ir	= SYSEX_IDENTITY_REPLY;
	sysex->id	= SYSEX_MYID;
	// sysex->family - not used now
	//sysex->model[1]	= uuid;
	sysex->version[3] = SYSEX_VERSION;
	sysex->end	= SYSEX_END;

	return sysex;
}
