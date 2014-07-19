#include <semaphore.h>
#include <errno.h>
#include <sys/mman.h>

#include "jfst.h"

#include <jack/thread.h>

#define RINGBUFFER_SIZE 16 * sizeof(struct MidiMessage)

/* fsthost.c */
extern void session_callback_aux( jack_session_event_t *event, void* arg );

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
	printf( "Connect: %s -> %s [ %s ]\n", source_port, destination_port, (ret==0)?"DONE":"FAIL" );
	return (ret==0) ? TRUE : FALSE;
}

void jvst_connect_audio(JackVST *jvst, const char *audio_to) {
	// Connect audio port
	const char **jports = jack_get_ports(jvst->client, audio_to, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
	if (!jports) {
		printf("Can't find any ports for %s\n", audio_to);
		return;
	}

	unsigned short i;
	for (i=0; jports[i] && i < jvst->numOuts; i++) {
		const char *pname = jack_port_name(jvst->outports[i]);
		jack_connect_wrap ( jvst->client, pname, jports[i] );
	}
	jack_free(jports);
}

void jvst_connect_midi_to_physical(JackVST* jvst) {
	const char **jports = jack_get_ports(jvst->client, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput|JackPortIsPhysical);
        if (!jports) return;

	const char *pname = jack_port_name(jvst->midi_inport);
	unsigned short i;
        for (i=0; jports[i]; i++)
		jack_connect_wrap (jvst->client, jports[i], pname);

	jack_free(jports);
}

void jvst_connect_to_ctrl_app(JackVST* jvst) {
	const char **jports = jack_get_ports(jvst->client, CTRLAPP, JACK_DEFAULT_MIDI_TYPE, 0);
	if (!jports) return;

	bool done = false;
	unsigned short i;
	for (i=0; jports[i]; i++) {
		const char *src, *dst;
		jack_port_t* port = jack_port_by_name(jvst->client, jports[i]);
		jack_port_t* my_port;
		if (jack_port_flags(port) & JackPortIsInput) {
			/* ctrl_out -> input */
			my_port = jvst->ctrl_outport;
			src = jack_port_name( my_port );
			dst = jports[i];
		} else if (jack_port_flags(port) & JackPortIsOutput) {
			/* output -> midi_in */
			my_port = jvst->midi_inport;
			src = jports[i];
			dst = jack_port_name( my_port );
		} else continue;

		/* Already connected ? */
		if ( jack_port_connected_to(my_port, jports[i]) ) continue;

		if ( jack_connect_wrap (jvst->client, src, dst) )
			done = true;
	}
        jack_free(jports);

	/* Now we are connected to CTRL APP - send announce */
	if (done) jvst_send_sysex(jvst, SYSEX_WANT_IDENT_REPLY);
}

static jack_port_t** jack_audio_port_init ( jack_client_t* client, unsigned long flags, int32_t num ) {
	jack_port_t** ports = malloc( sizeof(jack_port_t*) * num );
	mlock ( ports, sizeof(jack_port_t*) * num );

	int32_t i;
	for (i = 0; i < num; ++i) {
		char buf[16];
		snprintf (buf, sizeof(buf), "in%d", i+1);
		ports[i] = jack_port_register ( client, buf, JACK_DEFAULT_AUDIO_TYPE, flags, 0 );
	}
	return ports;
}

static int jvst_graph_order_callback( void *arg ) {
	JackVST* jvst = (JackVST*) arg;
	jvst->graph_order_change = TRUE;
	return 0;
}

static int process_callback ( jack_nframes_t nframes, void* data) {
	JackVST* jvst = (JackVST*) data;
	jvst_process( jvst, nframes );
	return 0;
}

static void jvst_log(const char *msg) { fprintf(stderr, "JACK: %s\n", msg); }

bool jvst_jack_init( JackVST* jvst, bool want_midi_out ) {

	jack_set_info_function(jvst_log);
	jack_set_error_function(jvst_log);

	printf("Starting Jack thread ... ");
	jack_status_t status;
	jvst->client = jack_client_open(jvst->client_name,JackSessionID,&status,jvst->uuid);
	if (! jvst->client) {
		jvst_log ("can't connect to JACK");
		return false;
	}
	puts("Done");

	/* Change client name if jack assign new */
	if (status & JackNameNotUnique) {
		jvst->client_name = jack_get_client_name(jvst->client);
		printf("Jack change our name to %s\n", jvst->client_name);
	}

	// Set client callbacks
	jack_set_thread_creator (wine_thread_create);
	jack_set_process_callback ( jvst->client, (JackProcessCallback) process_callback, jvst );
	jack_set_session_callback ( jvst->client, session_callback_aux, jvst );
	jack_set_graph_order_callback ( jvst->client, jvst_graph_order_callback, jvst );

	/* set rate and blocksize */
	jvst->sample_rate = jack_get_sample_rate ( jvst->client );
	jvst->buffer_size = jack_get_buffer_size ( jvst->client );

	// Register/allocate audio ports
	jvst->inports  = jack_audio_port_init ( jvst->client, JackPortIsInput,  jvst->numIns );
	jvst->outports = jack_audio_port_init ( jvst->client, JackPortIsOutput, jvst->numOuts );

	// Register MIDI input port (if needed)
	// NOTE: we always create midi_in port cause it works also as ctrl_in
	jvst->midi_inport = jack_port_register(jvst->client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	// Register MIDI output port (if needed)
	if ( want_midi_out ) {
		jvst->midi_outport = jack_port_register(jvst->client, "midi-out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
		jvst->ringbuffer = jack_ringbuffer_create(RINGBUFFER_SIZE);
		if (! jvst->ringbuffer) {
			jvst_log("Cannot create JACK ringbuffer.");
			return false;
		}
		jack_ringbuffer_mlock(jvst->ringbuffer);
	}

	// Register Control MIDI Output port
	jvst->ctrl_outport = jack_port_register(jvst->client, "ctrl-out",
		JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	return true;
}
