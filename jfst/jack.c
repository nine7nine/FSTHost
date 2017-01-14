#include <stdio.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <windows.h>

#include "log/log.h"
#include "jfst.h"

#include <jack/thread.h>

#define RINGBUFFER_SIZE 16 * sizeof(struct MidiMessage)

/* sysex.c */
extern bool jfst_sysex_jack_init ( JFST* jfst );

void jfst_set_volume(JFST* jfst, short volume) {
	if (jfst->volume != -1) jfst->volume = powf(volume / 63.0f, 2);
}

unsigned short jfst_get_volume(JFST* jfst) {
	if (jfst->volume == -1) return 0;

	short ret = roundf(sqrtf(jfst->volume) * 63.0f);

	return (ret < 0) ? 0 : (ret > 127) ? 127 : ret;
}

void jfst_apply_volume ( JFST* jfst, jack_nframes_t nframes, float** outs ) {
	if (jfst->volume == -1) return;

	int32_t i;
	for ( i = 0; i < jfst->numOuts; i++ ) {
		jack_nframes_t n;
		for ( n = 0; n < nframes; n++ )
			outs[i][n] *= jfst->volume;
	}
}

struct JackWineThread {
	void* (*func)(void*);
	void* arg;
	pthread_t pthid;
	sem_t sema;
};

static DWORD WINAPI
wine_thread_aux( LPVOID arg ) {
	struct JackWineThread* jwt = (struct JackWineThread*) arg;

	fst_show_thread_info ( "Audio" );

	void* func_arg = jwt->arg;
	void* (*func)(void*) = jwt->func;

	jwt->pthid = pthread_self();
	sem_post( &jwt->sema );

	func ( func_arg );

	return 0;
}

static int
wine_thread_create (pthread_t* thread_id, const pthread_attr_t* attr, void *(*function)(void*), void* arg) {
	struct JackWineThread jwt;

	sem_init( &jwt.sema, 0, 0 );
	jwt.func = function;
	jwt.arg = arg;

	CreateThread( NULL, 0, wine_thread_aux, &jwt, 0, 0 );
	sem_wait( &jwt.sema );

	*thread_id = jwt.pthid;
	return 0;
}

static bool jack_connect_wrap ( jack_client_t* client , const char* source_port, const char* destination_port ) {
	int ret = jack_connect( client, source_port, destination_port );
	if ( ret == EEXIST ) return FALSE;
	log_info( "Connect: %s -> %s [ %s ]",
		source_port, destination_port,
		(ret==0) ? "DONE" : "FAIL"
	);
	return (ret==0) ? TRUE : FALSE;
}

void jfst_connect_audio(JFST *jfst, const char *audio_to) {
	unsigned long flags = JackPortIsInput;

	if ( audio_to ) { // ! mean don't connect
		if ( !strcmp(audio_to,"!") ) return;
	} else { // NULL mean connect to first physical
		audio_to = "";
		flags |= JackPortIsPhysical;
	}

	// Connect audio port
	const char **jports = jack_get_ports(jfst->client, audio_to, JACK_DEFAULT_AUDIO_TYPE, flags);
	if (!jports) {
		log_error("Can't find any ports for %s", audio_to);
		return;
	}

	unsigned short i;
	for (i=0; jports[i] && i < jfst->numOuts; i++) {
		const char *pname = jack_port_name(jfst->outports[i]);
		jack_connect_wrap ( jfst->client, pname, jports[i] );
	}
	jack_free(jports);
}

void jfst_connect_midi_to_physical(JFST* jfst) {
	const char **jports = jack_get_ports(jfst->client, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput|JackPortIsPhysical);
        if (!jports) return;

	const char *pname = jack_port_name(jfst->midi_inport);
	unsigned short i;
        for (i=0; jports[i]; i++)
		jack_connect_wrap (jfst->client, jports[i], pname);

	jack_free(jports);
}

void jfst_connect_to_ctrl_app(JFST* jfst) {
	const char **jports = jack_get_ports(jfst->client, CTRLAPP, JACK_DEFAULT_MIDI_TYPE, 0);
	if (!jports) return;

	bool done = false;
	unsigned short i;
	for (i=0; jports[i]; i++) {
		const char *src, *dst;
		jack_port_t* port = jack_port_by_name(jfst->client, jports[i]);
		jack_port_t* my_port;
		if (jack_port_flags(port) & JackPortIsInput) {
			/* ctrl_out -> input */
			my_port = jfst->ctrl_outport;
			src = jack_port_name( my_port );
			dst = jports[i];
		} else if (jack_port_flags(port) & JackPortIsOutput) {
			/* output -> midi_in */
			my_port = jfst->midi_inport;
			src = jports[i];
			dst = jack_port_name( my_port );
		} else continue;

		/* Already connected ? */
		if ( jack_port_connected_to(my_port, jports[i]) ) continue;

		if ( jack_connect_wrap (jfst->client, src, dst) )
			done = true;
	}
        jack_free(jports);

	/* Now we are connected to CTRL APP - send announce */
	if (done) jfst_send_sysex(jfst, SYSEX_TYPE_REPLY);
}

static jack_port_t** jack_audio_port_init ( jack_client_t* client, const char* prefix, unsigned long flags, int32_t num ) {
	jack_port_t** ports = malloc( sizeof(jack_port_t*) * num );
	mlock ( ports, sizeof(jack_port_t*) * num );

	int32_t i;
	for (i = 0; i < num; i++) {
		char buf[16];
		snprintf (buf, sizeof(buf), "%s%d", prefix, i+1);
		ports[i] = jack_port_register ( client, buf, JACK_DEFAULT_AUDIO_TYPE, flags, 0 );
	}
	return ports;
}

static int graph_order_callback( void *arg ) {
	JFST* jfst = (JFST*) arg;
	event_queue_send_val ( &jfst->event_queue, EVENT_GRAPH, 0 );
	return 0;
}

static void session_callback( jack_session_event_t *event, void* arg ) {
        JFST* jfst = (JFST*) arg;
	event_queue_send_ptr ( &jfst->event_queue, EVENT_SESSION, event );
}

static int process_callback ( jack_nframes_t nframes, void* data) {
	JFST* jfst = (JFST*) data;
	jfst_process( jfst, nframes );
	return 0;
}

static int buffer_size_callback( jack_nframes_t new_buf_size, void *arg ) {
	JFST* jfst = (JFST*) arg;
	jfst->buffer_size = new_buf_size;
	fst_configure( jfst->fst, jfst->sample_rate, jfst->buffer_size );
	return 0;
}

static int srate_callback ( jack_nframes_t new_srate, void *arg ) {
	JFST* jfst = (JFST*) arg;
	jfst->sample_rate = new_srate;
	fst_configure( jfst->fst, jfst->sample_rate, jfst->buffer_size );
	return 0;
}

static void jfst_log(const char *msg) { log_error( "JACK: %s", msg); }

bool jfst_jack_init( JFST* jfst, bool want_midi_out ) {

	jack_set_info_function(jfst_log);
	jack_set_error_function(jfst_log);

	log_info("Starting Jack thread");
	jack_status_t status;
	jfst->client = jack_client_open(jfst->client_name,JackSessionID,&status,jfst->uuid);
	if (! jfst->client) {
		log_error ("can't connect to JACK");
		return false;
	}

	/* Change client name if jack assign new */
	if (status & JackNameNotUnique) {
		jfst->client_name = jack_get_client_name(jfst->client);
		log_info("Jack change our name to %s", jfst->client_name);
	}

	// Set client callbacks
	jack_set_thread_creator (wine_thread_create);
	jack_set_process_callback ( jfst->client, (JackProcessCallback) process_callback, jfst );
	jack_set_session_callback ( jfst->client, session_callback, jfst );
	jack_set_graph_order_callback ( jfst->client, graph_order_callback, jfst );
	jack_set_buffer_size_callback ( jfst->client, buffer_size_callback, jfst );
	jack_set_sample_rate_callback ( jfst->client, srate_callback, jfst );

	/* set rate and blocksize */
	jfst->sample_rate = jack_get_sample_rate ( jfst->client );
	jfst->buffer_size = jack_get_buffer_size ( jfst->client );

	// Register/allocate audio ports
	jfst->inports  = jack_audio_port_init ( jfst->client, "in",  JackPortIsInput,  jfst->numIns );
	jfst->outports = jack_audio_port_init ( jfst->client, "out", JackPortIsOutput, jfst->numOuts );

	// Register MIDI input port (if needed)
	// NOTE: we always create midi_in port cause it works also as ctrl_in
	jfst->midi_inport = jack_port_register(jfst->client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	// Register MIDI output port (if needed)
	if ( want_midi_out ) {
		jfst->midi_outport = jack_port_register(jfst->client, "midi-out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
		jfst->ringbuffer = jack_ringbuffer_create(RINGBUFFER_SIZE);
		if (! jfst->ringbuffer) {
			log_error("Cannot create JACK ringbuffer.");
			return false;
		}
		jack_ringbuffer_mlock(jfst->ringbuffer);
	}

	// Register Control MIDI Output port
	jfst->ctrl_outport = jack_port_register(jfst->client, "ctrl-out",
		JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	/* Sysex init */
        if ( ! jfst_sysex_jack_init ( jfst ) ) return false;

	return true;
}
