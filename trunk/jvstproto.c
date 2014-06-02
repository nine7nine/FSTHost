#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include "jackvst.h"

#define ACK "<OK>"

/* serv.c */
int serv_get_sock ( uint16_t port );
int serv_get_client ( int socket_desc );
bool serv_send_client_data ( int client_sock, char* msg, int msg_len );
bool serv_client_get_data ( int client_sock, char* msg, int msg_max_len );
void serv_close_socket ( int socket_desc );

/* jfst.c */
void jvst_quit(JackVST* jvst);

static int serv_fd = 0;

static void get_programs ( JackVST* jvst, int client_sock ) {
	FST* fst = jvst->fst;
	int32_t i;
	for ( i = 0; i < fst->plugin->numPrograms; i++ ) {
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

static bool jvst_proto_client_dispatch ( JackVST* jvst, int client_sock ) {
        char msg[64];
        if ( ! serv_client_get_data ( client_sock, msg, sizeof msg ) )
		return false;

	printf ( "GOT MSG: %s\n", msg );

	if ( ! strcasecmp ( msg, "editor open" ) ) {
		fst_run_editor ( jvst->fst, false );
	} else if (  ! strcasecmp ( msg, "editor close" ) ) {
		fst_call ( jvst->fst, EDITOR_CLOSE );
	} else if (  ! strcasecmp ( msg, "programs" ) ) {
		get_programs ( jvst, client_sock );
	} else if (  ! strcasecmp ( msg, "suspend" ) ) {
		jvst_bypass ( jvst, true );
	} else if (  ! strcasecmp ( msg, "resume" ) ) {
		jvst_bypass ( jvst, false );
	} else if (  ! strcasecmp ( msg, "kill" ) ) {
		jvst_quit ( jvst );
	} else {
		char* sep = strchr ( msg, ':' );
		if ( sep != NULL ) {
			*sep = '\0';
			int value = strtol ( sep + 1, NULL, 10 );
			if (  ! strcasecmp ( msg, "program" ) ) {
				fst_program_change ( jvst->fst, value );
			}
		}
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
