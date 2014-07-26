#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "../serv/serv.h"
#include "jfst.h"

#define ACK "<OK>"

/* jfst.c */
void jvst_quit(JackVST* jvst);

static int serv_fd = 0;

static void list_programs ( JackVST* jvst, int client_sock ) {
	FST* fst = jvst->fst;
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

static void get_program ( JackVST* jvst, int client_sock ) {
	char msg[16];
	sprintf( msg, "PROGRAM:%d", jvst->fst->current_program );
	serv_send_client_data ( client_sock, msg, strlen(msg) );
}

static struct PROTO_MAP proto_string_map[] = {
	{ CMD_EDITOR_OPEN, "editor open" },
	{ CMD_EDITOR_CLOSE, "editor_close" },
	{ CMD_LIST_PROGRAMS, "list_programs" },
	{ CMD_GET_PROGRAM, "get_program" },
	{ CMD_SET_PROGRAM, "set_program" },
	{ CMD_SUSPEND, "suspend" },
	{ CMD_RESUME, "resume" },
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

static bool jvst_proto_client_dispatch ( JackVST* jvst, int client_sock ) {
        char msg[64];
        if ( ! serv_client_get_data ( client_sock, msg, sizeof msg ) )
		return false;

	printf ( "GOT MSG: %s\n", msg );

	int value = 0;
	char* sep = strchr ( msg, ':' );
	if ( sep != NULL ) {
		*sep = '\0';
		value = strtol ( sep + 1, NULL, 10 );
	}

	switch ( proto_lookup ( msg ) ) {
	case CMD_EDITOR_OPEN:
		fst_run_editor ( jvst->fst, false );
		break;
	case CMD_EDITOR_CLOSE:
		fst_call ( jvst->fst, EDITOR_CLOSE );
		break;
	case CMD_LIST_PROGRAMS:
		list_programs ( jvst, client_sock );
		break;
	case CMD_GET_PROGRAM:
		get_program ( jvst, client_sock );
		break;
	case CMD_SET_PROGRAM:
		fst_program_change ( jvst->fst, value );
		break;
	case CMD_SUSPEND:
		jvst_bypass ( jvst, true );
		break;
	case CMD_RESUME:
		jvst_bypass ( jvst, false );
		break;
	case CMD_KILL:
		jvst_quit ( jvst );
		break;
	case CMD_UNKNOWN:
	default:
		printf ( "Unknown command: %s\n", msg );
	}

	// Send ACK
	serv_send_client_data ( client_sock, ACK, strlen(ACK) );

	return true;
}

static bool handle_client_connection (GIOChannel *source, GIOCondition condition, gpointer data ) {
	JackVST* jvst = (JackVST*) data;

	int client_fd = g_io_channel_unix_get_fd ( source );
	bool ok = jvst_proto_client_dispatch ( jvst, client_fd );

	return ok;
}

static bool handle_server_connection (GIOChannel *source, GIOCondition condition, gpointer data ) {
	JackVST* jvst = (JackVST*) data;

	int serv_fd = g_io_channel_unix_get_fd ( source );
	int client_fd = serv_get_client ( serv_fd );

	/* Watch client socket */
	GIOChannel* channel = g_io_channel_unix_new ( client_fd );
	g_io_add_watch_full(
		channel,
		G_PRIORITY_DEFAULT_IDLE,
		G_IO_IN,
		(GIOFunc) handle_client_connection,
		jvst, NULL
	);

	return true;
}

/* Public functions */
bool jvst_proto_init ( JackVST* jvst ) {
	puts ( "Starting JVST PROTO control server ..." );
	serv_fd = serv_get_sock ( jvst->ctrl_port_number );
	if ( ! serv_fd ) {
		fst_error ( "Cannot create CTRL socket :(" );
		return false;
	}

	/* Watch server socket */
	GIOChannel* channel = g_io_channel_unix_new(serv_fd);
	g_io_add_watch_full (
		channel,
		G_PRIORITY_DEFAULT_IDLE,
		G_IO_IN,
		(GIOFunc) handle_server_connection,
		jvst, NULL
	);
	g_io_channel_unref(channel);

	return true;
}

bool jvst_proto_close ( JackVST* jvst ) {
	if ( serv_fd ) serv_close_socket ( serv_fd );
	return true;
}
