#include <stdio.h>
#include <sys/mman.h>

#include "jfst.h"
#include "../fst/amc.h"
#include "../xmldb/info.h"

/* fps.c */
extern bool fps_save(JFST* jfst, const char* filename);
extern bool fps_load(JFST* jfst, const char* filename);

/* jackamc.c */
extern void jfstamc_init ( JFST* jfst, AMC* amc );

/* sysex.c */
extern void jfst_sysex_init ( JFST* jfst );
extern void jfst_sysex_handler ( JFST* jfst );
extern void jfst_sysex_notify ( JFST* jfst );
extern void jfst_sysex_gen_random_id ( JFST* jfst );

JFST* jfst_new( const char* appname ) {
	JFST* jfst = calloc (1, sizeof (JFST));

	jfst->appname = appname;

	pthread_mutex_init (&jfst->sysex_lock, NULL);
	pthread_cond_init (&jfst->sysex_sent, NULL);

	jfst->with_editor = WITH_EDITOR_SHOW;
	jfst->volume = 1; // 63 here mean zero
	/* Local Keyboard MIDI CC message (122) is probably not used by any VST */
	jfst->want_state_cc = 122;
	jfst->midi_pc = MIDI_PC_PLUG; // plugin take care of Program Change
	jfst->want_auto_midi_physical = true; // By default autoconnect MIDI In port to all physical

	event_queue_init ( &jfst->event_queue );

	/* MidiLearn */
	short i;
	MidiLearn* ml = &jfst->midi_learn;
	ml->wait = FALSE;
	ml->cc = -1;
	ml->param = -1;
	for(i=0; i<128;++i) ml->map[i] = -1;

	jfst->transposition = midi_filter_transposition_init ( &jfst->filters );
	midi_filter_one_channel_init( &jfst->filters, &jfst->channel );
	
	jfst_sysex_init ( jfst );

	return jfst;
}

static void jfst_destroy (JFST* jfst) {
	midi_filter_cleanup( &jfst->filters, true );
	free(jfst);
}

static void jfst_set_aliases ( JFST* jfst, FSTPortType type ) {
	int32_t count;
	jack_port_t** ports;
	switch ( type ) {
	case FST_PORT_IN:
		count = jfst->numIns;
		ports = jfst->inports;
		break;
	case FST_PORT_OUT:
		count = jfst->numOuts;
		ports = jfst->outports;
		break;
	default: return;
	}

	// Set port alias
	int32_t i;
	for ( i=0; i < count; i++ ) {
		char* name = fst_get_port_name ( jfst->fst, i, type );
		if ( ! name ) continue;

		jack_port_set_alias ( ports[i], name );
		free ( name );
	}
}

bool jfst_init( JFST* jfst, int32_t max_in, int32_t max_out ) {
	FST* fst = jfst->fst;

	// Set client name (if user did not provide own)
	if (!jfst->client_name) jfst->client_name = fst->handle->name;

	// Jack Audio
	jfst->numIns = (max_in >= 0 && max_in < fst_num_ins(fst)) ? max_in : fst_num_ins(fst);
	jfst->numOuts = (max_out >= 0 && max_out < fst_num_outs(fst)) ? max_out : fst_num_outs(fst);
	printf("Port Layout (FSTHost/plugin) IN: %d/%d OUT: %d/%d\n", 
		jfst->numIns, fst_num_ins(fst), jfst->numOuts, fst_num_outs(fst));

	bool want_midi_out = fst_want_midi_out ( jfst->fst );
	if ( ! jfst_jack_init ( jfst, want_midi_out ) ) return false;

	// Port aliases
	if ( jfst->want_port_aliases ) {
		jfst_set_aliases ( jfst, FST_PORT_IN );
		jfst_set_aliases ( jfst, FST_PORT_OUT );
	}

	// Lock our crucial memory ( which is used in process callback )
	// TODO: this is not all
	mlock ( jfst, sizeof(JFST) );
	mlock ( fst, sizeof(FST) );

	// Set block size / sample rate
	fst_call_dispatcher (fst, effSetSampleRate, 0, 0, NULL, (float) jfst->sample_rate);
	fst_call_dispatcher (fst, effSetBlockSize, 0, (intptr_t) jfst->buffer_size, NULL, 0.0f);
	printf("Sample Rate: %d | Block Size: %d\n", jfst->sample_rate, jfst->buffer_size);

	jfst_sysex_gen_random_id ( jfst );

	return true;
}

void jfst_close ( JFST* jfst ) {
	jack_client_close ( jfst->client );

	fst_close(jfst->fst);

	free ( jfst->inports );
	free ( jfst->outports );

	jfst_destroy( jfst );
}

/* return false if want quit */
bool jfst_session_handler( JFST* jfst, jack_session_event_t* event ) {
	puts("session callback");

	// Save state
	char filename[MAX_PATH];
	snprintf( filename, sizeof(filename), "%sstate.fps", event->session_dir );
	if ( ! jfst_save_state( jfst, filename ) ) {
		puts("SAVE ERROR");
		event->flags |= JackSessionSaveError;
	}

	// Reply to session manager
	char retval[256];
	snprintf( retval, sizeof(retval), "%s -u %s -s \"${SESSION_DIR}state.fps\"", jfst->appname, event->client_uuid);
	event->command_line = strndup( retval, strlen(retval) + 1  );

	jack_session_reply(jfst->client, event);

	bool quit = (event->type == JackSessionSaveAndQuit);

	jack_session_event_free(event);

        if ( quit ) {
		puts("JackSession manager ask for quit");
		return false;
	}
	return true;
}

/* plug_spec could be path, dll name or eff/plug name */
bool jfst_load (JFST* jfst, const char* plug_spec, bool want_state_and_amc, bool state_can_fail) {
	printf( "yo... lets see...\n" );

	/* Try load directly */
	bool loaded = false;
	if ( plug_spec ) {
		jfst->fst = fst_info_load_open ( jfst->dbinfo_file, plug_spec );
		loaded = ( jfst->fst != NULL );
	}

	/* load state if requested - state file may contain plugin path
	   NOTE: it can call jfst_load */
	if ( want_state_and_amc && jfst->default_state_file) {
		bool state_loaded = jfst_load_state (jfst, NULL);
		if ( ! state_can_fail ) loaded = state_loaded;
	}

	/* bind Jack to Audio Master Callback */
	if ( loaded && want_state_and_amc )
		jfstamc_init ( jfst, &( jfst->fst->amc ) );

	return loaded;
}

void jfst_bypass(JFST* jfst, bool bypass) {
	if ( bypass && !jfst->bypassed ) {
		jfst->bypassed = TRUE;
		fst_call ( jfst->fst, SUSPEND );
	} else if ( !bypass && jfst->bypassed ) {
		fst_call ( jfst->fst, RESUME );
		jfst->bypassed = FALSE;
	}
}

static Changes detect_change( JFST* jfst ) {
        // Wait until program change
        if (jfst->fst->event_call.type == PROGRAM_CHANGE)
		return 0;

	DetectChangesLast* L = &jfst->last;
	Changes ret = 0;

	if ( L->bypassed != jfst->bypassed ) {
		L->bypassed = jfst->bypassed;
		ret |= CHANGE_BYPASS;
	}

	if ( L->program != jfst->fst->current_program ) {
		L->program = jfst->fst->current_program;
		ret |= CHANGE_PROGRAM;
	}

	if ( L->volume != jfst_get_volume(jfst) ) {
		L->volume = jfst_get_volume(jfst);
		ret |= CHANGE_VOLUME;
	}

	if ( L->channel != midi_filter_one_channel_get( &jfst->channel ) ) {
		L->channel  = midi_filter_one_channel_get( &jfst->channel );
		ret |= CHANGE_CHANNEL;
	}

	if ( L->editor != (bool) jfst->fst->window ) {
		L->editor = (bool) jfst->fst->window;
		ret |= CHANGE_EDITOR;
	}

	return ret;
}

/* Return false if want quit */
Changes jfst_idle(JFST* jfst ) {
	// Handle SysEx Input
	jfst_sysex_handler(jfst);

	Event* ev;
	while ( (ev = event_queue_get ( &jfst->event_queue )) ) {
		switch ( ev->type ) {
		case EVENT_BYPASS:
			jfst_bypass( jfst, ev->value );
			break;
		case EVENT_PC:
			// Self Program change support
			if (jfst->midi_pc == MIDI_PC_SELF)
				fst_program_change(jfst->fst, ev->value);
			break;
		case EVENT_GRAPH:
			// Attempt to connect MIDI ports to control app if Graph order change
			jfst_connect_to_ctrl_app(jfst);
			// Autoconnect MIDI IN to all physical ports
			if (jfst->want_auto_midi_physical)
				jfst_connect_midi_to_physical(jfst);
			break;
		case EVENT_SESSION:
			if ( ! jfst_session_handler(jfst, ev->ptr) )
				return CHANGE_QUIT;
			break;
		}
	}

	// MIDI learn support
	MidiLearn* ml = &jfst->midi_learn;
	if ( ml->wait && ml->cc >= 0 && ml->param >= 0 ) {
		ml->map[ml->cc] = ml->param;
		ml->wait = false;

		printf("MIDIMAP CC: %d => ", ml->cc);
		char name[32];
		bool success = fst_call_dispatcher( jfst->fst, effGetParamName, ml->param, 0, name, 0 );
		if (success) {
			printf("%s\n", name);
		} else {
			printf("%d\n", ml->param);
		}
	}

	Changes change = detect_change( jfst );
	Changes change_sysex_mask = CHANGE_BYPASS|CHANGE_CHANNEL|CHANGE_VOLUME|CHANGE_PROGRAM;

	if ( change & change_sysex_mask ) {
		// Send notify if we want notify and something change
		if (jfst->sysex_want_notify) jfst_sysex_notify(jfst);
		puts ( "SYSEX CHANGE" );
	}

	return change;
}

typedef enum {
	JFST_FILE_TYPE_FXP,
	JFST_FILE_TYPE_FXB,
	JFST_FILE_TYPE_FPS,
	JFST_FILE_TYPE_UNKNOWN
} JFST_FileType;

static JFST_FileType get_file_type ( const char * filename ) {
	char* file_ext = strrchr(filename, '.');
	if (! file_ext) return JFST_FILE_TYPE_UNKNOWN;

	if ( !strcasecmp(file_ext, ".fps") ) return JFST_FILE_TYPE_FPS;
	if ( !strcasecmp(file_ext, ".fxp") ) return JFST_FILE_TYPE_FXP;
	if ( !strcasecmp(file_ext, ".fxb") ) return JFST_FILE_TYPE_FXB;

	puts("Unkown file type");
	return JFST_FILE_TYPE_UNKNOWN;
}

bool jfst_load_state (JFST* jfst, const char* filename) {
	bool success = false;

	if ( ! filename ) {
		if ( ! jfst->default_state_file ) return false;
		filename = jfst->default_state_file;
	}

	switch ( get_file_type ( filename ) ) {
	case JFST_FILE_TYPE_FPS:
		success = fps_load (jfst, filename);
		break;

	case JFST_FILE_TYPE_FXP:
	case JFST_FILE_TYPE_FXB:
		if (! jfst->fst) break;
		success = fst_load_fxfile (jfst->fst, filename);
		break;

	case JFST_FILE_TYPE_UNKNOWN:;
	}

	if (success) {
		printf("File %s loaded\n", filename);
	} else {
		printf("Unable to load file %s\n", filename);
	}

	return success;
}

bool jfst_save_state (JFST* jfst, const char * filename) {
	switch ( get_file_type ( filename ) ) {
	case JFST_FILE_TYPE_FPS:
		return fps_save(jfst, filename);

	case JFST_FILE_TYPE_FXB:
		return fst_save_fxfile(jfst->fst, filename, FXBANK);

	case JFST_FILE_TYPE_FXP:
		return fst_save_fxfile (jfst->fst, filename, FXPROGRAM);

	case JFST_FILE_TYPE_UNKNOWN:;
	}
	return false;
}
