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
#include <unistd.h>
#include <libgen.h>
#include <glib.h>
#include <semaphore.h>
#include <signal.h>
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
extern int gtk_gui_start (JackVST * jvst);

void queue_midi_message(JackVST* jvst, int status, int d1, int d2, jack_nframes_t delta);

#ifdef HAVE_LASH
lash_client_t * lash_client;
#endif

static void *(*the_function)(void*);
static void *the_arg;
static pthread_t the_thread_id;
static sem_t sema;
static sem_t jvst_quit;

JackVST *jvst_first;

bool
jvst_load_state (JackVST* jvst, const char * filename)
{
	bool success;
	char* file_ext = strrchr(filename, '.');

	if (strcasecmp(file_ext, ".fps") == 0) {
		success = fps_load(jvst, filename);
	} else if ( (strcasecmp(file_ext, ".fxp") == 0) || 
	            (strcasecmp(file_ext, ".fxb") == 0) )
	{
		success = fst_load_fxfile(jvst->fst, filename);
	} else {
		printf("Unkown file type\n");
		success = FALSE;
	}

	if (success) {
		printf("File %s loaded\n", filename);
	} else {
		printf("Unable to load file %s\n", filename);
	}

	return success;
}

bool
jvst_save_state (JackVST* jvst, const char * filename)
{
	char* file_ext = strrchr(filename, '.');

	if (strcasecmp(file_ext, ".fxp") == 0) {
		fst_save_fxfile(jvst->fst, filename, 0);
	} else if (strcasecmp(file_ext, ".fxb") == 0) {
		fst_save_fxfile(jvst->fst, filename, 1);
	} else if (strcasecmp(file_ext, ".fps") == 0) {
		fps_save(jvst, filename);
	} else {
		printf("Unkown file type\n");
		return FALSE;
	}

	return TRUE;
}

void
signal_callback_handler(int signum)
{
	JackVST *jvst;

	jvst = jvst_first;

	printf("Caught signal to terminate\n");

  	sem_post( &jvst_quit );
}


static DWORD WINAPI wine_thread_aux( LPVOID arg )
{
  printf("Audio ThID: %d\n", GetCurrentThreadId ());

  the_thread_id = pthread_self();
  sem_post( &sema );
  the_function( the_arg );
  return 0;
}

int
wine_pthread_create (pthread_t* thread_id, const pthread_attr_t* attr, void *(*function)(void*), void* arg)
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

static inline void
process_midi_output(JackVST* jvst, jack_nframes_t nframes)
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

static inline void
process_midi_input(JackVST* jvst, jack_nframes_t nframes)
{
	struct AEffect* plugin = jvst->fst->plugin;

	void *port_buffer = jack_port_get_buffer( jvst->midi_inport, nframes );
	jack_nframes_t num_jackevents = jack_midi_get_event_count( port_buffer );
	jack_midi_event_t jackevent;
	unsigned short i, j;
	int stuffed_events = 0;

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
		// If Volume control is enabled then grab CC7 messages
		if ( (jvst->volume != -1) && 
			( (jackevent.buffer[0] & 0xf0) == 0xB0 ) && 
			(jackevent.buffer[1] == 0x07) )
		{
			jvst_set_volume(jvst, jackevent.buffer[2]);
			continue;
		}
		// Mapping MIDI
		if ( (jackevent.buffer[0] & 0xf0) == 0xb0 && jvst->midi_learn ) {
			jvst->midi_learn_CC = jackevent.buffer[1];
		// handle MIDI mapped
		} else if( (jackevent.buffer[0] & 0xf0) == 0xb0 && jvst->midi_map[jackevent.buffer[1]] != -1 ) {
			int parameter = jvst->midi_map[jackevent.buffer[1]];
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

void
queue_midi_message(JackVST* jvst, int status, int d1, int d2, jack_nframes_t delta )
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

int
process_callback( jack_nframes_t nframes, void* data) 
{
	short i, o;
	JackVST* jvst = (JackVST*) data;
	struct AEffect* plugin = jvst->fst->plugin;

	audio_thread = pthread_self();

	// Initialize input buffers
	for (i = 0; i < jvst->numIns; ++i)
		jvst->ins[i]  = (float *) jack_port_get_buffer (jvst->inports[i], nframes);

	// Initialize output buffers
	for (o = 0, i = 0; o < jvst->numOuts; ++o) {
		jvst->outs[o]  = (float *) jack_port_get_buffer (jvst->outports[o], nframes);
	
		// If bypassed then copy In's to Out's
		if ( (jvst->bypassed) && (i < jvst->numIns) && (o < jvst->numOuts) ) {
			memcpy (jvst->outs[o], jvst->ins[i], sizeof (float) * nframes);
			++i;
		// Zeroing output buffers
		} else {
			memset (jvst->outs[o], 0, sizeof (float) * nframes);
		}
	}

	// Bypass - because all audio jobs are done  - simply return
	if (jvst->bypassed) return 0;

	// Process MIDI Input
	if (jvst->midi_inport) 
		process_midi_input(jvst, nframes);

	// Deal with plugin
	if (plugin->flags & effFlagsCanReplacing) {
		plugin->processReplacing (plugin, jvst->ins, jvst->outs, nframes);
	} else {
		plugin->process (plugin, jvst->ins, jvst->outs, nframes);
	}

	// Ouput volume control
	if (jvst->volume != -1) {
		jack_nframes_t n=0;
		o = 0;
		while(o < jvst->numOuts) {
			jvst->outs[o][n] *= jvst->volume;
			if (n < nframes) {
				n++;
			} else {
				++o;
				n=0;
			}
		}
	}

	// Process MIDI Output
	if (jvst->midi_outport)
		process_midi_output(jvst, nframes);

	return 0;      
}

int
session_callback( void *arg )
{
	printf("session callback\n");

        JackVST* jvst = (JackVST*) arg;
        jack_session_event_t *event = jvst->session_event;

        char retval[256];
        char filename[MAX_PATH];

        snprintf( filename, sizeof(filename), "%sstate.fps", event->session_dir );
        jvst_save_state( jvst, filename );
        snprintf( retval, sizeof(retval), "fsthost -u %s -s ${SESSION_DIR}state.fps %s", event->client_uuid, jvst->handle->path );
        event->command_line = strdup( retval );

        jack_session_reply( jvst->client, event );

        jack_session_event_free( event );

        return 0;
}

void
session_callback_aux( jack_session_event_t *event, void *arg )
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

int
jvst_connect(JackVST *jvst, const char *myname, const char *connect_to)
{
	unsigned short i,j;
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

void
usage(char* appname) {
	const char* format = "%-20s%s\n";

	fprintf(stderr, "\nUsage: %s [ options ] <plugin>\n", appname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, format, "-b", "Bypass");
	fprintf(stderr, format, "-n", "Disable Editor and GTK GUI");
	fprintf(stderr, format, "-e", "Hide Editor");
	fprintf(stderr, format, "-s state_file", "Load state_file");
	fprintf(stderr, format, "-c client_name", "Jack Client name");
	fprintf(stderr, format, "-k channel", "MIDI Channel filter");
	fprintf(stderr, format, "-i num_in", "Jack number In ports");
	fprintf(stderr, format, "-j connect_to", "Connect Audio Out to connect_to");
	fprintf(stderr, format, "-o num_out", "Jack number Out ports");
	fprintf(stderr, format, "-t tempo", "Set fixed Tempo rather than using JackTransport");
	fprintf(stderr, format, "-u uuid", "JackSession UUID");
	fprintf(stderr, format, "-V", "Disable Volume control (and filter CC7 messages)");
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow)
//int 
//main( int argc, char **argv ) 
{
	LPWSTR*		szArgList;
	int		argc;
	char**		argv;
	JackVST*	jvst;
	FST*		fst;
	struct AEffect*	plugin;
	char*		client_name = NULL;
	short		i;
	int		opt_uuid = 0;
	int		opt_channel = -1;
	short		opt_numIns = 0;
	short		opt_numOuts = 0;
	short		opt_with_editor = 2;
	bool		load_state = FALSE;
	bool		opt_bypassed = FALSE;
	bool		opt_disable_volume = FALSE;
	double		opt_tempo = -1;
	const char*	connect_to = NULL;
	const char*	state_file = 0;
	const char*	plug;

	float		sample_rate = 0;
	long		block_size = 0;

	// Parse command line
	szArgList = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (szArgList == NULL) {
		fprintf(stderr, "Unable to parse command line\n");
		return 10;
	}

    	argv = malloc(argc * sizeof(char*));
	for (i=0; i < argc; ++i) {
		int nsize = WideCharToMultiByte(CP_UNIXCP, 0, szArgList[i], -1, NULL, 0, NULL, NULL);
		
		argv[i] = malloc( nsize );
		WideCharToMultiByte(CP_UNIXCP, 0, szArgList[i], -1, (LPSTR) argv[i], nsize, NULL, NULL);
	}
	LocalFree(szArgList);
	strcpy(argv[0], "fsthost");

#ifdef HAVE_LASH
	lash_event_t *event;
	lash_args_t *lash_args;

	lash_args = lash_extract_args(&argc, &argv);
#endif

        // Parse command line options
	int c;
	while ( (c = getopt (argc, argv, "bnes:c:k:i:j:o:t:u:V")) != -1) {
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
			case 'i':
				opt_numIns = strtol(optarg, NULL, 10);
				break;
			case 'j':
				connect_to = optarg;
				break;
			case 'o':
				opt_numOuts = strtol(optarg, NULL, 10);
				break;
			case 't':
				opt_tempo = strtod(optarg, NULL);
				break;
			case 'u':
				opt_uuid = (int) optarg;
				break;
			case 'V':
				opt_disable_volume = TRUE;
				break;
			default:
				usage (argv[0]);
				return 1;
		}
	}

	if (optind >= argc) {
		usage (argv[0]);
		return 1;
	}
	plug = argv[optind];

	// Init JackVST
	jvst = (JackVST*) calloc (1, sizeof (JackVST));
	jvst_first = jvst;
	jvst->bypassed = opt_bypassed;
	jvst->channel = opt_channel;
	jvst->with_editor = opt_with_editor;
	jvst->uuid = opt_uuid;
	jvst->tempo = opt_tempo; // -1 here mean get it from Jack
	if (opt_disable_volume) {
		jvst->volume = -1;
	} else {
		jvst_set_volume(jvst, 63);
	}
	jvst->midi_learn = FALSE;
	jvst->midi_learn_CC = -1;
	jvst->midi_learn_PARAM = -1;
	for(i=0; i<128;++i)
		jvst->midi_map[i] = -1;

	printf( "yo... lets see...\n" );
	if ((jvst->handle = fst_load (plug)) == NULL) {
		fst_error ("can't load plugin %s", plug);
		return 1;
	}

	if (!client_name)
		client_name = jvst->handle->name;

	printf("FST init\n");
	fst_init();

	printf( "Revive plugin: %s\n", client_name);
	if ((jvst->fst = fst_open (jvst->handle, (audioMasterCallback) jack_host_callback, jvst)) == NULL) {
		fst_error ("can't instantiate plugin %s", plug);
		return 1;
	}

	fst = jvst->fst;
	plugin = fst->plugin;

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

	fst->plugin->dispatcher (fst->plugin, effSetSampleRate, 0, 0, NULL, (float) jack_get_sample_rate (jvst->client));
	fst->plugin->dispatcher (fst->plugin, effSetBlockSize, 0, jack_get_buffer_size (jvst->client), NULL, 0.0f);

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

	jvst->midi_inport = 
		jack_port_register(jvst->client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	if (fst->vst_version >= 2) {
		printf("Plugin isSynth = %d\n", fst->isSynth);

		/* should we send the plugin VST events (i.e. MIDI) */
		if (fst->isSynth || fst->canReceiveVstEvents || fst->canReceiveVstMidiEvent) {
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
		if (fst->canSendVstEvents || fst->canSendVstMidiEvent) {
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

	// Register / allocate ports
	printf("Plugin PortLayout: in: %d out: %d\n", plugin->numInputs, plugin->numOutputs);
	jvst->numIns = (opt_numIns > 0 && opt_numIns < plugin->numInputs) ? opt_numIns : plugin->numInputs;
	jvst->numOuts = (opt_numOuts > 0 && opt_numOuts < plugin->numOutputs) ? opt_numOuts : plugin->numOutputs;
	printf("FSTHost PortLayout: in: %d out: %d\n", jvst->numIns, jvst->numOuts);

	jvst->inports = (jack_port_t**)malloc(sizeof(jack_port_t*) * jvst->numIns);
	jvst->ins = (float**)malloc(sizeof(float*) * plugin->numInputs);
	
	for (i = 0; i < jvst->numIns; ++i) {
		if (i < jvst->numIns) {
			char buf[64];
			snprintf (buf, sizeof(buf), "in%d", i+1);
			jvst->inports[i] = jack_port_register (jvst->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		} else {
			// Swap area for plugin not used ports;-)
			jvst->outs[i] = malloc(sizeof(float) * jack_get_buffer_size (jvst->client));
		}
	}
	
	jvst->outports = (jack_port_t **) malloc (sizeof(jack_port_t*) * jvst->numOuts);
	jvst->outs = (float **) malloc (sizeof (float *) * plugin->numOutputs);
	
	for (i = 0; i < plugin->numOutputs; ++i) {
		if (i < jvst->numOuts) {
			char buf[64];
			snprintf (buf, sizeof(buf), "out%d", i+1);
			jvst->outports[i] = jack_port_register (jvst->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		} else {
			// Swap area for plugin not used ports;-)
			jvst->outs[i] = malloc(sizeof(float) * jack_get_buffer_size (jvst->client));
		}
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
        // load state if requested
	if ( load_state && ! jvst_load_state (jvst, state_file) )
		return 1;

	// Activate plugin
	if (! jvst->bypassed) fst_resume(jvst->fst);

	printf( "Jack Activate\n" );
	jack_activate (jvst->client);

	if (connect_to)
		jvst_connect(jvst, client_name, connect_to);

	if (jvst->with_editor) {
		printf( "Start GUI\n" );
		gtk_gui_init (&argc, &argv);
		gtk_gui_start(jvst);
	} else {
		signal(SIGINT, signal_callback_handler);
		printf("GUI Disabled\n");

		sem_init(&jvst_quit, 0, 0);
		sem_wait(&jvst_quit);
	}

	printf("Jack Deactivate\n");
        jack_deactivate( jvst->client );

	printf("Close plugin\n");
        fst_close(jvst->fst);

	free(jvst);

	return 0;
}
