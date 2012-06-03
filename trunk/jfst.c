/*
    Copyright (C) 2004 Paul Davis <paul@linuxaudiosystems.com> 
                       Torben Hohn <torbenh@informatik.uni-bremen.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <glib.h>
#include <semaphore.h>
#include <signal.h>
#include <vestige/aeffectx.h>
#include <windows.h>

#include "fst.h"

#include "jackvst.h"
#include "jack/midiport.h"

#ifdef HAVE_LASH
#include <lash/lash.h>
#endif

/* audiomaster.c */

extern long jack_host_callback (struct AEffect*, int32_t, int32_t, intptr_t, void *, float );

/* gtk.c */

extern void gtk_gui_init (int* argc, char** argv[]);
extern int manage_vst_plugin (JackVST * jvst);

/* Prototype for plugin "canDo" helper function*/
int canDo(struct AEffect* plugin, char* feature);

/* Structures & Prototypes for midi output and associated queue */
typedef struct _MidiMessage MidiMessage;

struct _MidiMessage {
	jack_nframes_t	time;
	int				len;	/* Length of MIDI message, in bytes. */
	unsigned char	data[3];
};
#define RINGBUFFER_SIZE	1024*sizeof(MidiMessage)
void process_midi_output(JackVST* jvst, jack_nframes_t nframes);
void queue_midi_message(JackVST* jvst, int status, int d1, int d2, jack_nframes_t delta);


#ifdef HAVE_LASH
lash_client_t * lash_client;
#endif


static void *(*the_function)(void*);
static void *the_arg;
static pthread_t the_thread_id;
static sem_t sema;

JackVST *jvst_first;

void
signal_callback_handler(int signum)
{
	JackVST *jvst;

	jvst = jvst_first;

	printf("Caught signal to terminate\n");

        jvst->fst->plugin->dispatcher (jvst->fst->plugin, effStopProcess, 0, 0, NULL, 0.0f);
        jvst->fst->plugin->dispatcher (jvst->fst->plugin, effMainsChanged, 0, 0, NULL, 0.0f);

	jack_deactivate(jvst->client);

	jvst->fst->event_call = CLOSE;
}


static DWORD WINAPI wine_thread_aux( LPVOID arg )
{
  printf("Audio ThID: %d\n", GetCurrentThreadId ());

  the_thread_id = pthread_self();
  sem_post( &sema );
  the_function( the_arg );
  return 0;
}

int wine_pthread_create (pthread_t* thread_id, const pthread_attr_t* attr, void *(*function)(void*), void* arg)
{
  sem_init( &sema, 0, 0 );
  the_function = function;
  the_arg = arg;

  CreateThread( NULL, 0, wine_thread_aux, arg, 0,0 );
  sem_wait( &sema );

  *thread_id = the_thread_id;
  return 0;
}

static pthread_t audio_thread = 0;

void process_midi_output(JackVST* jvst, jack_nframes_t nframes)
{
	/* This jack ringbuffer consume code was largely taken from jack-keyboard */
	/* written by Edward Tomasz Napierala <trasz@FreeBSD.org>                 */
	int		read, t;
	unsigned char  *buffer;
	void           *port_buffer;
	jack_nframes_t	last_frame_time;
	jack_ringbuffer_t* ringbuffer;
	MidiMessage ev;

	last_frame_time = jack_last_frame_time(jvst->client);

	port_buffer = jack_port_get_buffer(jvst->midi_outport, nframes);
	if (port_buffer == NULL) {
		fst_error("jack_port_get_buffer failed, cannot send anything.");
		return;
	}

	jack_midi_clear_buffer(port_buffer);

	ringbuffer = jvst->ringbuffer;
	while (jack_ringbuffer_read_space(ringbuffer)) {
		read = jack_ringbuffer_peek(ringbuffer, (char*)&ev, sizeof(ev));
		if (read != sizeof(ev)) {
			fst_error("Short read from the ringbuffer, possible note loss.");
			jack_ringbuffer_read_advance(ringbuffer, read);
			continue;
		}

		t = ev.time + nframes - last_frame_time;

		/* If computed time is too much into the future, we'll send it later. */
		if (t >= (int)nframes) break;

		/* If computed time is < 0, we missed a cycle because of xrun. */
		if (t < 0) t = 0;

		jack_ringbuffer_read_advance(ringbuffer, sizeof(ev));

		buffer = jack_midi_event_reserve(port_buffer, t, ev.len);
		if (buffer == NULL) {
			fst_error("queue: jack_midi_event_reserve failed, NOTE LOST.");
			break;
		}

		memcpy(buffer, ev.data, ev.len);
	}
}

void process_midi_input(JackVST* jvst, jack_nframes_t nframes)
{
	struct AEffect* plugin = jvst->fst->plugin;

	void *port_buffer = jack_port_get_buffer( jvst->midi_inport, nframes );
	jack_nframes_t num_jackevents = jack_midi_get_event_count( port_buffer );
	jack_midi_event_t jackevent;
	int i,j,stuffed_events = 0;

	if( num_jackevents >= MIDI_EVENT_MAX )
		num_jackevents = MIDI_EVENT_MAX;

	for( i=0; i < num_jackevents; i++ ) {
		if ( jack_midi_event_get( &jackevent, port_buffer, i ) != 0 )
			break;

		// Midi channel
		if ( (jvst->channel != -1 )
			&&  ( jackevent.buffer[0] >= 0x80 && jackevent.buffer[0] <= 0xEF)
			&&  ( (jackevent.buffer[0] & 0x0f) != jvst->channel ) )
			continue;
		// Mapping MIDI
		if ( (jackevent.buffer[0] & 0xf0) == 0xb0 && jvst->fst->midi_learn ) {
			jvst->fst->midi_learn_CC = jackevent.buffer[1];
		// handle MIDI mapped
		} else if( (jackevent.buffer[0] & 0xf0) == 0xb0 && jvst->fst->midi_map[jackevent.buffer[1]] != -1 ) {
			int parameter = jvst->fst->midi_map[jackevent.buffer[1]];
			float value = 1.0/127.0 * (float) jackevent.buffer[2];
			plugin->setParameter( plugin, parameter, value );
		// .. let's play
		} else if( jvst->want_midi_in ) {
			jvst->event_array[stuffed_events].type = kVstMidiType;
			jvst->event_array[stuffed_events].byteSize = 24;
			jvst->event_array[stuffed_events].deltaFrames = jackevent.time;

			for( j=0; (j<4); j++ ) {
				jvst->event_array[stuffed_events].midiData[j] = 
					(j<jackevent.size) ? jackevent.buffer[j] : 0;
			}
			stuffed_events += 1;
		}
	}

	if ( stuffed_events > 0 ) {
		jvst->events->numEvents = stuffed_events;
		plugin->dispatcher (plugin, effProcessEvents, 0, 0, jvst->events, 0.0f);
	}
}

void queue_midi_message(JackVST* jvst, int status, int d1, int d2, jack_nframes_t delta )
{
	jack_ringbuffer_t* ringbuffer;
	int	written;
	int	statusHi = (status >> 4) & 0xF;
	int	statusLo = status & 0xF;
	MidiMessage ev;

	/*fst_error("queue_new_message = 0x%hhX, %d, %d\n", status, d1, d2);*/
	/* fst_error("statusHi = %d, statusLo = %d\n", statusHi, statusLo);*/

	ev.data[0] = status;
	if (statusHi == 0xC || statusHi == 0xD) {
		ev.len = 2;
		ev.data[1] = d1;
	} else if (statusHi == 0xF) {
		if (statusLo == 0 || statusLo == 2) {
			ev.len = 3;
			ev.data[1] = d1;
			ev.data[2] = d2;
		} else if (statusLo == 1 || statusLo == 3) {
			ev.len = 2;
			ev.data[1] = d1;
		} else ev.len = 1;
	} else {
		ev.len = 3;
		ev.data[1] = d1;
		ev.data[2] = d2;
	}

	if( pthread_self() == audio_thread ) {
		unsigned char  *buffer;
		void           *port_buffer;
		port_buffer = jack_port_get_buffer(jvst->midi_outport, jack_get_buffer_size( jvst->client ) );
		if (port_buffer == NULL) {
			fst_error("jack_port_get_buffer failed, cannot send anything.");
			return;
		}

		buffer = jack_midi_event_reserve(port_buffer, delta, ev.len);
		if (buffer == NULL) {
			fst_error("jack_midi_event_reserve failed, NOTE LOST.");
			return;
		}
		memcpy(buffer, ev.data, ev.len);

		return;
	}

	ev.time = jack_frame_time(jvst->client) + delta;

	ringbuffer = jvst->ringbuffer;
	if (jack_ringbuffer_write_space(ringbuffer) < sizeof(ev)) {
		fst_error("Not enough space in the ringbuffer, NOTE LOST.");
		return;
	}

	written = jack_ringbuffer_write(ringbuffer, (char*)&ev, sizeof(ev));
	if (written != sizeof(ev)) {
		fst_error("jack_ringbuffer_write failed, NOTE LOST.");
	}
}

int process_callback( jack_nframes_t nframes, void* data) 
{
	short i, o;
	JackVST* jvst = (JackVST*) data;
	struct AEffect* plugin = jvst->fst->plugin;

	audio_thread = pthread_self();

	// Initialize JackAudio-input buffers
	for (i = 0; i < plugin->numInputs; ++i)
		jvst->ins[i]  = (float *) jack_port_get_buffer (jvst->inports[i], nframes);

	// Initialize JackAudio-output buffers
	for (o = 0, i = 0; o < plugin->numOutputs; ++o) {
		jvst->outs[o]  = (float *) jack_port_get_buffer (jvst->outports[o], nframes);
	
		// If bypassed then copy In's to Out's
		if ( (jvst->bypassed) && (i < plugin->numInputs) ) {
			memcpy (jvst->outs[o], jvst->ins[i], sizeof (float) * nframes);
			++i;
		// Zeroing buffers
		} else {
			memset (jvst->outs[o], 0, sizeof (float) * nframes);
		}
	}

	// Bypass - because all audio jobs are done  - simply return
	if (jvst->bypassed)
		return 0;

	// Normal process
	if (jvst->midi_inport) 
		process_midi_input(jvst, nframes);

	if (plugin->flags & effFlagsCanReplacing) {
		plugin->processReplacing (plugin, jvst->ins, jvst->outs, nframes);
	} else {
		plugin->process (plugin, jvst->ins, jvst->outs, nframes);
	}

	if (jvst->midi_outport)
		process_midi_output(jvst, nframes);

	return 0;      
}

int session_callback( void *arg )
{
	printf("session callback\n");

        JackVST* jvst = (JackVST*) arg;
        jack_session_event_t *event = jvst->session_event;

        char retval[256];
        char filename[MAX_PATH];

        snprintf( filename, sizeof(filename), "%sstate.fps", event->session_dir );
        fst_save_state( jvst->fst, filename );
        snprintf( retval, sizeof(retval), "fst -u %s -s ${SESSION_DIR}state.fps %s", event->client_uuid, jvst->handle->path );
        event->command_line = strdup( retval );

        jack_session_reply( jvst->client, event );

        jack_session_event_free( event );

        return 0;
}

void session_callback_aux( jack_session_event_t *event, void *arg )
{
        JackVST* jvst = (JackVST*) arg;
        jvst->session_event = event;

        g_idle_add( session_callback, arg );
}

enum ParseMode {
    MODE_NORMAL,
    MODE_QUOTE,
    MODE_DOUBLEQUOTE,
    MODE_ESCAPED,
    MODE_WHITESPACE,
    MODE_EOL

};

void create_argc_argv_from_cmdline( char *cmdline, char *argv0, int *argc, char ***argv ) {
    // first count argc
    char *pos = cmdline;
    enum ParseMode parseMode = MODE_WHITESPACE;
    enum ParseMode parseMode_before_ESC = MODE_NORMAL;
    int i;

    int myargc = 1; 
    char **myargv;

    while( parseMode != MODE_EOL ) {
	switch( parseMode ) {
	    case MODE_NORMAL:
		switch( *pos ) {
		    case '\"':
			parseMode = MODE_DOUBLEQUOTE;
			break;
		    case '\'':
			parseMode = MODE_QUOTE;
			break;
		    case '\\':
			parseMode_before_ESC = parseMode;
			parseMode = MODE_ESCAPED;
			break;
		    case ' ':	// First Space after an arg;
			parseMode = MODE_WHITESPACE;
			myargc++;
			break;
		    case 0:	// EOL after arg.
			parseMode = MODE_EOL;
			break;
		    default:
			// Normal char;
			break;
		}
		break;
	    case MODE_QUOTE:
		switch( *pos ) {
		    case '\'':
			parseMode = MODE_NORMAL;
			break;
		    case '\\':
			parseMode_before_ESC = parseMode;
			parseMode = MODE_ESCAPED;
			break;
		    case 0:
			fst_error( "parse Error on cmdline" );
			parseMode = MODE_EOL;
			break;
		    default:
			// Normal char;
			break;
		}
		break;
	    case MODE_DOUBLEQUOTE:
		switch( *pos ) {
		    case '\"':
			parseMode = MODE_NORMAL;
			break;
		    case '\\':
			parseMode_before_ESC = parseMode;
			parseMode = MODE_ESCAPED;
			break;
		    case 0:
			fst_error( "parse Error on cmdline" );
			parseMode = MODE_EOL;
			break;
		    default:
			// Normal char;
			break;
		}
		break;
	    case MODE_ESCAPED:
		switch( *pos ) {
		    case '\"':
			// emit escaped char;
			parseMode = parseMode_before_ESC;
			break;
		    case '\'':
			// emit escaped char;
			parseMode = parseMode_before_ESC;
			break;
		    case '\\':
			// emit escaped char;
			parseMode = parseMode_before_ESC;
			break;
		    case 0:
			fst_error( "EOL after escape: ignored" );
			parseMode = MODE_EOL;
			break;
		    default:
			fst_error( "Unknown Escapecharacter: ignored" );
			parseMode = parseMode_before_ESC;
			// Normal char;
			break;
		}
		break;
	    case MODE_WHITESPACE:
		switch( *pos ) {
		    case '\"':
			parseMode = MODE_DOUBLEQUOTE;
			myargc++;
			break;
		    case '\'':
			parseMode = MODE_QUOTE;
			myargc++;
			break;
		    case '\\':
			parseMode_before_ESC = MODE_NORMAL;
			parseMode = MODE_ESCAPED;
			myargc++;
			break;
		    case ' ':
			parseMode = MODE_WHITESPACE;
			break;
		    case 0:
			parseMode = MODE_EOL;
			break;
		    default:
			// 
			parseMode = MODE_NORMAL;
			// Normal char;
			myargc++;
			break;
		}
		break;
	}
	pos++;
    }

    myargv = malloc( myargc * sizeof( char * ) );
    if( !myargv ) {
	fst_error( "cant alloc memory" );
	exit( 10 );
    }

    // alloc strlen(cmdline) + 1 for each argv.
    // this avoids another parsing pass.
    for( i=0; i<myargc; i++ ) {
	myargv[i] = malloc( strlen(cmdline) + 1 );
	if( !myargv[i] ) {
	    fst_error( "cant alloc memory" );
	    exit( 10 );
	}
	myargv[i][0] = 0;
    }

    // Now rerun theparser and actually emit chars.
    pos = cmdline;
    parseMode = MODE_WHITESPACE;
    parseMode_before_ESC = MODE_NORMAL;
    int current_arg = 0;
    char *emit_pos = myargv[0];
    
    while( parseMode != MODE_EOL ) {
	switch( parseMode ) {
	    case MODE_NORMAL:
		switch( *pos ) {
		    case '\"':
			parseMode = MODE_DOUBLEQUOTE;
			break;
		    case '\'':
			parseMode = MODE_QUOTE;
			break;
		    case '\\':
			parseMode_before_ESC = parseMode;
			parseMode = MODE_ESCAPED;
			break;
		    case ' ':	// First Space after an arg;
			parseMode = MODE_WHITESPACE;
			*emit_pos = 0;

			break;
		    case 0:	// EOL after arg.
			parseMode = MODE_EOL;
		        *emit_pos = 0;	
			break;
		    default:

			*(emit_pos++) = *pos;
			break;
		}
		break;
	    case MODE_QUOTE:
		switch( *pos ) {
		    case '\'':
			parseMode = MODE_NORMAL;
			break;
		    case '\\':
			parseMode_before_ESC = parseMode;
			parseMode = MODE_ESCAPED;
			break;
		    case 0:
			fst_error( "parse Error on cmdline" );
			parseMode = MODE_EOL;
		        *emit_pos = 0;	
			break;
		    default:
			// Normal char;
			*(emit_pos++) = *pos;
			break;
		}
		break;
	    case MODE_DOUBLEQUOTE:
		switch( *pos ) {
		    case '"':
			parseMode = MODE_NORMAL;
			break;
		    case '\\':
			parseMode_before_ESC = parseMode;
			parseMode = MODE_ESCAPED;
			break;
		    case 0:
			fst_error( "parse Error on cmdline" );
			parseMode = MODE_EOL;
		        *emit_pos = 0;	
			break;
		    default:
			// Normal char;
			*(emit_pos++) = *pos;
			break;
		}
		break;
	    case MODE_ESCAPED:
		switch( *pos ) {
		    case '\"':
			// emit escaped char;
			parseMode = parseMode_before_ESC;
			*(emit_pos++) = *pos;
			break;
		    case '\'':
			// emit escaped char;
			parseMode = parseMode_before_ESC;
			*(emit_pos++) = *pos;
			break;
		    case '\\':
			// emit escaped char;
			parseMode = parseMode_before_ESC;
			*(emit_pos++) = *pos;
			break;
		    case 0:
			fst_error( "EOL after escape: ignored" );
			parseMode = MODE_EOL;
		        *emit_pos = 0;	
			break;
		    default:
			fst_error( "Unknown Escapecharacter: ignored" );
			parseMode = parseMode_before_ESC;
			break;
		}
		break;
	    case MODE_WHITESPACE:
		switch( *pos ) {
		    case '\"':
			parseMode = MODE_DOUBLEQUOTE;
			// ok... arg begins
			current_arg++;
			emit_pos = myargv[current_arg];
			break;
		    case '\'':
			parseMode = MODE_QUOTE;
			// ok... arg begins
			current_arg++;
			emit_pos = myargv[current_arg];
			break;
		    case '\\':
			parseMode_before_ESC = MODE_NORMAL;
			parseMode = MODE_ESCAPED;
			// ok... arg begins
			current_arg++;
			emit_pos = myargv[current_arg];
			break;
		    case ' ':
			parseMode = MODE_WHITESPACE;
			break;
		    case 0:
			parseMode = MODE_EOL;
			break;
		    default:
			parseMode = MODE_NORMAL;
			// ok... arg begins
			current_arg++;
			emit_pos = myargv[current_arg];
			// Normal char;
			*(emit_pos++) = *pos;
			break;
		}
		break;
	}
	pos++;
    }

    strncpy( myargv[0], argv0, strlen(cmdline) );
    
    *argc = myargc;
    *argv = myargv;
}

/* Plugin "canDo" helper function to neaten up plugin feature detection calls */
int canDo(struct AEffect* plugin, char* feature)
{
	return (plugin->dispatcher(plugin, effCanDo, 0, 0, (void*)feature, 0.0f) > 0);
}

int
jvst_connect(JackVST *jvst, const char *myname, const char *connect_to)
{
	int i,j;
	char pname[strlen(myname) + 16];
	const char *ptype;

	const char **jports = jack_get_ports(jvst->client, connect_to, NULL, JackPortIsInput);
	if (jports == NULL) {
		printf("Can't find any ports for %s\n", connect_to);
		return 0;
	}

	for (i=0, j=0; jports[i] != NULL && j < jvst->fst->plugin->numOutputs; i++) {
		ptype = jack_port_type(jack_port_by_name(jvst->client, jports[i]));

		if (strcmp(ptype, JACK_DEFAULT_AUDIO_TYPE) != 0) {
			printf("Skip incompatibile port: %s\n", ptype);
			continue;
		}

		sprintf(pname, "%s:out%d", myname, ++j);
		jack_connect(jvst->client, pname, jports[i]);
		printf("Connect: %s -> %s\n", pname, jports[i]);
	}
	jack_free(jports);

	return 1;
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow)
//int 
//main( int argc, char **argv ) 
{
	JackVST* jvst;
	struct AEffect* plugin;
	char *client_name = 0;
	char* period;
	int opt_uuid = 0;
	int opt_channel = -1;
	short i;
	short opt_with_editor = 2;
	bool load_state = FALSE;
	bool opt_bypassed = FALSE;
	double opt_tempo = -1;
	const char *connect_to = NULL;
	const char *state_file = 0;
	const char *plug;
	int vst_version;

	float sample_rate = 0;
	long  block_size = 0;

	int argc;
	char **argv;

	create_argc_argv_from_cmdline( cmdline, "./fst", &argc, &argv );

#ifdef HAVE_LASH
	lash_event_t *event;
	lash_args_t *lash_args;

	lash_args = lash_extract_args(&argc, &argv);
#endif

        // Parse command line options
	int c;
	while ( (c = getopt (argc, argv, "bners:c:k:j:t:u:")) != -1) {
		switch (c) {
			case 'b':
				opt_bypassed = TRUE;
				break;
			case 'e':
				opt_with_editor = 1;
				break;
			case 'n':
				opt_with_editor = 0;
				break;
			case 's':
				load_state = 1;
                                state_file = optarg;
				break;
			case 'c':
				client_name = optarg;
				break;
			case 'k':
				opt_channel = strtol(optarg, NULL, 10) - 1;
				if (opt_channel < 0 || opt_channel > 15)
					opt_channel = -1;
				break;
			case 'j':
				connect_to = optarg;
				break;
			case 't':
				opt_tempo = strtod(optarg, NULL);
				break;
			case 'u':
				opt_uuid = (int) optarg;
				break;
			default:
				fprintf (stderr, "usage: %s <plugin>\n", argv[0]);
				return 1;
		}
	}

	if (optind >= argc) {
		fprintf (stderr, "usage: [ -b | -n | -e | -s state_file | -j connect_to | -c client_name ] %s <plugin>\n", argv[0]);
		return 1;
	}
	plug = argv[optind];

	// Init VST
	jvst = (JackVST*) calloc (1, sizeof (JackVST));
	jvst_first = jvst;
	jvst->bypassed = opt_bypassed;
	jvst->channel = opt_channel;
	jvst->with_editor = opt_with_editor;
	jvst->uuid = opt_uuid;
	jvst->tempo = opt_tempo; // -1 here mean get it from Jack

//	for (i=0; i<128; i++ )
//		jvst->fst->midi_map[i] = -1;

	if (!client_name) {
		client_name = basename( strdup(plug) );
		if ((period = strrchr (client_name, '.')) != NULL)
			*period = '\0';
	}

	printf( "yo... lets see...\n" );
	if ((jvst->handle = fst_load (plug)) == NULL) {
		fst_error ("can't load plugin %s", plug);
		return 1;
	}

	printf( "instantiate... \n" );

	if ((jvst->fst = fst_instantiate (jvst->handle, (audioMasterCallback) jack_host_callback, jvst)) == NULL) {
		fst_error ("can't instantiate plugin %s", plug);
		return 1;
	}

	plugin = jvst->fst->plugin;

	printf("Start Jack thread ...\n");
	if ((jvst->client = jack_client_open (client_name, JackSessionID, NULL, jvst->uuid )) == 0) {
		fst_error ("can't connect to JACK");
		return 1;
	}

	/* set rate and blocksize */
	sample_rate = (float)jack_get_sample_rate(jvst->client);
	block_size = jack_get_buffer_size(jvst->client);

	printf("Sample Rate = %.2f\n", sample_rate);
	printf("Block Size = %ld\n", block_size);

	plugin->dispatcher (plugin, effSetSampleRate, 0, 0, NULL, 
			    (float) jack_get_sample_rate (jvst->client));
	plugin->dispatcher (plugin, effSetBlockSize, 0, 
			    jack_get_buffer_size (jvst->client), NULL, 0.0f);

	// ok.... plugin is running... lets bind to lash...
	
#ifdef HAVE_LASH
	int flags = LASH_Config_Data_Set;

	lash_client = lash_init(lash_args, client_name, flags, LASH_PROTOCOL(2, 0));

	if (!lash_client) {
	    fprintf(stderr, "%s: could not initialise lash\n", __FUNCTION__);
	    fprintf(stderr, "%s: running fst without lash session-support\n", __FUNCTION__);
	    fprintf(stderr, "%s: to enable lash session-support launch the lash server prior fst\n", __FUNCTION__);
	}

	if (lash_enabled(lash_client)) {
		event = lash_event_new_with_type(LASH_Client_Name);
		lash_event_set_string(event, client_name);
		lash_send_event(lash_client, event);
	}
#endif


	/* set program to zero */
	/* i comment this out because it breaks dfx Geometer
	 * looks like we cant set programs for it
	 *
	 * TODO:
	 * this might have been because we were not a real wine thread, but i doubt
	 * it. need to check.
	 *
	 * plugin->dispatcher (plugin, effSetProgram, 0, 0, NULL, 0.0f); 
	 */


	jvst->midi_inport = 
		jack_port_register(jvst->client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	vst_version = plugin->dispatcher (plugin, effGetVstVersion, 0, 0, NULL, 0.0f);
	if (vst_version >= 2) {
		int isSynth = (plugin->flags & effFlagsIsSynth) > 0;
		int canReceiveVstEvents = canDo(plugin, "receiveVstEvents");
		int canReceiveVstMidiEvent = canDo(plugin, "receiveVstMidiEvent");
		int canSendVstEvents = canDo(plugin, "sendVstEvents");
		int canSendVstMidiEvent = canDo(plugin, "sendVstMidiEvent");

		printf("Plugin isSynth = %d\n", isSynth);
		printf("Plugin canDo receiveVstEvents = %d\n", canReceiveVstEvents);
		printf("Plugin canDo receiveVstMidiEvent = %d\n", canReceiveVstMidiEvent);
		printf("Plugin canDo sendVstEvents = %d\n", canSendVstEvents);
		printf("Plugin canDo SendVstMidiEvent = %d\n", canSendVstMidiEvent);

		/* should we send the plugin VST events (i.e. MIDI) */
		if (isSynth || canReceiveVstEvents || canReceiveVstMidiEvent) {
			int i;
			jvst->want_midi_in = 1;

			/* The VstEvents structure already contains an array of 2    */
			/* pointers to VstEvent so I guess that this malloc actually */
			/* gives enough  space for MIDI_EVENT_MAX + 2 events....     */
			jvst->events = (VstEvents*)malloc(sizeof(VstEvents) +
					(MIDI_EVENT_MAX * sizeof(VstMidiEvent*)));

			jvst->events->numEvents = 0;
			jvst->events->reserved = 0;

			/* Initialise dynamic array of MIDI_EVENT_MAX VstMidiEvents */
			/* and point the VstEvents events array of pointers to it   */
			jvst->event_array = (VstMidiEvent*)calloc(MIDI_EVENT_MAX, sizeof (VstMidiEvent));
			for (i = 0; i < MIDI_EVENT_MAX; i++) {
				jvst->events->events[i] = (VstEvent*)&(jvst->event_array[i]);
			}
		}

		/* Can the plugin send VST events (i.e. MIDI) */
		if (canSendVstEvents || canSendVstMidiEvent) {
			jvst->ringbuffer = jack_ringbuffer_create(RINGBUFFER_SIZE);
			if (jvst->ringbuffer == NULL) {
				fst_error("Cannot create JACK ringbuffer.");
				return 1;
			}

			jack_ringbuffer_mlock(jvst->ringbuffer);

			jvst->midi_outport =
				jack_port_register(jvst->client, "midi-out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
		}
	}

	printf("PortLayout: in: %d out: %d\n", plugin->numInputs, plugin->numOutputs);

	jvst->inports = (jack_port_t**)malloc(sizeof(jack_port_t*) * plugin->numInputs);
	jvst->ins = (float**)malloc(sizeof(float*) * plugin->numInputs);
	
	for (i = 0; i < plugin->numInputs; ++i) {
		char buf[64];
		snprintf (buf, sizeof(buf), "in%d", i+1);
		jvst->inports[i] = jack_port_register (jvst->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	}
	
	jvst->outports = (jack_port_t **) malloc (sizeof(jack_port_t*) * plugin->numOutputs);
	jvst->outs = (float **) malloc (sizeof (float *) * plugin->numOutputs);
	
	for (i = 0; i < plugin->numOutputs; ++i) {
		char buf[64];
		snprintf (buf, sizeof(buf), "out%d", i+1);
		jvst->outports[i] = jack_port_register (jvst->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	}

	jack_set_thread_creator (wine_pthread_create);

	// Start process audio
	jack_set_process_callback (jvst->client, (JackProcessCallback) process_callback, jvst);

        if (jack_set_session_callback) {
             printf( "Setting up session callback\n" );
             jack_set_session_callback( jvst->client, session_callback_aux, jvst );
        }


#ifdef HAVE_LASH
	if( lash_enabled( lash_client ) ) {
	    event = lash_event_new_with_type(LASH_Jack_Client_Name);
	    lash_event_set_string(event, client_name);
	    lash_send_event(lash_client, event);
	}
#endif
        /* load state if requested */
	if ( load_state && ! fst_load_state (jvst->fst, state_file)) {
		jack_deactivate( jvst->client );
		return 1;
	}

	if (jvst->with_editor) {
		printf( "ok.... RockNRoll\n" );
		gtk_gui_init (&argc, &argv);
        	if (CreateThread (NULL, 0, (LPTHREAD_START_ROUTINE) manage_vst_plugin, jvst, 0, NULL) == NULL) {
                	fst_error ("could not create new thread proxy");
	                return 0;
        	}
	} else {
		signal(SIGINT, signal_callback_handler);
		printf("Editor Disabled\n");
	}

	if (! jvst->bypassed) {
		printf("Activate plugin\n");
		fst_resume(jvst->fst);
	}

	printf( "Calling Jack activate\n" );
	jack_activate (jvst->client);

	if (connect_to)
		jvst_connect(jvst, client_name, connect_to);

	fst_gui_event_loop(hInst);

	free(jvst);

	return 0;
}
