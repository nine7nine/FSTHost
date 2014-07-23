#include <sys/mman.h>

#include "jfst.h"
#include "../fst/amc.h"

/* fps.c */
extern bool fps_save(JackVST* jvst, const char* filename);
extern bool fps_load(JackVST* jvst, const char* filename);

/* jackamc.c */
extern void jvstamc_init ( JackVST* jvst, AMC* amc );

/* sysex.c */
extern bool jvst_sysex_init ( JackVST* jvst );
extern void jvst_sysex_handler ( JackVST* jvst );
extern void jvst_sysex_notify ( JackVST* jvst );

/* info.c */
extern FST* fst_info_load_open ( const char* dbpath, const char* plug_spec );

/* fsthost.c - FIXME */
extern void jvst_quit(JackVST* jvst);

JackVST* jvst_new() {
	JackVST* jvst = calloc (1, sizeof (JackVST));

	pthread_mutex_init (&jvst->sysex_lock, NULL);
	pthread_cond_init (&jvst->sysex_sent, NULL);

	jvst->with_editor = WITH_EDITOR_SHOW;
	jvst->volume = -1;
	jvst->tempo = -1; // -1 here mean get it from Jack
	/* Local Keyboard MIDI CC message (122) is probably not used by any VST */
	jvst->want_state_cc = 122;
	jvst->midi_pc = MIDI_PC_PLUG; // mean that plugin take care of Program Change

	event_queue_init ( &jvst->event_queue );

	/* MidiLearn */
	short i;
	MidiLearn* ml = &jvst->midi_learn;
	ml->wait = FALSE;
	ml->cc = -1;
	ml->param = -1;
	for(i=0; i<128;++i) ml->map[i] = -1;

	jvst->transposition = midi_filter_transposition_init ( &jvst->filters );
	midi_filter_one_channel_init( &jvst->filters, &jvst->channel );

	return jvst;
}

static void jvst_destroy (JackVST* jvst) {
	midi_filter_cleanup( &jvst->filters, true );
	free(jvst);
}

bool jvst_init( JackVST* jvst, int32_t max_in, int32_t max_out ) {
	FST* fst = jvst->fst;

	// Set client name (if user did not provide own)
	if (!jvst->client_name) jvst->client_name = fst->handle->name;

	// Jack Audio
	jvst->numIns = (max_in >= 0 && max_in < fst_num_ins(fst)) ? max_in : fst_num_ins(fst);
	jvst->numOuts = (max_out >= 0 && max_out < fst_num_outs(fst)) ? max_out : fst_num_outs(fst);
	printf("Port Layout (FSTHost/plugin) IN: %d/%d OUT: %d/%d\n", 
		jvst->numIns, fst_num_ins(fst), jvst->numOuts, fst_num_outs(fst));

	bool want_midi_out = fst_want_midi_out ( jvst->fst );
	if ( ! jvst_jack_init ( jvst, want_midi_out ) ) return false;

	/* Sysex init */
	if ( ! jvst_sysex_init ( jvst ) ) return false;

	// Lock our crucial memory ( which is used in process callback )
	// TODO: this is not all
	mlock ( jvst, sizeof(JackVST) );
	mlock ( fst, sizeof(FST) );

	// Set block size / sample rate
	fst_call_dispatcher (fst, effSetSampleRate, 0, 0, NULL, (float) jvst->sample_rate);
	fst_call_dispatcher (fst, effSetBlockSize, 0, (intptr_t) jvst->buffer_size, NULL, 0.0f);
	printf("Sample Rate: %d | Block Size: %d\n", jvst->sample_rate, jvst->buffer_size);

	return true;
}

void jvst_close ( JackVST* jvst ) {
	puts("Jack Deactivate");
	jack_deactivate ( jvst->client );
	jack_client_close ( jvst->client );

	fst_close(jvst->fst);

	free ( jvst->inports );
	free ( jvst->outports );

	jvst_destroy( jvst );
}

/* return true if want quit */
void jvst_session_handler( JackVST* jvst, jack_session_event_t* event, const char* appname ) {
	puts("session callback");

	// Save state
	char filename[MAX_PATH];
	snprintf( filename, sizeof(filename), "%sstate.fps", event->session_dir );
	if ( ! jvst_save_state( jvst, filename ) ) {
		puts("SAVE ERROR");
		event->flags |= JackSessionSaveError;
	}

	// Reply to session manager
	char retval[256];
	snprintf( retval, sizeof(retval), "%s -u %s -s \"${SESSION_DIR}state.fps\"", appname, event->client_uuid);
	event->command_line = strndup( retval, strlen(retval) + 1  );

	jack_session_reply(jvst->client, event);

	bool quit = false;
	if (event->type == JackSessionSaveAndQuit) {
		puts("JackSession manager ask for quit");
		quit = true;
	}

	jack_session_event_free(event);

        if ( quit ) jvst_quit ( jvst );
}

/* plug_spec could be path, dll name or eff/plug name */
static bool jvst_load_directly (JackVST* jvst, const char* plug_spec ) {
	printf( "yo... lets see... ( try load directly )\n" );
	jvst->fst = fst_load_open ( plug_spec );
	if ( jvst->fst ) return true;

	if ( ! jvst->dbinfo_file ) return false;
	printf ( "... and now for something completely different ... try load using XML DB\n" );
	jvst->fst = fst_info_load_open ( jvst->dbinfo_file, plug_spec );
	return ( jvst->fst ) ? true : false;
}

bool jvst_load (JackVST* jvst, const char* plug_spec, bool want_state_and_amc) {
	/* Try load directly */
	bool loaded = false;
	if ( plug_spec )
		loaded = jvst_load_directly ( jvst, plug_spec );

	/* load state if requested - state file may contain plugin path
	   NOTE: it can call jvst_load */
	if ( want_state_and_amc && jvst->default_state_file )
		loaded = jvst_load_state (jvst, jvst->default_state_file);

	/* bind Jack to Audio Master Callback */
	if ( loaded && want_state_and_amc )
		jvstamc_init ( jvst, &( jvst->fst->amc ) );

	return loaded;
}

void jvst_bypass(JackVST* jvst, bool bypass) {
	if (bypass & !jvst->bypassed) {
		jvst->bypassed = TRUE;
		fst_call ( jvst->fst, SUSPEND );
	} else if (!bypass & jvst->bypassed) {
		fst_call ( jvst->fst, RESUME );
		jvst->bypassed = FALSE;
	}
}

void jvst_idle(JackVST* jvst, const char* appname) {
	// Handle SysEx Input
	jvst_sysex_handler(jvst);

	Event* ev;
	while ( (ev = event_queue_get ( &jvst->event_queue )) ) {
		switch ( ev->type ) {
		case EVENT_STATE:
			switch ( ev->value ) {
			case WANT_STATE_BYPASS: jvst_bypass(jvst,TRUE); break;
			case WANT_STATE_RESUME: jvst_bypass(jvst,FALSE); break;
			}
			break;
		case EVENT_PC:
			// Self Program change support
			if (jvst->midi_pc != MIDI_PC_SELF) break;
			
			fst_program_change(jvst->fst, ev->value);
			break;
		case EVENT_GRAPH:
			// Attempt to connect MIDI ports to control app if Graph order change
			jvst_connect_to_ctrl_app(jvst);
			break;
		case EVENT_SESSION:
			jvst_session_handler(jvst, ev->ptr, appname);
			break;
		}
	}

	// MIDI learn support
	MidiLearn* ml = &jvst->midi_learn;
	if ( ml->wait && ml->cc >= 0 && ml->param >= 0 ) {
		ml->map[ml->cc] = ml->param;
		ml->wait = false;

		char name[32];
		bool success = fst_call_dispatcher( jvst->fst, effGetParamName, ml->param, 0, name, 0 );
		if (success) {
			printf("MIDIMAP CC: %d => %s\n", ml->cc, name);
		} else {
			printf("MIDIMAP CC: %d => %d\n", ml->cc, ml->param);
		}
	}

	// Send notify if we want notify and something change
	if (jvst->sysex_want_notify) jvst_sysex_notify(jvst);
}

typedef enum {
	JVST_FILE_TYPE_FXP,
	JVST_FILE_TYPE_FXB,
	JVST_FILE_TYPE_FPS,
	JVST_FILE_TYPE_UNKNOWN
} JVST_FileType;

static JVST_FileType get_file_type ( const char * filename ) {
	char* file_ext = strrchr(filename, '.');
	if (! file_ext) return JVST_FILE_TYPE_UNKNOWN;

	if ( !strcasecmp(file_ext, ".fps") ) return JVST_FILE_TYPE_FPS;
	if ( !strcasecmp(file_ext, ".fxp") ) return JVST_FILE_TYPE_FXP;
	if ( !strcasecmp(file_ext, ".fxb") ) return JVST_FILE_TYPE_FXB;

	printf("Unkown file type\n");
	return JVST_FILE_TYPE_UNKNOWN;
}

bool jvst_load_state (JackVST* jvst, const char * filename) {
	bool success = false;

	switch ( get_file_type ( filename ) ) {
	case JVST_FILE_TYPE_FPS:
		success = fps_load (jvst, filename);
		break;

	case JVST_FILE_TYPE_FXP:
	case JVST_FILE_TYPE_FXB:
		if (! jvst->fst) break;
		success = fst_load_fxfile (jvst->fst, filename);
		break;

	case JVST_FILE_TYPE_UNKNOWN:;
	}

	if (success) {
		printf("File %s loaded\n", filename);
	} else {
		printf("Unable to load file %s\n", filename);
	}

	return success;
}

bool jvst_save_state (JackVST* jvst, const char * filename) {
	switch ( get_file_type ( filename ) ) {
	case JVST_FILE_TYPE_FPS:
		return fps_save(jvst, filename);

	case JVST_FILE_TYPE_FXB:
		return fst_save_fxfile(jvst->fst, filename, FXBANK);

	case JVST_FILE_TYPE_FXP:
		return fst_save_fxfile (jvst->fst, filename, FXPROGRAM);

	case JVST_FILE_TYPE_UNKNOWN:;
	}
	return false;
}
