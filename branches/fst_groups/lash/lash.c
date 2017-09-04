#include <stdio.h>
#include <lash/lash.h>
#include "jfst/jfst.h"

static lash_args_t* lash_args;
static lash_client_t* lash_client;

void jfst_lash_init(int* argc, char** argv[]) {
	lash_args = lash_extract_args(argc, argv);
}

void jfst_lash_add ( JFST* jfst ) {
	int flags = LASH_Config_Data_Set;
	lash_client = lash_init(lash_args, jfst->client_name, flags, LASH_PROTOCOL(2, 0));

	if (!lash_client) {
		fprintf(stderr, "%s: could not initialise lash\n", __FUNCTION__);
		fprintf(stderr, "%s: running fsthost without lash session-support\n", __FUNCTION__);
		fprintf(stderr, "%s: to enable lash session-support launch the lash server prior fsthost\n",
			__FUNCTION__);
		return;
	}

	if (lash_enabled(lash_client))
		return;

	lash_event_t* event;
	event = lash_event_new_with_type(LASH_Client_Name);
	lash_event_set_string(event, jfst->client_name);
	lash_send_event(lash_client, event);

	event = lash_event_new_with_type(LASH_Jack_Client_Name);
	lash_event_set_string(event, jfst->client_name);
	lash_send_event(lash_client, event);
}

static void
jfst_lash_save(JFST *jfst) {
	unsigned short i;
	size_t bytelen;
	lash_config_t *config;
	void *chunk;
	MidiLearn* ml = &jfst->midi_learn;

	for( i=0; i < fst_num_params(jfst->fst); i++ ) {
	    char buf[10];
	    float param;
	    
	    snprintf( buf, 9, "%d", i );

	    config = lash_config_new_with_key( buf );

	    fst_lock(jfst->fst);
	    param = fst_get_param(jfst->fst, i); 
	    fst_unlock(jfst->fst);

	    lash_config_set_value_double(config, param);
	    lash_send_config(lash_client, config);
	    //lash_config_destroy( config );
	}

	for( i=0; i<128; i++ ) {
	    char buf[16];
	    
	    snprintf( buf, 15, "midi_map%d", i );
	    config = lash_config_new_with_key( buf );
	    lash_config_set_value_int(config, ml->map[i]);
	    lash_send_config(lash_client, config);
	    //lash_config_destroy( config );
	}

	if ( fst_has_chunks(jfst->fst) ) {
	    printf( "getting chunk...\n" );

	    bytelen = fst_get_chunk( jfst->fst, FXBANK, &chunk );
	    printf( "got tha chunk..\n" );
	    if( bytelen ) {
		if( bytelen < 0 ) {
		    printf( "Chunke len < 0 !!! Not saving chunk.\n" );
		} else {
		    config = lash_config_new_with_key( "bin_chunk" );
		    lash_config_set_value(config, chunk, bytelen );
		    lash_send_config(lash_client, config);
		    //lash_config_destroy( config );
		}
	    }
	}
}

static void
jfst_lash_restore(lash_config_t *config, JFST *jfst ) {
	const char *key = lash_config_get_key(config);

	if (strncmp(key, "midi_map", strlen( "midi_map")) == 0) {
	    short cc = atoi( key+strlen("midi_map") );
	    int param = lash_config_get_value_int( config );

	    if( cc < 0 || cc >= 128 || param < 0 || param >= fst_num_params(jfst->fst) ) 
		return;

	    MidiLearn* ml = &jfst->midi_learn;
	    ml->map[cc] = param;
	    return;
	}

	if ( fst_has_chunks(jfst->fst) ) {
	    if (strcmp(key, "bin_chunk") == 0) {
		fst_set_chunk( jfst->fst, FXBANK, lash_config_get_value_size(config),
			(void*) lash_config_get_value(config) );
	    } 
	} else {
	    fst_lock(jfst->fst);
	    fst_set_param( jfst->fst, atoi(key), lash_config_get_value_double(config) );
	    fst_unlock(jfst->fst);
	}
}

/* Return FALSE if want exit */
bool jfst_lash_idle(JFST *jfst) {
	if (! lash_enabled(lash_client))
		return false;

	lash_event_t *event;
	lash_config_t *config;

	while ((event = lash_get_event(lash_client))) {
		switch (lash_event_get_type(event)) {
		case LASH_Quit:
			lash_event_destroy(event);
			return false;
		case LASH_Restore_Data_Set:
			printf( "lash_restore... \n" );
			lash_send_event(lash_client, event);
			break;
		case LASH_Save_Data_Set:
			printf( "lash_save... \n" );
			jfst_lash_save(jfst);
			lash_send_event(lash_client, event);
			break;
		case LASH_Server_Lost:
			return false;
		default:
			printf("%s: receieved unknown LASH event of type %d",
				__FUNCTION__, lash_event_get_type(event));
			lash_event_destroy(event);
		}
	}

	while ((config = lash_get_config(lash_client))) {
		jfst_lash_restore(config, jfst);
		lash_config_destroy(config);
	}

	return true;
}

