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

const static uint8_t SYSEX_IDENT_RQST[6] = { 
	SYSEX_BEGIN,
	SYSEX_NON_REALTIME,
	0,
	SYSEX_GENERAL_INFORMATION,
	SYSEX_IDENTITY_REQUEST,
	SYSEX_END
};

typedef struct _SysExIdentReply {
	uint8_t begin;
	uint8_t type;
       	uint8_t target_id;
	uint8_t gi;
	uint8_t ir;
	uint8_t id;
	uint8_t family[2];
	uint8_t model[2];
	uint8_t version[4];
	uint8_t end;
} SysExIdentReply;

enum SysExState {
	SYSEX_STATE_NOACTIVE = 0,
	SYSEX_STATE_ACTIVE   = 1
};

typedef struct _SysExDumpV1 {
	uint8_t begin;
	uint8_t id;
	uint8_t version;
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
extern SysExIdentReply* sysex_ident_reply(uint8_t uuid);
extern SysExDumpV1* sysex_dump_v1(
	uint8_t uuid,
	uint8_t program, 
	uint8_t channel,
	uint8_t volume,
	enum SysExState state,
	char* program_name,
	char* plugin_name
);

#endif /* __sysex_h__ */
