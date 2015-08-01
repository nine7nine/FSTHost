#include <stdio.h>
#include "log/log.h"
#include "jfst.h"

#define SYSEX_RINGBUFFER_SIZE 16 * SYSEX_MAX_SIZE

static void sysex_makeASCII(uint8_t* ascii_midi_dest, char* name, size_t size_dest) {
	size_t i;
	for (i=0; i < strlen(name) && i < size_dest - 1; i++)
		if ( isprint( toascii( name[i]) ) )
			ascii_midi_dest[i] = name[i];
	memset(ascii_midi_dest + i, 0, size_dest - i - 1); /* Set rest to 0 */
}

static void cat_bin2hexstr ( char* str, uint8_t* bin, size_t bin_size ) {
	char ret[ 3 * bin_size + 1];
	char* p = ret;
	int i;
	for ( i = 0; i < bin_size; i++ ) {
		sprintf(p, " %02X", bin[i]);
		p += 3;
	}
	strcat( str, ret );
}

void jfst_sysex_init ( JFST* jfst ) {
	// Init Sysex structures - little trick (const entries)
	SysExIdentReply sxir = SYSEX_IDENT_REPLY;
	memcpy(&jfst->sysex_ident_reply, &sxir, sizeof(SysExIdentReply));

	SysExDumpV1 sxd = SYSEX_DUMP;
	memcpy(&jfst->sysex_dump, &sxd, sizeof(SysExDumpV1));
}

void jfst_sysex_gen_random_id ( JFST* jfst ) {

	srand(GetTickCount()); /* Init ramdom generator */
	unsigned short g;
	size_t size = sizeof jfst->sysex_ident_reply.version;
	for(g=0; g < size; g++)
		jfst->sysex_ident_reply.version[g] = rand() % 128;

	char strID[3 * size + 1];
	strID[0] = '\0';
	cat_bin2hexstr( strID, jfst->sysex_ident_reply.version, size );
	log_info("Random SysEx ID:%s", strID);
}

bool jfst_sysex_jack_init ( JFST* jfst ) {
	/* Init MIDI Input sysex buffer */
	jfst->sysex_ringbuffer = jack_ringbuffer_create(SYSEX_RINGBUFFER_SIZE);
	if (! jfst->sysex_ringbuffer) {
		log_error("Cannot create JACK ringbuffer.");
		return false;
	}
	jack_ringbuffer_mlock(jfst->sysex_ringbuffer);
	return true;
}

void jfst_sysex_set_uuid (JFST* jfst, uint8_t uuid) {
	jfst->sysex_ident_reply.model[0] = jfst->sysex_dump.uuid = uuid;
}

void jfst_sysex_rt_send ( JFST* jfst, void *port_buffer ) {
	if (jfst->sysex_want == SYSEX_TYPE_NONE) return;

	// Are our lock is ready for us ?
	// If not then we try next time
	if (pthread_mutex_trylock(&jfst->sysex_lock) != 0) return;

	size_t sysex_size;
	jack_midi_data_t* sysex_data;
	SysExDone sxd = SYSEX_DONE;
	switch(jfst->sysex_want) {
	case SYSEX_TYPE_REPLY:
		sysex_data = (jack_midi_data_t*) &jfst->sysex_ident_reply;
		sysex_size = sizeof(SysExIdentReply);
		break;
	case SYSEX_TYPE_DUMP:
		sysex_data = (jack_midi_data_t*) &jfst->sysex_dump;
		sysex_size = sizeof(SysExDumpV1);
		break;
	case SYSEX_TYPE_DONE:
		sxd.uuid = jfst->sysex_ident_reply.model[0];
		sysex_data = (jack_midi_data_t*) &sxd;
		sysex_size = sizeof(SysExDone);
		break;
	default: goto pco_ret; // error - skip processing for now
	}

	/* Note: we always send sysex on first frame */
	if ( jack_midi_event_write(port_buffer, 0, sysex_data, sysex_size) )
		log_error("SysEx error: jack_midi_event_write failed.");
	jfst->sysex_want = SYSEX_TYPE_NONE;
	
pco_ret:
	pthread_cond_signal(&jfst->sysex_sent);
	pthread_mutex_unlock(&jfst->sysex_lock);
}

// Prepare data for RT thread and wait for send
void jfst_send_sysex(JFST* jfst, SysExType type) {
	/* Do not send anything if we are not connected */
	if (! jack_port_connected ( jfst->ctrl_outport  ) ) return;

	pthread_mutex_lock (&jfst->sysex_lock);

	uint8_t id = 0;
	switch ( type ) {
	case SYSEX_TYPE_DUMP:;
		SysExDumpV1* sxd = &jfst->sysex_dump;

		char progName[FST_MAX_PROG_NAME];
		fst_get_program_name(jfst->fst, jfst->fst->current_program, progName, sizeof(progName));

//		sxd->uuid = ; /* Set once on start */
		sxd->program = jfst->fst->current_program;
		sxd->channel = midi_filter_one_channel_get( &jfst->channel );
		midi_filter_one_channel_set( &jfst->channel, sxd->channel );
		sxd->volume = jfst_get_volume(jfst);
		sxd->state = (jfst->bypassed) ? SYSEX_STATE_NOACTIVE : SYSEX_STATE_ACTIVE;
		sysex_makeASCII(sxd->program_name, progName, 24);
		sysex_makeASCII(sxd->plugin_name, jfst->client_name, 24);
		id = sxd->uuid;
		break;
	case SYSEX_TYPE_REPLY:
	case SYSEX_TYPE_DONE:
		id = jfst->sysex_ident_reply.model[0];
		break;
	default:
		log_error("SysEx Type:%s ID:%d not supprted to send", SysExType2str(type), id);
		pthread_mutex_unlock (&jfst->sysex_lock);
		return;
	}

	jfst->sysex_want = type;
	pthread_cond_wait (&jfst->sysex_sent, &jfst->sysex_lock);
	pthread_mutex_unlock (&jfst->sysex_lock);
	log_info("SysEx Sent Type:%s ID:%d\n", SysExType2str(type), id);
}

void jfst_queue_sysex(JFST* jfst, jack_midi_data_t* data, size_t size) {
	if ( size > SYSEX_MAX_SIZE ) {
		log_error("Sysex is too big. Skip. Requested %d, but MAX is %d", size, SYSEX_MAX_SIZE);
		return;
	}

	jack_ringbuffer_t* rb = jfst->sysex_ringbuffer;
	if (jack_ringbuffer_write_space(rb) < size + sizeof(size)) {
		log_error("No space in SysexInput buffer");
	} else {
		// Size of message
		jack_ringbuffer_write(rb, (char*) &size, sizeof size);
		// Message itself
		jack_ringbuffer_write(rb, (char*) data, size);
	}
}

static void jfst_sync2sysex( JFST* jfst ) {
	SysExDumpV1* sd = (SysExDumpV1*) &jfst->sysex_dump;

	jfst_bypass(jfst, (sd->state == SYSEX_STATE_ACTIVE) ? FALSE : TRUE);
	fst_program_change(jfst->fst, sd->program);
	midi_filter_one_channel_set(&jfst->channel, sd->channel);
	jfst_set_volume(jfst, sd->volume);
}

/* Process Sysex messages in non-realtime thread */
static inline void
jfst_parse_sysex_input(JFST* jfst, jack_midi_data_t* data, size_t size) {
	char tmpstr[128];

	switch(data[1]) {
	case SYSEX_MYID:;
		SysExHeader* sysex = (SysExHeader*) data;

		// Our sysex
		sprintf( tmpstr, "SysEx Received Type:%s Version:%d ID:%d - ",
			SysExType2str( sysex->type ),
			sysex->version,
			sysex->uuid
		);

		if ( sysex->version != 1 ) {
			log_info( "%snot supported", tmpstr );
			break;
		}

		if ( sysex->type != SYSEX_TYPE_OFFER &&
		     sysex->uuid != jfst->sysex_dump.uuid
		) {
			log_info ( "%sNot to Us", tmpstr );
			break;
		}

		switch( sysex->type ) {
		case SYSEX_TYPE_DUMP: ;
			SysExDumpV1* sd = (SysExDumpV1*) sysex;

			log_info("%sOK | state:%d program:%d channel:%d volume:%d\n",
				tmpstr, sd->state, sd->program, sd->channel, sd->volume);		

			// Copy sysex state for preserve resending SysEx Dump
			memcpy(&jfst->sysex_dump,sd,sizeof(SysExDumpV1));
			jfst_sync2sysex( jfst );

			jfst_send_sysex(jfst, SYSEX_TYPE_DONE);
			break;
		case SYSEX_TYPE_RQST:
			log_info("%sOK", tmpstr);
			jfst_send_sysex(jfst, SYSEX_TYPE_DUMP);
			/* If we got DumpRequest then it mean FHControl is here and we wanna notify */
			jfst->sysex_want_notify = true;
			break;
		case SYSEX_TYPE_OFFER: ;
			SysExIdOffer* sysex_id_offer = (SysExIdOffer*) sysex;
			size_t size = sizeof(sysex_id_offer->rnid);

			strcat(tmpstr, "RndID:");
			cat_bin2hexstr ( tmpstr, sysex_id_offer->rnid, size );
			strcat(tmpstr, " - ");

			if (jfst->sysex_ident_reply.model[0] != SYSEX_AUTO_ID) {
				log_info("%sUNEXPECTED", tmpstr);
			} else if (memcmp(sysex_id_offer->rnid, jfst->sysex_ident_reply.version, 
			     sizeof(jfst->sysex_ident_reply.version)*sizeof(uint8_t)) == 0)
			{
				log_info("%sOK", tmpstr);
				jfst_sysex_set_uuid( jfst, sysex_id_offer->uuid );
				jfst_send_sysex(jfst, SYSEX_TYPE_REPLY);
			} else {
				log_info("%sNOT FOR US", tmpstr);
			}
			break;
		case SYSEX_TYPE_RELOAD: ;
			log_info("%sOK", tmpstr);
			jfst_load_state ( jfst, NULL );
			jfst_sync2sysex( jfst );
			break;
		default:
			log_info("%sBROKEN", tmpstr);
		}
		break;
	case SYSEX_NON_REALTIME:
		// Identity request
		if (size >= sizeof(SysExIdentRqst)) {
			// TODO: for now we just always answer ;-)
			SysExIdentRqst sxir = SYSEX_IDENT_REQUEST;
			data[2] = 0x7F; // veil
			if ( memcmp(data, &sxir, sizeof(SysExIdentRqst) ) == 0) {
				log_info("Got Identity request");
				jfst_send_sysex(jfst, SYSEX_TYPE_REPLY);
			}
		}
		break;
	}
}

void jfst_sysex_handler(JFST* jfst) {
	jack_ringbuffer_t* rb = jfst->sysex_ringbuffer;
	/* Send our queued messages */
	while (jack_ringbuffer_read_space(rb)) {
		size_t size;
		jack_ringbuffer_read(rb, (char*) &size, sizeof size);

                jack_midi_data_t tmpbuf[size];
		jack_ringbuffer_read(rb, (char*) &tmpbuf, size);

		jfst_parse_sysex_input(jfst, (jack_midi_data_t *) &tmpbuf, size);
        }
}

void jfst_sysex_notify(JFST* jfst) {
	// Do not notify if have not SysEx ID
	if (jfst->sysex_ident_reply.model[0] == SYSEX_AUTO_ID)
		return;

	jfst_send_sysex(jfst, SYSEX_TYPE_DUMP);
}
