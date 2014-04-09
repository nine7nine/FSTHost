#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "jackvst.h"

#define PROGRAMS "PROGRAMS"

/* serv.c */
extern bool serv_send_client_data ( int client_sock, char* msg, int msg_len );

static void get_programs ( JackVST* jvst, int client_sock ) {
	FST* fst = jvst->fst;
	int32_t i;
	serv_send_client_data ( client_sock, PROGRAMS, strlen(PROGRAMS) );
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
	serv_send_client_data ( client_sock, PROGRAMS, strlen(PROGRAMS) );
}

bool jvst_dispatch ( JackVST* jvst, int client_sock, const char* msg ) {
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
	}

	return true;
}
