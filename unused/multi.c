#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <semaphore.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/thread.h>

#include "../fst/fst.h"
#include "../xmldb/info.h"

#define CHANNELS 2

typedef struct {
	FST** fst;
	int32_t num;
	jack_port_t* out[CHANNELS];
	jack_port_t* midi_in;
	float max[CHANNELS];
} FSTHOST;

volatile bool quit = false;
static void signal_handler (int signum) {
	switch(signum) {
	case SIGINT:
		puts("Caught signal to terminate (SIGINT)");
		quit = true;
		break;
	}
}

static void cmdline2arg(int *argc, char ***pargv, LPSTR cmdline) {
	LPWSTR* szArgList = CommandLineToArgvW(GetCommandLineW(), argc);
	if (!szArgList) {
		fputs("Unable to parse command line", stderr);
		*argc = -1;
		return;
	}

	char** argv = malloc(*argc * sizeof(char*));
	unsigned short i;
	for (i=0; i < *argc; ++i) {
		int nsize = WideCharToMultiByte(CP_UNIXCP, 0, szArgList[i], -1, NULL, 0, NULL, NULL);
		
		argv[i] = malloc( nsize );
		WideCharToMultiByte(CP_UNIXCP, 0, szArgList[i], -1, (LPSTR) argv[i], nsize, NULL, NULL);
	}
	LocalFree(szArgList);
	argv[0] = (char*) "fsthost_multi"; // Force APP name
	*pargv = argv;
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

void process( FST** fst, int num, jack_nframes_t nframes, float** out ) {
	float ins[num][CHANNELS][nframes];
	float outs[num][CHANNELS][nframes];

	memset ( ins, 0, sizeof ins );
	memset ( outs, 0, sizeof outs );

	int i, c;
	for ( i=0; i < num; i++ ) {
		/* Process AUDIO */
		int32_t num_ins = fst_num_ins(fst[i]);
		int32_t num_outs = fst_num_outs(fst[i]);

		float swap[nframes];
		memset ( swap, 0, sizeof swap );

		float* pi[num_ins];
		float* po[num_outs];

		for ( c=0; c < num_ins; c++ )
			pi[c] = ( c < CHANNELS) ? ins[i][c] : swap;

		for ( c=0; c < num_outs; c++ )
			po[c] = ( c < CHANNELS) ? outs[i][c] : swap;

//		printf( "INS: %d | OUTS: %d\n", num_ins, num_outs );

		fst_process( fst[i], pi, po, nframes );
	}

	jack_nframes_t f;
	for ( i=0; i < num; i++ )
		for ( c=0; c < CHANNELS; c++ )
			for ( f=0; f < nframes; f++ )
				out[c][f] += outs[i][c][f];
}

int process_cb_handler (jack_nframes_t frames, void* arg) {
	FSTHOST* fsthost = (FSTHOST*) arg;

	// NOTE: we process MIDI even in bypass mode for want_state handling and our SysEx
	void *port_buffer = jack_port_get_buffer( fsthost->midi_in, frames );
	jack_nframes_t num_jackevents = jack_midi_get_event_count( port_buffer );
	if ( num_jackevents == 0 ) goto no_midi_in;

	/* Allocate space for VST MIDI - preallocate space for all messages even
	   if we use only few - cause of alloca scope. Can't move this to separate
	   function because VST plug need this space during process call
	   The VstEvents structure already contains an array of 2 pointers to VstEvent
	   Can't use VLA here because of goto above
	*/
	// Pointers
	size_t size = sizeof(VstEvents);
	if ( num_jackevents > 2 )
		size += (num_jackevents - 2) * sizeof(VstEvent*);
	VstEvents* events = alloca( size );
	memset ( events, 0, sizeof(VstEvents) );

	// VstEvents
	VstMidiEvent* event_array = alloca ( num_jackevents * sizeof(VstMidiEvent) );
	memset ( event_array, 0, num_jackevents * sizeof(VstMidiEvent) );

	int32_t i;
	for (i = 0; i < num_jackevents; i++) {
		jack_midi_event_t jackevent;
		if ( jack_midi_event_get( &jackevent, port_buffer, i ) != 0 ) break;

		/* Bind MIDI event to collection */
		events->events[i] = (VstEvent*) &( event_array[i] );

		/* Prepare MIDI events */
		VstMidiEvent* me = (VstMidiEvent*) events->events[events->numEvents];
		me->type = kVstMidiType;
		me->byteSize = sizeof (VstMidiEvent);
		me->deltaFrames = jackevent.time;
		me->flags = kVstMidiEventIsRealtime; // All our MIDI data are realtime, it's clear ?

		uint8_t j;
		for (j=0; j < jackevent.size; j++)
			me->midiData[j] = jackevent.buffer[j];
		/* event_array[3] remain 0 (according to VST Spec) */
		me->midiData[3] = 0;

		events->numEvents++;
	}

	// ... let's the music play
	if ( events->numEvents > 0 )
		for ( i=0; i < fsthost->num; i++ )
			fst_process_events ( fsthost->fst[i], events );
no_midi_in: ;
	int c;
	float* outbuf[CHANNELS];
	for (c=0; c < CHANNELS; c++) {
		outbuf[c] = jack_port_get_buffer(fsthost->out[c], frames);
		memset( outbuf[c], 0, frames * sizeof(float) );
	}

	process ( fsthost->fst, fsthost->num, frames, outbuf );
	
	for ( c=0; c < CHANNELS; c++ ) {
		float max = 0;
		jack_nframes_t f;
		for ( f=0; f < frames; f++ )
			if ( outbuf[c][f] > max ) max = outbuf[c][f];
		fsthost->max[c] = max;
	}

	return 0;
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	int i;
	int	argc = -1;
	char**	argv = NULL;
	cmdline2arg(&argc, &argv, cmdline);

	if ( argc < 2 ) {
		printf ( "Usage: %s plugin ... ...\n", argv[0] );
		return 1;
	}

	jack_client_t* client = jack_client_open ( argv[0], JackNullOption, NULL);
	if ( ! client ) {
		fprintf ( stderr, "JACK server not running?\n" );
		return 1;
	}

	FSTHOST fsthost;
	fsthost.out[0] = jack_port_register ( client, "out_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0 );
	fsthost.out[1] = jack_port_register ( client, "out_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0 );
	fsthost.midi_in = jack_port_register( client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0 );

	// Handling signals
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &signal_handler;
	sigaction(SIGINT, &sa, NULL); // SIGINT for clean quit

	puts ( "...... LOADING ....." );
	FST* fst[argc];
	int loaded = 0;
	int fst_count = argc - 1;
	for ( i=0; i < fst_count; i++ ) {
		fst[i] = fst_info_load_open ( NULL, argv[i+1] );
		if (! fst[i]) goto exit;

		fst_run_editor(fst[i], false);
		loaded++;
	}

	fsthost.fst = fst;
	fsthost.num = loaded;
	jack_set_thread_creator (wine_thread_create);
	jack_set_process_callback(client, process_cb_handler, &fsthost); /* for jack1 */

        // Set block size / sample rate
	puts ( "...... CONFIGURE ....." );
	float sample_rate = 48000;
	intptr_t buffer_size = 1024;
	for ( i=0; i < loaded; i++ )
		fst_configure( fst[i], sample_rate, buffer_size );

	puts ( "...... RESUMING ....." );
	for ( i=0; i < loaded; i++ )
		fst_call ( fst[i], RESUME );

	jack_activate ( client );

	puts ("CTRL+C to cancel");
	puts ( "" );
	while ( ! quit ) {
		printf( "\r[0J" );
		int c;
		for ( c=0; c < CHANNELS; c++ )
			printf( "%2.4g | ", fsthost.max[c] );

		fst_event_callback();
		usleep ( 10000 );
	}

exit:
	jack_deactivate ( client );
	jack_client_close ( client );

	for ( i=0; i < loaded; i++ )
		fst_close(fst[i]);

	puts ( "DONE" );

	return 0;
}
