#include <stdio.h>
#include <string.h>

#include "../serv/serv.h"
#include "jfst.h"

#define ACK "<OK>"
#define NAK "<FAIL>"

/* jfst.c */
extern void jfst_quit(JFST* jfst);

/* cpuusage.c */
extern void CPUusage_init();
extern double CPUusage_getCurrentValue();

static void list_programs ( JFST* jfst, int client_sock ) {
	char progName[FST_MAX_PROG_NAME];

	FST* fst = jfst->fst;
	int32_t i;
	for ( i = 0; i < fst_num_presets(fst); i++ ) {
		fst_get_program_name(fst, i, progName, sizeof(progName));
		serv_send_client_data ( client_sock, progName, strlen(progName) );
        }
}

static void get_program ( JFST* jfst, int client_sock ) {
	char msg[16];
	sprintf( msg, "PROGRAM:%d", jfst->fst->current_program );
	serv_send_client_data ( client_sock, msg, strlen(msg) );
}

static void get_channel ( JFST* jfst, int client_sock ) {
	char msg[16];
	sprintf( msg, "CHANNEL:%d", midi_filter_one_channel_get(&jfst->channel) );
	serv_send_client_data ( client_sock, msg, strlen(msg) );
}

static void cpu_usage ( int client_sock ) {
	char msg[8];
	snprintf( msg, sizeof msg, "%g", CPUusage_getCurrentValue() );
	serv_send_client_data ( client_sock, msg, strlen(msg) );
}

static void news ( JFST* jfst, int client_sock, uint8_t* changes ) {
	char msg[16];

	if ( *changes & CHANGE_BYPASS ) {
		snprintf( msg, sizeof msg, "BYPASS:%d", jfst->bypassed );
		serv_send_client_data ( client_sock, msg, strlen(msg) );
	}

	if ( *changes & CHANGE_EDITOR ) {
		snprintf( msg, sizeof msg, "EDITOR:%d", (bool) jfst->fst->window );
		serv_send_client_data ( client_sock, msg, strlen(msg) );
	}

	if ( *changes & CHANGE_CHANNEL ) {
		uint8_t channel = midi_filter_one_channel_get( &jfst->channel );
		snprintf( msg, sizeof msg, "CHANNEL:%d", channel );
		serv_send_client_data ( client_sock, msg, strlen(msg) );
	}

	if ( *changes & CHANGE_VOLUME ) {
		snprintf( msg, sizeof msg, "VOLUME:%d", jfst_get_volume(jfst) );
		serv_send_client_data ( client_sock, msg, strlen(msg) );
	}

	// TODO
	//if ( *changes & CHANGE_MIDILE ) {

	if ( *changes & CHANGE_PROGRAM ) get_program( jfst, client_sock );

	*changes = 0;
}

enum PROTO_CMD {
	CMD_UNKNOWN,
	CMD_EDITOR,
	CMD_LIST_PROGRAMS,
	CMD_GET_PROGRAM,
	CMD_SET_PROGRAM,
	CMD_GET_CHANNEL,
	CMD_SET_CHANNEL,
	CMD_SUSPEND,
	CMD_RESUME,
	CMD_LOAD,
	CMD_SAVE,
	CMD_NEWS,
	CMD_CPU,
	CMD_HELP,
	CMD_QUIT,
	CMD_KILL
};

struct PROTO_MAP {
	enum PROTO_CMD key;
	const char* name;
};

static struct PROTO_MAP proto_string_map[] = {
	{ CMD_EDITOR, "editor" },
	{ CMD_LIST_PROGRAMS, "list_programs" },
	{ CMD_GET_PROGRAM, "get_program" },
	{ CMD_SET_PROGRAM, "set_program" },
	{ CMD_GET_CHANNEL, "get_channel" },
	{ CMD_SET_CHANNEL, "set_channel" },
	{ CMD_SUSPEND, "suspend" },
	{ CMD_RESUME, "resume" },
	{ CMD_LOAD, "load" },
	{ CMD_SAVE, "save" },
	{ CMD_NEWS, "news" },
	{ CMD_CPU, "cpu" },
	{ CMD_HELP, "help" },
	{ CMD_QUIT, "quit" },
	{ CMD_KILL, "kill" },
	{ CMD_UNKNOWN, NULL }
};

static enum PROTO_CMD proto_lookup ( const char* name ) {
	short i;
	for ( i = 0; proto_string_map[i].key != CMD_UNKNOWN; i++ ) {
		if ( ! strcasecmp( proto_string_map[i].name, name ) )
			return proto_string_map[i].key;
	}
	return CMD_UNKNOWN;
}

static void help( int client_sock ) {
	char msg[256] = "";
	short i;
	for ( i = 0; proto_string_map[i].key != CMD_UNKNOWN; i++ ) {
		strcat( msg, proto_string_map[i].name );
		strcat( msg, " " );
	}
	serv_send_client_data ( client_sock, msg, strlen(msg) );
}

static bool jfst_proto_client_dispatch ( JFST* jfst, char* msg, uint8_t* changes, int client_sock ) {
	printf ( "GOT MSG: %s\n", msg );

	bool ret = true;
	bool ack = true;

	char* value = msg;
	while ( *(++value) != '\0' ) { // at least one character cmd
		if ( *value == ':' || *value == ' ' ) {
			*value = '\0';
			value++;
			break;
		}
	}

	uint8_t all_changes = -1;
	switch ( proto_lookup ( msg ) ) {
	case CMD_EDITOR:
		if ( !strcasecmp(value, "open") ) {
			fst_run_editor ( jfst->fst, false );
		} else if ( !strcasecmp(value,"close") ) {
			fst_call ( jfst->fst, EDITOR_CLOSE );
		} else {
			puts ( "Need value: open|close" );
			ack = false;
		}
		break;
	case CMD_LIST_PROGRAMS:
		list_programs ( jfst, client_sock );
		break;
	case CMD_GET_PROGRAM:
		get_program ( jfst, client_sock );
		break;
	case CMD_SET_PROGRAM:
		fst_program_change ( jfst->fst, strtol(value, NULL, 10) );
		break;
	case CMD_GET_CHANNEL:
		get_channel ( jfst, client_sock );
		break;
	case CMD_SET_CHANNEL:
		midi_filter_one_channel_set( &jfst->channel, strtol(value, NULL, 10) );
		break;
	case CMD_SUSPEND:
		jfst_bypass ( jfst, true );
		break;
	case CMD_RESUME:
		jfst_bypass ( jfst, false );
		break;
	case CMD_LOAD:
		ack = jfst_load_state ( jfst, value );
		break;
	case CMD_SAVE:
		ack = jfst_save_state ( jfst, value );
		break;
	case CMD_NEWS:
		if ( !strcasecmp(value,"all") ) {
			news( jfst, client_sock, &all_changes );
		} else {
			news( jfst, client_sock, changes );
		}
		break;
	case CMD_CPU:
		cpu_usage( client_sock );
		break;
	case CMD_QUIT:
		ret = false;
		break;
	case CMD_HELP:
		help( client_sock );
		break;
	case CMD_KILL:
		jfst_quit ( jfst );
		break;
	case CMD_UNKNOWN:
	default:
		printf ( "Unknown command: %s\n", msg );
		ack = false;
	}

	// Send ACK / NAK
	const char* RESP = (ack) ? ACK : NAK;
	serv_send_client_data ( client_sock, RESP, strlen(RESP) );

	return ret;
}

static bool handle_client_callback ( char* msg, int client_sock, uint8_t* changes, void* data ) {
	JFST* jfst = (JFST*) data;
	return jfst_proto_client_dispatch ( jfst, msg, changes, client_sock );
}

/* Public functions */
bool jfst_proto_init ( JFST* jfst ) {
	puts ( "Starting JFST control server ..." );
	bool ok = serv_init ( jfst->ctrl_port_number, handle_client_callback, jfst );
	if ( ! ok ) fst_error ( "Cannot create CTRL socket :(" );

	CPUusage_init();

	return ok;
}
