#ifndef __sysex_h__
#define __sysex_h__

/*
  SysEx parser - part of FST host
*/

#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define SYSEX_BEGIN 0xF0
#define SYSEX_END 0xF7
#define SYSEX_NON_REALTIME 0x7E
#define SYSEX_GENERAL_INFORMATION 0x06
#define SYSEX_IDENTITY_REQUEST 0x01
#define SYSEX_IDENTITY_REPLY 0x02
#define SYSEX_MYID 0x5B
#define SYSEX_VERSION 1
#define SYSEX_TYPE_DUMP 0
#define SYSEX_TYPE_RQST 1

#define SYSEX_IDENT_REQUEST {SYSEX_BEGIN,SYSEX_NON_REALTIME,0x7F,SYSEX_GENERAL_INFORMATION,SYSEX_IDENTITY_REQUEST,SYSEX_END}
typedef struct _SysExIdentRqst {
	const uint8_t begin;
	const uint8_t type;
       	const uint8_t target_id;
	const uint8_t gi;
	const uint8_t ir;
	const uint8_t end;
} SysExIdentRqst;

#define SYSEX_IDENT_REPLY {SYSEX_BEGIN,SYSEX_NON_REALTIME,0x7F,SYSEX_GENERAL_INFORMATION,SYSEX_IDENTITY_REPLY,\
   SYSEX_MYID,{0},{0},SYSEX_VERSION,SYSEX_END}
typedef struct _SysExIdentReply {
	const uint8_t begin;
	const uint8_t type;
       	const uint8_t target_id;
	const uint8_t gi;
	const uint8_t ir;
	const uint8_t id;
	const uint8_t family[2];
	uint8_t model[2]; // Here we set sysex_uuid as [1]
	const uint8_t version[4];
	const uint8_t end;
} SysExIdentReply;

#define SYSEX_DUMP_REQUEST {SYSEX_BEGIN,SYSEX_MYID,SYSEX_VERSION,SYSEX_TYPE_RQST,0,SYSEX_END}
typedef struct _SysExDumpRequestV1 {
	const uint8_t begin;
	const uint8_t id;
	const uint8_t version;
	const uint8_t type;
	uint8_t uuid;
	const uint8_t end;
} SysExDumpRequestV1;

enum SysExState {
	SYSEX_STATE_NOACTIVE = 0,
	SYSEX_STATE_ACTIVE   = 1
};

#define SYSEX_DUMP {SYSEX_BEGIN,SYSEX_MYID,SYSEX_VERSION,SYSEX_TYPE_DUMP,0,0,0,0,0,{0},{0},SYSEX_END}
typedef struct _SysExDumpV1 {
	const uint8_t begin;
	const uint8_t id;
	const uint8_t version;
	const uint8_t type;
	uint8_t uuid;
	uint8_t state;
	uint8_t program;
	uint8_t channel;
	uint8_t volume;
	uint8_t program_name[24]; /* Last is always 0 */
	uint8_t plugin_name[24]; /* Last is always 0 */
	const uint8_t end;
} SysExDumpV1;

static void
sysex_makeASCII(uint8_t* ascii_midi_dest, char* name, size_t size_dest) {
	size_t i;
	for (i=0; i < strlen(name) && i < size_dest - 1; i++) {
		if ( ! isprint( toascii( name[i]) ) )
			continue;

		ascii_midi_dest[i] = name[i];
	}

	/* Set rest to 0 */
	memset(ascii_midi_dest + i, 0, size_dest - i - 1);
}

#endif /* __sysex_h__ */
