#include "jfst.h"
#include "../fst/amc.h"

/* fps.c */
bool fps_save(JackVST* jvst, const char* filename);
bool fps_load(JackVST* jvst, const char* filename);

/* jackamc.c */
extern void jvstamc_init ( JackVST* jvst, AMC* amc );

/* info.c */
FST* fst_info_load_open ( const char* dbpath, const char* plug_spec );

static inline void jvst_midi_learn_init ( JackVST* jvst ) {
	short i;
	MidiLearn* ml = &jvst->midi_learn;
	ml->wait = FALSE;
	ml->cc = -1;
	ml->param = -1;
	for(i=0; i<128;++i) ml->map[i] = -1;
}

JackVST* jvst_new() {
	JackVST* jvst = calloc (1, sizeof (JackVST));

	pthread_mutex_init (&jvst->sysex_lock, NULL);
	pthread_cond_init (&jvst->sysex_sent, NULL);

	jvst->with_editor = WITH_EDITOR_SHOW;
	jvst->volume = 1;
	jvst->tempo = -1; // -1 here mean get it from Jack
	/* Local Keyboard MIDI CC message (122) is probably not used by any VST */
	jvst->want_state_cc = 122;
	jvst->midi_pc = MIDI_PC_PLUG; // mean that plugin take care of Program Change

	jvst_midi_learn_init ( jvst );

	jvst->transposition = midi_filter_transposition_init ( &jvst->filters );
	midi_filter_one_channel_init( &jvst->filters, &jvst->channel );

	return jvst;
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

void jvst_destroy (JackVST* jvst) {
	midi_filter_cleanup( &jvst->filters, true );
	free(jvst);
}

void jvst_sysex_set_uuid (JackVST* jvst, uint8_t uuid) {
	jvst->sysex_ident_reply.model[0] = jvst->sysex_dump.uuid = uuid;
}

void jvst_log(const char *msg) { fprintf(stderr, "JACK: %s\n", msg); }

void jvst_set_volume(JackVST* jvst, short volume) {
	if (jvst->volume != -1) jvst->volume = powf(volume / 63.0f, 2);
}

unsigned short jvst_get_volume(JackVST* jvst) {
	if (jvst->volume == -1) return 0;

	short ret = roundf(sqrtf(jvst->volume) * 63.0f);

	return (ret < 0) ? 0 : (ret > 127) ? 127 : ret;
}

void jvst_apply_volume ( JackVST* jvst, jack_nframes_t nframes, float** outs ) {
	if (jvst->volume == -1) return;

	int32_t i;
	for ( i = 0; i < jvst->numOuts; i++ ) {
		jack_nframes_t n;
		for ( n = 0; n < nframes; n++ )
			outs[i][n] *= jvst->volume;
	}
}

void jvst_bypass(JackVST* jvst, bool bypass) {
	jvst->want_state = WANT_STATE_NO;
	if (bypass & !jvst->bypassed) {
		jvst->bypassed = TRUE;
		fst_call ( jvst->fst, SUSPEND );
	} else if (!bypass & jvst->bypassed) {
		fst_call ( jvst->fst, RESUME );
		jvst->bypassed = FALSE;
	}
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
