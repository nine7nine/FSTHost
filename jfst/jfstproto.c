#include <stdio.h>
#include <string.h>

#include "../serv/serv.h"
#include "jfst.h"

#define ACK "<OK>"

/* jfst.c */
extern void jfst_quit(JFST* jfst);

/* cpuusage.c */
extern void CPUusage_init();
extern double CPUusage_getCurrentValue();

static void list_programs ( JFST* jfst, int client_sock ) {
	FST* fst = jfst->fst;
	int32_t i;
	for ( i = 0; i < fst_num_presets(fst); i++ ) {
		/* VST standard says that progName is 24 bytes but some plugs use more characters */
		char progName[32];
		if ( fst->vst_version >= 2 ) {
			fst_get_program_name(fst, i, progName, sizeof(progName));
		} else {
			/* FIXME:
			So what ? nasty plugin want that we iterate around all presets ?
			no way ! We don't have time for this
			*/
			sprintf ( progName, "preset %d", i );
		}
		serv_send_client_data ( client_sock, progName, strlen(progName) );
        }
}

static void get_program ( JFST* jfst, int client_sock ) {
	char msg[16];
	sprintf( msg, "PROGRAM:%d", jfst->fst->current_program );
	serv_send_client_data ( client_sock, msg, strlen(msg) );
}

static void cpu_usage ( int client_sock ) {
	char msg[8];
	snprintf( msg, sizeof msg, "%g", CPUusage_getCurrentValue() );
	serv_send_client_data ( client_sock, msg, strlen(msg) );
}

static struct PROTO_MAP proto_string_map[] = {
	{ CMD_EDITOR_OPEN, "editor_open" },
	{ CMD_EDITOR_CLOSE, "editor_close" },
	{ CMD_LIST_PROGRAMS, "list_programs" },
	{ CMD_GET_PROGRAM, "get_program" },
	{ CMD_SET_PROGRAM, "set_program" },
	{ CMD_SUSPEND, "suspend" },
	{ CMD_RESUME, "resume" },
	{ CMD_CPU, "cpu" },
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

static bool jfst_proto_client_dispatch ( JFST* jfst, char* msg, int client_sock ) {
	printf ( "GOT MSG: %s\n", msg );

	bool ret = true;
	int value = 0;
	char* sep = strchr ( msg, ':' );
	if ( sep != NULL ) {
		*sep = '\0';
		value = strtol ( ++sep, NULL, 10 );
	}

	switch ( proto_lookup ( msg ) ) {
	case CMD_EDITOR_OPEN:
		fst_run_editor ( jfst->fst, false );
		break;
	case CMD_EDITOR_CLOSE:
		fst_call ( jfst->fst, EDITOR_CLOSE );
		break;
	case CMD_LIST_PROGRAMS:
		list_programs ( jfst, client_sock );
		break;
	case CMD_GET_PROGRAM:
		get_program ( jfst, client_sock );
		break;
	case CMD_SET_PROGRAM:
		fst_program_change ( jfst->fst, value );
		break;
	case CMD_SUSPEND:
		jfst_bypass ( jfst, true );
		break;
	case CMD_RESUME:
		jfst_bypass ( jfst, false );
		break;
	case CMD_CPU:
		cpu_usage( client_sock );
		break;
	case CMD_QUIT:
		ret = false;
		break;
	case CMD_KILL:
		jfst_quit ( jfst );
		break;
	case CMD_UNKNOWN:
	default:
		printf ( "Unknown command: %s\n", msg );
	}

	// Send ACK
	serv_send_client_data ( client_sock, ACK, strlen(ACK) );

	return ret;
}

static bool handle_client_callback ( char* msg, int client_sock, void* data ) {
	JFST* jfst = (JFST*) data;
	return jfst_proto_client_dispatch ( jfst, msg, client_sock );
}

/* Public functions */
bool jfst_proto_init ( JFST* jfst ) {
	puts ( "Starting JFST control server ..." );
	bool ok = serv_init ( jfst->ctrl_port_number, handle_client_callback, jfst );
	if ( ! ok ) fst_error ( "Cannot create CTRL socket :(" );

	CPUusage_init();

	return ok;
}
