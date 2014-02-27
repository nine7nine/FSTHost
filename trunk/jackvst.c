#include <jackvst.h>
#include <amc.h>

/* fps.c */
bool fps_save(JackVST* jvst, const char* filename);
bool fps_load(JackVST* jvst, const char* filename);

/* jackamc.c */
extern void jvstamc_init ( JackVST* jvst, AMC* amc );

/* info.c */
FST* fst_info_load_open ( const char* dbpath, const char* plug_spec );

JackVST* jvst_new() {
	JackVST* jvst = calloc (1, sizeof (JackVST));
	short i;

	pthread_mutex_init (&jvst->sysex_lock, NULL);
	pthread_cond_init (&jvst->sysex_sent, NULL);

	jvst->with_editor = WITH_EDITOR_SHOW;
	jvst->volume = 1;
	jvst->tempo = -1; // -1 here mean get it from Jack
	/* Local Keyboard MIDI CC message (122) is probably not used by any VST */
	jvst->want_state_cc = 122;
	jvst->midi_learn = FALSE;
	jvst->midi_learn_CC = -1;
	jvst->midi_learn_PARAM = -1;
	for(i=0; i<128;++i) jvst->midi_map[i] = -1;
	jvst->midi_pc = MIDI_PC_PLUG; // mean that plugin take care of Program Change

	// Little trick (const entries)
	SysExIdentReply sxir = SYSEX_IDENT_REPLY;
	memcpy(&jvst->sysex_ident_reply, &sxir, sizeof(SysExIdentReply));

	SysExDumpV1 sxd = SYSEX_DUMP;
	memcpy(&jvst->sysex_dump, &sxd, sizeof(SysExDumpV1));

	jvst->transposition = midi_filter_transposition_init ( &jvst->filters );
	midi_filter_one_channel_init( &jvst->filters, &jvst->channel );

	return jvst;
}

/* plug_spec could be path, dll name or eff/plug name */
bool jvst_load (JackVST* jvst, const char* plug_spec) {
	/* Try load directly */
	printf( "yo... lets see... ( try load directly)\n" );
	jvst->fst = fst_load_open ( plug_spec );
	if ( ! jvst->fst ) {
		printf ( "... and now for something completely different ... try load using XML DB\n" );
		if ( ! jvst->dbinfo_file ) return false;
		jvst->fst = fst_info_load_open ( jvst->dbinfo_file, plug_spec );
		if ( ! jvst->fst ) return false;
	}

	/* Plugin loaded - bind Jack to Audio Master Callback */
	jvstamc_init ( jvst, &( jvst->fst->amc ) );

	return true;
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

bool jvst_load_state (JackVST* jvst, const char * filename) {
	bool success = false;
	char* file_ext = strrchr(filename, '.');
	if (! file_ext) return false;

	if ( !strcasecmp(file_ext, ".fps") ) {
		success = fps_load(jvst, filename);
	} else if ( !strcasecmp(file_ext, ".fxp") || !strcasecmp(file_ext, ".fxb") ) {
		if (jvst->fst) success = fst_load_fxfile(jvst->fst, filename);
	} else {
		printf("Unkown file type\n");
	}

	if (success) {
		printf("File %s loaded\n", filename);
	} else {
		printf("Unable to load file %s\n", filename);
	}

	return success;
}

bool jvst_save_state (JackVST* jvst, const char * filename) {
	bool ret = FALSE;
	char* file_ext = strrchr(filename, '.');

	if (strcasecmp(file_ext, ".fxp") == 0) {
		ret = fst_save_fxfile(jvst->fst, filename, FXPROGRAM);
	} else if (strcasecmp(file_ext, ".fxb") == 0) {
		ret = fst_save_fxfile(jvst->fst, filename, FXBANK);
	} else if (strcasecmp(file_ext, ".fps") == 0) {
		ret = fps_save(jvst, filename);
	} else {
		printf("Unkown file type\n");
	}

	return ret;
}
