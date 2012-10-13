#ifndef __sysex_h__
#define __sysex_h__

/*
  SysEx parser - part of FST host
*/

#include <stdint.h>

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

typedef struct _SysExIdentRqst {
	uint8_t begin;
	uint8_t type;
       	uint8_t target_id;
	uint8_t gi;
	uint8_t ir;
	uint8_t end;
} SysExIdentRqst;

typedef struct _SysExIdentReply {
	uint8_t begin;
	uint8_t type;
       	uint8_t target_id;
	uint8_t gi;
	uint8_t ir;
	uint8_t id;
	uint8_t family[2];
	uint8_t model[2]; // Here we set sysex_uuid as [1]
	uint8_t version[4];
	uint8_t end;
} SysExIdentReply;

enum SysExState {
	SYSEX_STATE_NOACTIVE = 0,
	SYSEX_STATE_ACTIVE   = 1
};

typedef struct _SysExDumpRequestV1 {
	uint8_t begin;
	uint8_t id;
	uint8_t version;
	uint8_t type;
	uint8_t uuid;
	uint8_t end;
} SysExDumpRequestV1;

typedef struct _SysExDumpV1 {
	uint8_t begin;
	uint8_t id;
	uint8_t version;
	uint8_t type;
	uint8_t uuid;
	uint8_t state;
	uint8_t program;
	uint8_t channel;
	uint8_t volume;
	uint8_t program_name[24]; /* Last is always 0 */
	uint8_t plugin_name[24]; /* Last is always 0 */
	uint8_t end;
} SysExDumpV1;

/* Prototypes */
SysExDumpV1* sysex_dump_v1_new();
SysExDumpRequestV1* sysex_dump_request_v1_new();
SysExIdentRqst* sysex_ident_request_new();
SysExIdentReply* sysex_ident_reply_new();
void sysex_makeASCII(uint8_t* ascii_midi_dest, char* name, size_t size_dest);

#endif /* __sysex_h__ */
