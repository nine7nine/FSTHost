#include <stdio.h>
#include "jfst.h"

#define SYSEX_RINGBUFFER_SIZE 16 * SYSEX_MAX_SIZE

static void sysex_makeASCII(uint8_t* ascii_midi_dest, char* name, size_t size_dest) {
	size_t i;
	for (i=0; i < strlen(name) && i < size_dest - 1; i++)
		if ( isprint( toascii( name[i]) ) )
			ascii_midi_dest[i] = name[i];
	memset(ascii_midi_dest + i, 0, size_dest - i - 1); /* Set rest to 0 */
}

bool jvst_sysex_init ( JackVST* jvst ) {
	// Little trick (const entries)
	SysExIdentReply sxir = SYSEX_IDENT_REPLY;
	memcpy(&jvst->sysex_ident_reply, &sxir, sizeof(SysExIdentReply));

	SysExDumpV1 sxd = SYSEX_DUMP;
	memcpy(&jvst->sysex_dump, &sxd, sizeof(SysExDumpV1));

	/* Init MIDI Input sysex buffer */
	jvst->sysex_ringbuffer = jack_ringbuffer_create(SYSEX_RINGBUFFER_SIZE);
	if (! jvst->sysex_ringbuffer) {
		fst_error("Cannot create JACK ringbuffer.");
		return false;
	}
	jack_ringbuffer_mlock(jvst->sysex_ringbuffer);

	/* Generate random ID */
	srand(GetTickCount()); /* Init ramdom generator */
	printf("Random SysEx ID:");
	unsigned short g;
	for(g=0; g < sizeof(jvst->sysex_ident_reply.version); g++) {
		jvst->sysex_ident_reply.version[g] = rand() % 128;
		printf(" %02X", jvst->sysex_ident_reply.version[g]);
	}
	putchar('\n');

	return true;
}

void jvst_sysex_set_uuid (JackVST* jvst, uint8_t uuid) {
	jvst->sysex_ident_reply.model[0] = jvst->sysex_dump.uuid = uuid;
}

void jvst_sysex_rt_send ( JackVST* jvst, void *port_buffer ) {
	if (jvst->sysex_want == SYSEX_WANT_NO) return;

	// Are our lock is ready for us ?
	// If not then we try next time
	if (pthread_mutex_trylock(&jvst->sysex_lock) != 0) return;

	size_t sysex_size;
	jack_midi_data_t* sysex_data;
	SysExDone sxd = SYSEX_DONE;
	switch(jvst->sysex_want) {
	case SYSEX_WANT_IDENT_REPLY:
		sysex_data = (jack_midi_data_t*) &jvst->sysex_ident_reply;
		sysex_size = sizeof(SysExIdentReply);
		break;
	case SYSEX_WANT_DUMP:
		sysex_data = (jack_midi_data_t*) &jvst->sysex_dump;
		sysex_size = sizeof(SysExDumpV1);
		break;
	case SYSEX_WANT_DONE:
		sxd.uuid = jvst->sysex_ident_reply.model[0];
		sysex_data = (jack_midi_data_t*) &sxd;
		sysex_size = sizeof(SysExDone);
		break;
	default: goto pco_ret; // error - skip processing for now
	}

	/* Note: we always send sysex on first frame */
	if ( jack_midi_event_write(port_buffer, 0, sysex_data, sysex_size) )
		fst_error("SysEx error: jack_midi_event_write failed.");
	jvst->sysex_want = SYSEX_WANT_NO;
	
pco_ret:
	pthread_cond_signal(&jvst->sysex_sent);
	pthread_mutex_unlock(&jvst->sysex_lock);
}

// Prepare data for RT thread and wait for send
void jvst_send_sysex(JackVST* jvst, enum SysExWant sysex_want) {
	/* Do not send anything if we are not connected */
	if (! jack_port_connected ( jvst->ctrl_outport  ) ) return;

	pthread_mutex_lock (&jvst->sysex_lock);

	uint8_t id = 0;
	switch ( sysex_want ) {
	case SYSEX_WANT_DUMP:;
		char progName[32];
		SysExDumpV1* sxd = &jvst->sysex_dump;
		fst_get_program_name(jvst->fst, jvst->fst->current_program, progName, sizeof(progName));

//		sxd->uuid = ; /* Set once on start */
		sxd->program = jvst->fst->current_program;
		sxd->channel = midi_filter_one_channel_get( &jvst->channel );
		midi_filter_one_channel_set( &jvst->channel, sxd->channel );
		sxd->volume = jvst_get_volume(jvst);
		sxd->state = (jvst->bypassed) ? SYSEX_STATE_NOACTIVE : SYSEX_STATE_ACTIVE;
		sysex_makeASCII(sxd->program_name, progName, 24);
		sysex_makeASCII(sxd->plugin_name, jvst->client_name, 24);
		id = sxd->uuid;
		break;
	case SYSEX_WANT_IDENT_REPLY:
	case SYSEX_WANT_DONE:
		id = jvst->sysex_ident_reply.model[0];
		break;
	case SYSEX_WANT_NO: /* ERROR */
		break;
	}

	jvst->sysex_want = sysex_want;
	pthread_cond_wait (&jvst->sysex_sent, &jvst->sysex_lock);
	pthread_mutex_unlock (&jvst->sysex_lock);
	printf("SysEx Sent %s ID:%d\n", SysExType2str(sysex_want), id);
}

void jvst_queue_sysex(JackVST* jvst, jack_midi_data_t* data, size_t size) {
	if ( size > SYSEX_MAX_SIZE ) {
		fst_error("Sysex is too big. Skip. Requested %d, but MAX is %d", size, SYSEX_MAX_SIZE);
		return;
	}

	jack_ringbuffer_t* rb = jvst->sysex_ringbuffer;
	if (jack_ringbuffer_write_space(rb) < size + sizeof(size)) {
		fst_error("No space in SysexInput buffer");
	} else {
		// Size of message
		jack_ringbuffer_write(rb, (char*) &size, sizeof size);
		// Message itself
		jack_ringbuffer_write(rb, (char*) data, size);
	}
}

/* Process Sysex messages in non-realtime thread */
static inline void
jvst_parse_sysex_input(JackVST* jvst, jack_midi_data_t* data, size_t size) {
	switch(data[1]) {
	case SYSEX_MYID:;
		SysExHeader* sysex = (SysExHeader*) data;

		// Our sysex
		printf( "Got Our SysEx Version:%d Type:%s ID:%d - ",
			sysex->version,
			SysExType2str( sysex->type ),
			sysex->uuid
		);

		if ( sysex->version != 1 ) {
			puts("not supported");
			break;
		}

		if ( sysex->type != SYSEX_TYPE_OFFER &&
		     sysex->uuid != jvst->sysex_dump.uuid
		) {
			puts("Not to Us");
			break;
		}

		switch( sysex->type ) {
		case SYSEX_TYPE_DUMP: ;
			SysExDumpV1* sd = (SysExDumpV1*) sysex;
		
			printf("OK | state:%d program:%d channel:%d volume:%d\n",
				sd->state, sd->program, sd->channel, sd->volume);
			jvst_bypass(jvst, (sd->state == SYSEX_STATE_ACTIVE) ? FALSE : TRUE);
			fst_program_change(jvst->fst, sd->program);
			midi_filter_one_channel_set(&jvst->channel, sd->channel);
			jvst_set_volume(jvst, sd->volume);

			// Copy sysex state for preserve resending SysEx Dump
			memcpy(&jvst->sysex_dump,sd,sizeof(SysExDumpV1));

			jvst_send_sysex(jvst, SYSEX_WANT_DONE);
			break;
		case SYSEX_TYPE_RQST:
			puts("OK");
			jvst_send_sysex(jvst, SYSEX_WANT_DUMP);
			/* If we got DumpRequest then it mean FHControl is here and we wanna notify */
			jvst->sysex_want_notify = true;
			break;
		case SYSEX_TYPE_OFFER: ;
			SysExIdOffer* sysex_id_offer = (SysExIdOffer*) sysex;
			printf("RndID:");
			unsigned short g = 0;
			while ( g < sizeof(sysex_id_offer->rnid) ) printf(" %02X", sysex_id_offer->rnid[g++]);
			printf(" - ");

			if (jvst->sysex_ident_reply.model[0] != SYSEX_AUTO_ID) {
				puts("UNEXPECTED");
			} else if (memcmp(sysex_id_offer->rnid, jvst->sysex_ident_reply.version, 
			     sizeof(jvst->sysex_ident_reply.version)*sizeof(uint8_t)) == 0)
			{
				puts("OK");
				jvst_sysex_set_uuid( jvst, sysex_id_offer->uuid );
				jvst_send_sysex(jvst, SYSEX_WANT_IDENT_REPLY);
			} else {
				puts("NOT FOR US");
			}
			break;
		case SYSEX_TYPE_RELOAD: ;
			jvst_load_state ( jvst, NULL );
			break;
		default:
			puts("BROKEN");
		}
		break;
	case SYSEX_NON_REALTIME:
		// Identity request
		if (size >= sizeof(SysExIdentRqst)) {
			// TODO: for now we just always answer ;-)
			SysExIdentRqst sxir = SYSEX_IDENT_REQUEST;
			data[2] = 0x7F; // veil
			if ( memcmp(data, &sxir, sizeof(SysExIdentRqst) ) == 0) {
				puts("Got Identity request");
				jvst_send_sysex(jvst, SYSEX_WANT_IDENT_REPLY);
			}
		}
		break;
	}
}

void jvst_sysex_handler(JackVST* jvst) {
	jack_ringbuffer_t* rb = jvst->sysex_ringbuffer;
	/* Send our queued messages */
	while (jack_ringbuffer_read_space(rb)) {
		size_t size;
		jack_ringbuffer_read(rb, (char*) &size, sizeof size);

                jack_midi_data_t tmpbuf[size];
		jack_ringbuffer_read(rb, (char*) &tmpbuf, size);

		jvst_parse_sysex_input(jvst, (jack_midi_data_t *) &tmpbuf, size);
        }
}

void jvst_sysex_notify(JackVST* jvst) {
	// Wait until program change
	if (jvst->fst->event_call.type == PROGRAM_CHANGE) return;
	// Do not notify if have not SysEx ID
	if (jvst->sysex_ident_reply.model[0] == SYSEX_AUTO_ID) return;

	SysExDumpV1* d = &jvst->sysex_dump;
	if ( d->program != jvst->fst->current_program ||
		d->channel != midi_filter_one_channel_get( &jvst->channel ) ||
		d->state   != ( (jvst->bypassed) ? SYSEX_STATE_NOACTIVE : SYSEX_STATE_ACTIVE ) ||
		d->volume  != jvst_get_volume(jvst)
	) jvst_send_sysex(jvst, SYSEX_WANT_DUMP);
}
