#include <stdio.h>
#include "nsm.h"

static nsm_client_t *nsm = NULL;

static int cb_nsm_open (
			const char *name,
			const char *display_name,
			const char *client_id,
			char **out_msg,
			void *userdata
) {
//	do_open_stuff();
	printf("NSM: Open callback\n");
	return ERR_OK;
}

static int cb_nsm_save ( char **out_msg, void *userdata ) {
//	do_save_stuff();
	printf("NSM: Save callback\n");
	return ERR_OK;
}

void jvst_nsm_init(const char* client_name, const char* exec_name) {
	const char *nsm_url = getenv( "NSM_URL" );

	printf ("Initialize NSM ... NSM_URL:");
	if ( ! nsm_url ) {
		printf("is empty | FAIL\n");
		return;
	}
	
	printf("%s | OK\n", nsm_url);
	nsm = nsm_new();
	nsm_set_open_callback( nsm, cb_nsm_open, 0 );
	nsm_set_save_callback( nsm, cb_nsm_save, 0 );
	
	if ( ! nsm_init( nsm, nsm_url ) ) {
		nsm_send_announce( nsm, client_name, "", exec_name );
	} else {
		nsm_free( nsm );
		nsm = NULL;
	}
}
