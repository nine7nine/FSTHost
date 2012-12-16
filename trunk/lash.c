#include <lash/lash.h>
#include "jackvst.h"

static lash_client_t *lash_client = NULL;

void jvst_lash_init(JackVST *jvst, int* argc, char** argv[]) {
	lash_event_t* event;
	lash_args_t* lash_args = lash_extract_args(argc, argv);

	int flags = LASH_Config_Data_Set;

	lash_client = lash_init(lash_args, jvst->client_name, flags, LASH_PROTOCOL(2, 0));

	if (!lash_client) {
		fprintf(stderr, "%s: could not initialise lash\n", __FUNCTION__);
		fprintf(stderr, "%s: running fsthost without lash session-support\n", __FUNCTION__);
		fprintf(stderr, "%s: to enable lash session-support launch the lash server prior fsthost\n",
			__FUNCTION__);
		return;
	}

	if (lash_enabled(lash_client))
		return;

	event = lash_event_new_with_type(LASH_Client_Name);
	lash_event_set_string(event, jvst->client_name);
	lash_send_event(lash_client, event);

	event = lash_event_new_with_type(LASH_Jack_Client_Name);
	lash_event_set_string(event, jvst->client_name);
	lash_send_event(lash_client, event);
}

static void
jvst_lash_save(JackVST *jvst) {
	unsigned short i;
	size_t bytelen;
	lash_config_t *config;
	void *chunk;

	for( i=0; i<jvst->fst->plugin->numParams; i++ ) {
	    char buf[10];
	    float param;
	    
	    snprintf( buf, 9, "%d", i );

	    config = lash_config_new_with_key( buf );

	    pthread_mutex_lock( &jvst->fst->lock );
	    param = jvst->fst->plugin->getParameter( jvst->fst->plugin, i ); 
	    pthread_mutex_unlock( &jvst->fst->lock );

	    lash_config_set_value_double(config, param);
	    lash_send_config(lash_client, config);
	    //lash_config_destroy( config );
	}

	for( i=0; i<128; i++ ) {
	    char buf[16];
	    
	    snprintf( buf, 15, "midi_map%d", i );
	    config = lash_config_new_with_key( buf );
	    lash_config_set_value_int(config, jvst->midi_map[i]);
	    lash_send_config(lash_client, config);
	    //lash_config_destroy( config );
	}

	if ( jvst->fst->plugin->flags & effFlagsProgramChunks ) {
	    // TODO: calling from this thread is wrong.
	    //       is should move it to fst gui thread.
	    printf( "getting chunk...\n" );

	    // XXX: alternative. call using the fst->lock
	    //pthread_mutex_lock( &(fst->lock) );
	    //bytelen = jvst->fst->plugin->dispatcher( jvst->fst->plugin, 23, 0, 0, &chunk, 0 );
	    //pthread_mutex_unlock( &(fst->lock) );

	    bytelen = fst_call_dispatcher( jvst->fst, effGetChunk, 0, 0, &chunk, 0 );
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
jvst_lash_restore(lash_config_t *config, JackVST *jvst ) {
	const char *key;

	key = lash_config_get_key(config);

	if (strncmp(key, "midi_map", strlen( "midi_map")) == 0) {
	    short cc = atoi( key+strlen("midi_map") );
	    int param = lash_config_get_value_int( config );

	    if( cc < 0 || cc>=128 || param<0 || param>=jvst->fst->plugin->numParams ) 
		return;

	    jvst->midi_map[cc] = param;
	    return;
	}

	if ( jvst->fst->plugin->flags & effFlagsProgramChunks) {
	    if (strcmp(key, "bin_chunk") == 0) {
		fst_call_dispatcher( jvst->fst, effSetChunk, 0, lash_config_get_value_size( config ), 
			(void *) lash_config_get_value( config ), 0 );
		return;
	    } 
	} else {
	    pthread_mutex_lock( &jvst->fst->lock );
	    jvst->fst->plugin->setParameter( jvst->fst->plugin, atoi( key ), 
		lash_config_get_value_double( config ) );
	    pthread_mutex_unlock( &jvst->fst->lock );
	}
}

/* Return FALSE if want exit */
void jvst_lash_idle(JackVST *jvst, bool *quit) {
	if (! lash_enabled(lash_client))
		return;

	lash_event_t *event;
	lash_config_t *config;

	while ((event = lash_get_event(lash_client))) {
		switch (lash_event_get_type(event)) {
		case LASH_Quit:
			lash_event_destroy(event);
			*quit = TRUE;
			return;
		case LASH_Restore_Data_Set:
			printf( "lash_restore... \n" );
			lash_send_event(lash_client, event);
			break;
		case LASH_Save_Data_Set:
			printf( "lash_save... \n" );
			jvst_lash_save(jvst);
			lash_send_event(lash_client, event);
			break;
		case LASH_Server_Lost:
			return;
		default:
			printf("%s: receieved unknown LASH event of type %d",
				__FUNCTION__, lash_event_get_type(event));
			lash_event_destroy(event);
		}
	}

	while ((config = lash_get_config(lash_client))) {
		jvst_lash_restore(config, jvst);
		lash_config_destroy(config);
	}
}

