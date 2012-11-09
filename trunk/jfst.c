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

#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <glib.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/syscall.h>
#include <jack/midiport.h>

#include "jackvst.h"

#ifdef HAVE_LASH
#include <lash/lash.h>
#endif

#define VERSION "1.3.2"

const char* my_motherfuckin_name = "fsthost";
const char* ControlAppName = "FHControl";

/* audiomaster.c */
extern long jack_host_callback (struct AEffect*, int32_t, int32_t, intptr_t, void *, float );

/* gtk.c */
extern void gtk_gui_init (int* argc, char** argv[]);
extern int gtk_gui_start (JackVST * jvst);

/* Structures & Prototypes for midi output and associated queue */
struct MidiMessage {
        jack_nframes_t time;
        int            len; /* Length of MIDI message, in bytes. */
        unsigned char  data[3];
};

struct SysExEvent {
        JackVST* jvst;
        jack_midi_data_t* data;
        size_t size;
};

#define RINGBUFFER_SIZE 1024*sizeof(struct MidiMessage)
#define MIDI_EVENT_MAX 1024

#ifdef HAVE_LASH
lash_client_t * lash_client;
#endif

static void *(*the_function)(void*);
static void *the_arg;
static pthread_t the_thread_id;
static sem_t sema;
GMainLoop* glib_main_loop;
JackVST *jvst_first;

JackVST* jvst_new() {
	JackVST* jvst = calloc (1, sizeof (JackVST));
	short i;

        pthread_mutex_init (&jvst->sysex_lock, NULL);
        pthread_cond_init (&jvst->sysex_sent, NULL);
	jvst->with_editor = WITH_EDITOR_SHOW;
	jvst->tempo = -1; // -1 here mean get it from Jack
        /* Local Keyboard MIDI CC message (122) is probably not used by any VST */
	jvst->want_state_cc = 122;
	jvst->midi_learn = FALSE;
	jvst->midi_learn_CC = -1;
	jvst->midi_learn_PARAM = -1;
	for(i=0; i<128;++i)
		jvst->midi_map[i] = -1;

	// Little trick
	SysExIdentReply sxir = SYSEX_IDENT_REPLY;
	memcpy(&jvst->sysex_ident_reply, &sxir, sizeof(SysExIdentReply));

	SysExDumpV1 sxd = SYSEX_DUMP;
	memcpy(&jvst->sysex_dump, &sxd, sizeof(SysExDumpV1));

	return jvst;
}

void
jvst_destroy(JackVST* jvst)
{
	free(jvst);
}

// Prepare data for RT thread and wait for send
bool
jvst_send_sysex(JackVST* jvst, enum SysExWant sysex_want)
{
	pthread_mutex_lock (&jvst->sysex_lock);

	switch(sysex_want) {
	case SYSEX_WANT_DUMP: ;
		char progName[24];
		SysExDumpV1* sxd = &jvst->sysex_dump;
		fst_get_program_name(jvst->fst, jvst->fst->current_program, progName, sizeof(progName));

//		sxd->uuid = jvst->uuid; /* Set once on start */
		sxd->program = jvst->fst->current_program;
		sxd->channel = jvst->channel;
		sxd->volume = jvst_get_volume(jvst);
		sxd->state = (jvst->bypassed) ? SYSEX_STATE_NOACTIVE : SYSEX_STATE_ACTIVE;
		sysex_makeASCII(sxd->program_name, progName, 24);
		sysex_makeASCII(sxd->plugin_name, jvst->client_name, 24);
		break;
	/* Set once on start */
//	case SYSEX_WANT_IDENT_REPLY:
//		jvst->sysex_ident_reply.model[1] = jvst->uuid;
//		break;
	}

	jvst->sysex_want = sysex_want;
	pthread_cond_wait (&jvst->sysex_sent, &jvst->sysex_lock);
	pthread_mutex_unlock (&jvst->sysex_lock);
	printf("SysEx Dumped\n");
}

static bool
jvst_sysex_handler(struct SysExEvent* sysex_event)
{

        JackVST* jvst = sysex_event->jvst;
        jack_midi_data_t* data = sysex_event->data;
        size_t size = sysex_event->size;
        free(sysex_event);

	switch(data[1]) {
	case SYSEX_MYID:
		// Our sysex
		printf("Got Our SysEx - ");

		// Version
		printf("version %d - ", data[2]);
		switch(data[2]) {
		case 1:
			// Type
			switch(data[3]) {
			case SYSEX_TYPE_DUMP:
				printf(" DUMP - OK\n");

				SysExDumpV1* sysex_v1 = (SysExDumpV1*) data;

				jvst->want_state = (sysex_v1->state == SYSEX_STATE_ACTIVE) ?
					WANT_STATE_RESUME : WANT_STATE_BYPASS;
				fst_program_change(jvst->fst, sysex_v1->program);
				jvst->channel = sysex_v1->channel;
				jvst_set_volume(jvst, sysex_v1->volume);

				// Copy sysex state for preserve resending SysEx Dump
				memcpy(&jvst->sysex_dump,sysex_v1,sizeof(SysExDumpV1));

				break;
			case SYSEX_TYPE_RQST: ;
				SysExDumpRequestV1* sysex_request_v1 = (SysExDumpRequestV1*) data;
				printf(" REQUEST - ID %d - OK\n", sysex_request_v1->uuid);
				if (sysex_request_v1->uuid == jvst->sysex_dump.uuid)
					jvst_send_sysex(jvst, SYSEX_WANT_DUMP);

				// If we got DumpRequest then it mean that there is FHControl, so we wanna notify
				jvst->sysex_want_notify = true;
				break;
			}

			break;
		default:
			printf("not supported\n");
		}
		break;
	case SYSEX_NON_REALTIME:
		// Identity request
		if (size >= sizeof(SysExIdentRqst)) {
			// TODO: for now we just always answer ;-)
			SysExIdentRqst sxir = SYSEX_IDENT_REQUEST;
			data[2] = 0x7F; // veil
			if ( memcmp(data, &sxir, sizeof(SysExIdentRqst) ) == 0) {
				printf("Got Identity request\n");
				jvst_send_sysex(jvst, SYSEX_WANT_IDENT_REPLY);
			}
		}
		break;
	}
	free(data);

	return FALSE;
}

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
jvst_save_state (JackVST* jvst, const char * filename) {
	bool ret = FALSE;
	char* file_ext = strrchr(filename, '.');

	if (strcasecmp(file_ext, ".fxp") == 0) {
		ret = fst_save_fxfile(jvst->fst, filename, FXPROGRAM);
	} else if (strcasecmp(file_ext, ".fxb") == 0) {
		ret = fst_save_fxfile(jvst->fst, filename, FXBANK);
	} else if (strcasecmp(file_ext, ".fps") == 0) {
		ret = fps_save(jvst, filename);
	} else {
		printf("Unkown file type\n");
	}

	return ret;
}

static void
jvst_quit(JackVST* jvst) {
	if (jvst->with_editor == WITH_EDITOR_NO) {
		g_main_loop_quit(glib_main_loop);

		printf("Jack Deactivate\n");
		jack_deactivate(jvst->client);

		printf("Close plugin\n");
		fst_close(jvst->fst);
	} else {
		gtk_main_quit();
	}
}

static void
sigint_handler(int signum, siginfo_t *siginfo, void *context)
{
	JackVST *jvst;

	jvst = jvst_first;

	printf("Caught signal to terminate (SIGINT)\n");

	g_idle_add( (GSourceFunc) jvst_quit, jvst);
}

static void
sigusr1_handler(int signum, siginfo_t *siginfo, void *context)
{
	JackVST *jvst;

	jvst = jvst_first;

	printf("Caught signal to save state (SIGUSR1)\n");

	jvst_save_state(jvst, jvst->default_state_file);
}

static DWORD WINAPI
wine_thread_aux( LPVOID arg )
{
        printf("Audio Thread WineID: %d | LWP: %d\n", GetCurrentThreadId (), (int) syscall (SYS_gettid));

	the_thread_id = pthread_self();
	sem_post( &sema );
	the_function( the_arg );
	return 0;
}

static int
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

static inline void
process_midi_output(JackVST* jvst, jack_nframes_t nframes)
{
	/* This jack ringbuffer consume code was largely taken from jack-keyboard */
	/* written by Edward Tomasz Napierala <trasz@FreeBSD.org>                 */
	void *port_buffer;
	int t;

	port_buffer = jack_port_get_buffer(jvst->midi_outport, nframes);
	if (port_buffer == NULL) {
		fst_error("jack_port_get_buffer failed, cannot send anything.");
		return;
	}

	jack_midi_clear_buffer(port_buffer);

	if (! jvst->want_midi_out)
		goto send_sysex;

	int read;
	struct MidiMessage ev;

	jack_nframes_t last_frame_time = jack_last_frame_time(jvst->client);
	jack_ringbuffer_t* ringbuffer = jvst->ringbuffer;
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

		if ( jack_midi_event_write(port_buffer, t, ev.data, ev.len) )
			fst_error("queue: jack_midi_event_write failed, NOTE LOST.");
	}

send_sysex:
	// Send SysEx
	if (jvst->sysex_want == SYSEX_WANT_NO)
		return;

	// Are our lock is ready for us ?
	// If not then we try next time
	if (pthread_mutex_trylock(&jvst->sysex_lock) != 0)
		return;

	// Set frame point
	t = jack_frame_time(jvst->client) - jack_last_frame_time(jvst->client);
	if (t < 0) t = 0;

	size_t sysex_size;
	jack_midi_data_t* sysex_data;
	switch(jvst->sysex_want) {
	case SYSEX_WANT_IDENT_REPLY:
		sysex_data = (jack_midi_data_t*) &jvst->sysex_ident_reply;
		sysex_size = sizeof(SysExIdentReply);
		break;
	case SYSEX_WANT_DUMP:
		sysex_data = (jack_midi_data_t*) &jvst->sysex_dump;
		sysex_size = sizeof(SysExDumpV1);
		break;
	}
	if ( jack_midi_event_write(port_buffer, t, sysex_data, sysex_size) )
		fst_error("SysEx error: jack_midi_event_write failed.");
	jvst->sysex_want = SYSEX_WANT_NO;
	
	pthread_cond_signal(&jvst->sysex_sent);
	pthread_mutex_unlock(&jvst->sysex_lock);
}

static inline void
process_midi_input(JackVST* jvst, jack_nframes_t nframes)
{
	struct AEffect* plugin = jvst->fst->plugin;

	void *port_buffer = jack_port_get_buffer( jvst->midi_inport, nframes );
	jack_nframes_t num_jackevents = jack_midi_get_event_count( port_buffer );
	jack_midi_event_t jackevent;
	unsigned short i, j, stuffed_events = 0;

	if( num_jackevents >= MIDI_EVENT_MAX )
		num_jackevents = MIDI_EVENT_MAX;

	for( i=0; i < num_jackevents; i++ ) {
		if ( jack_midi_event_get( &jackevent, port_buffer, i ) != 0 )
			break;

		// SysEx
		if ( jackevent.buffer[0] == SYSEX_BEGIN) {
			/* FIXME:
			we shoudn't call malloc in RT thread
			but I have no better idea :-(
			this memory will be free in sysex handler function
			*/
			struct SysExEvent* sysex_event = malloc(sizeof(struct SysExEvent));
                        sysex_event->data = malloc(jackevent.size);
                        sysex_event->jvst = jvst;
                        sysex_event->size = jackevent.size;
                        memcpy(sysex_event->data, jackevent.buffer, jackevent.size);
                        g_idle_add( (GSourceFunc) jvst_sysex_handler, sysex_event);
	
			/* TODO:
			For now we simply drop all SysEx messages because VST standard
			require special Event type for this (kVstSysExType)
			and this type is not supported (yet ;-)
			*/
			continue;
		}

		// Midi channel
		if ( (jvst->channel > 0 )
			&&  ( jackevent.buffer[0] >= 0x80 && jackevent.buffer[0] <= 0xEF)
			&&  ( (jackevent.buffer[0] & 0x0F) != jvst->channel - 1) )
			continue;

		// CC assigments
		if ( (jackevent.buffer[0] & 0xF0) == 0xB0 ) {
			short CC = jackevent.buffer[1];
			short VALUE = jackevent.buffer[2];

			// Want Mode
			if (CC == jvst->want_state_cc) {
				// 0-63 mean want bypass
				if (VALUE >= 0 && VALUE <= 63) {
					jvst->want_state = WANT_STATE_BYPASS;
				// 64-127 mean want resume
				} else if (VALUE > 63 && VALUE <= 127) {
					jvst->want_state = WANT_STATE_RESUME;
				// other values are wrong
				} else {
					jvst->want_state = WANT_STATE_NO;
				}
				continue;
			}
			// If Volume control is enabled then grab CC7 messages
			if ( (jvst->volume != -1) && (CC == 7) ) {
				jvst_set_volume(jvst, VALUE);
				continue;
			}
			// In bypass mode do not touch plugin
			if (jvst->bypassed)
				continue;
			// Mapping MIDI CC
			if ( jvst->midi_learn ) {
				jvst->midi_learn_CC = CC;
			// handle mapped MIDI CC
			} else if ( jvst->midi_map[CC] != -1 ) {
				int parameter = jvst->midi_map[CC];
				float value = 1.0/127.0 * (float) VALUE;
				plugin->setParameter( plugin, parameter, value );
			}
		}

		// ... wanna play ?
		if ( jvst->bypassed || ! jvst->want_midi_in )
			continue;
		
		// ... let's the music play
		jvst->event_array[stuffed_events].type = kVstMidiType;
		jvst->event_array[stuffed_events].byteSize = 24;
		jvst->event_array[stuffed_events].deltaFrames = jackevent.time;

		for (j=0; j < 4; j++) {
			jvst->event_array[stuffed_events].midiData[j] = 
				(j<jackevent.size) ? jackevent.buffer[j] : 0;
		}
		stuffed_events += 1;
	}

	if ( stuffed_events > 0 ) {
		jvst->events->numEvents = stuffed_events;
		plugin->dispatcher (plugin, effProcessEvents, 0, 0, jvst->events, 0.0f);
	}
}

// This function is used in audiomaster.c
void
queue_midi_message(JackVST* jvst, int status, int d1, int d2, jack_nframes_t delta )
{
	jack_ringbuffer_t* ringbuffer;
	int	written;
	int	statusHi = (status >> 4) & 0xF;
	int	statusLo = status & 0xF;
	struct  MidiMessage ev;

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

static int
process_callback( jack_nframes_t nframes, void* data) 
{
	short i, o;
	JackVST* jvst = (JackVST*) data;
	struct AEffect* plugin = jvst->fst->plugin;

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

	// Process MIDI Input
	// NOTE: we process MIDI even in bypass mode bacause of want_state handling
	process_midi_input(jvst, nframes);

	// Bypass - because all audio jobs are done  - simply return
	if (jvst->bypassed)
		goto midi_out;

	// Deal with plugin
	if (plugin->flags & effFlagsCanReplacing) {
		plugin->processReplacing (plugin, jvst->ins, jvst->outs, nframes);
	} else {
		plugin->process (plugin, jvst->ins, jvst->outs, nframes);
	}

	// Output volume control - if enabled
	if (jvst->volume == -1)
		goto midi_out;

	jack_nframes_t n=0;
	for(o=0; o < jvst->numOuts; ) {
		jvst->outs[o][n] *= jvst->volume;
		if (n < nframes) {
			n++;
		} else {
			++o;
			n=0;
		}
	}

midi_out:
	// Process MIDI Output
	process_midi_output(jvst, nframes);

	return 0;      
}

static bool
session_callback( JackVST* jvst )
{
	printf("session callback\n");

	jack_session_event_t *event = jvst->session_event;

	char retval[256];
	char filename[MAX_PATH];

	snprintf( filename, sizeof(filename), "%sstate.fps", event->session_dir );
	if ( ! jvst_save_state( jvst, filename ) )
		event->flags |= JackSessionSaveError;

	snprintf( retval, sizeof(retval), "%s -U %d -u %s -s \"${SESSION_DIR}state.fps\" \"%s\"",
		my_motherfuckin_name, jvst->sysex_dump.uuid, event->client_uuid, jvst->handle->path );
	event->command_line = strdup( retval );

	jack_session_reply(jvst->client, event);

	if (event->type == JackSessionSaveAndQuit) {
		printf("JackSession manager ask for quit\n");
		jvst_quit(jvst);
	}

	jack_session_event_free(event);

	return FALSE;
}

static void
session_callback_aux( jack_session_event_t *event, void* arg )
{
	JackVST* jvst = (JackVST*) arg;

        jvst->session_event = event;

        g_idle_add( (GSourceFunc) session_callback, jvst );
}

int
jvst_connect(JackVST *jvst, const char *audio_to)
{
	unsigned short i,j;
	const char *pname;
	const char **jports;
	jack_port_t* port;

	// Connect audio port
	jports = jack_get_ports(jvst->client, audio_to, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
	if (jports == NULL) {
		printf("Can't find any ports for %s\n", audio_to);
		return 0;
	}

	for (i=0, j=0; jports[i] != NULL && j < jvst->numOuts; i++) {
		pname = jack_port_name(jvst->outports[j++]);
		jack_connect(jvst->client, pname, jports[i]);
		printf("Connect: %s -> %s\n", pname, jports[i]);
	}
	jack_free(jports);
}

static bool
jvst_idle_cb(JackVST* jvst)
{
	const char **jports;
	jack_port_t* port;
	unsigned short i;

	// Check state
	if (jvst->want_state == WANT_STATE_BYPASS && !jvst->bypassed) {
		jvst->want_state = WANT_STATE_NO;
		jvst->bypassed = TRUE;
		fst_suspend(jvst->fst);
	} else if (jvst->want_state == WANT_STATE_RESUME && jvst->bypassed) {
		jvst->want_state = WANT_STATE_NO;
		fst_resume(jvst->fst);
		jvst->bypassed = FALSE;
	}

	// Send notify if something change
	if (jvst->sysex_want_notify && jvst->fst->want_program == -1) {
		SysExDumpV1* d = &jvst->sysex_dump;
		if ( d->program != jvst->fst->current_program ||
		     d->channel != jvst->channel ||
		     d->state   != ( (jvst->bypassed) ? SYSEX_STATE_NOACTIVE : SYSEX_STATE_ACTIVE ) ||
		     d->volume  != jvst_get_volume(jvst)
		) jvst_send_sysex(jvst, SYSEX_WANT_DUMP);
	}

	// Connect MIDI ports to control app
	jports = jack_get_ports(jvst->client, ControlAppName, JACK_DEFAULT_MIDI_TYPE, 0);
	if (jports == NULL) return TRUE;

	for (i=0; jports[i] != NULL; i++) {
		port = jack_port_by_name(jvst->client, jports[i]);

		// Skip mine
		if ( jack_port_is_mine(jvst->client, port) )
			continue;

		if (jack_port_flags(port) & JackPortIsInput) {
			if ( jack_port_connected_to(jvst->midi_outport, jports[i]) )
				continue;

			printf("Connect to: %s\n", jports[i]);
			jack_connect(jvst->client, jack_port_name(jvst->midi_outport), jports[i]);
		} else if (jack_port_flags(port) & JackPortIsOutput) {
			if ( jack_port_connected_to(jvst->midi_inport, jports[i]) )
				continue;

			jack_connect(jvst->client, jports[i], jack_port_name(jvst->midi_inport));
			printf("Connect to: %s\n", jports[i]);
		}

	}
        jack_free(jports);

	return TRUE;
}

static void
usage(char* appname) {
	const char* format = "%-20s%s\n";

	fprintf(stderr, "\nUsage: %s [ options ] <plugin>\n", appname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, format, "-b", "Bypass");
	fprintf(stderr, format, "-n", "Disable Editor and GTK GUI");
	fprintf(stderr, format, "-N", "Notify changes by SysEx");
	fprintf(stderr, format, "-e", "Hide Editor");
	fprintf(stderr, format, "-s state_file", "Load state_file");
	fprintf(stderr, format, "-c client_name", "Jack Client name");
	fprintf(stderr, format, "-k channel", "MIDI Channel (0: all, 17: none)");
	fprintf(stderr, format, "-i num_in", "Jack number In ports");
	fprintf(stderr, format, "-j connect_to", "Connect Audio Out to connect_to");
	fprintf(stderr, format, "-l", "save state to state_file on SIGUSR1 - require -s");
	fprintf(stderr, format, "-m mode_midi_cc", "Bypass/Resume MIDI CC (default: 122)");
	fprintf(stderr, format, "-o num_out", "Jack number Out ports");
	fprintf(stderr, format, "-t tempo", "Set fixed Tempo rather than using JackTransport");
	fprintf(stderr, format, "-u uuid", "JackSession UUID");
	fprintf(stderr, format, "-U uuid", "SysEx ID");
	fprintf(stderr, format, "-V", "Disable Volume control / filtering CC7 messages");
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow)
{
	LPWSTR*		szArgList;
	int		argc;
	char**		argv;
	FST*		fst;
	struct AEffect*	plugin;
	short		i;
	short		opt_numIns = 0;
	short		opt_numOuts = 0;
	bool		load_state = FALSE;
	bool		sigusr1_save_state = FALSE;
	const char*	connect_to = NULL;
	const char*	plug_path;

	int		sample_rate = 0;
	long		block_size = 0;

	printf("FSTHost Version: %s\n", VERSION);

	JackVST*	jvst = jvst_new();

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
	strcpy(argv[0], my_motherfuckin_name); // Force APP name

        // Parse command line options
	while ( (i = getopt (argc, argv, "bes:c:k:i:j:lnNm:o:t:u:U:V")) != -1) {
		switch (i) {
			case 'b':
				jvst->bypassed = TRUE;
				break;
			case 'e':
				jvst->with_editor = WITH_EDITOR_HIDE;
				break;
			case 's':
				load_state = 1;
                                jvst->default_state_file = optarg;
				break;
			case 'c':
				jvst->client_name = optarg;
				break;
			case 'k':
				jvst->channel = strtol(optarg, NULL, 10);
				if (jvst->channel < 0 || jvst->channel > 17)
					jvst->channel = 0;
				break;
			case 'i':
				opt_numIns = strtol(optarg, NULL, 10);
				break;
			case 'j':
				connect_to = optarg;
				break;
			case 'l':
				sigusr1_save_state = TRUE;
				break;
			case 'o':
				opt_numOuts = strtol(optarg, NULL, 10);
				break;
			case 'n':
				jvst->with_editor = WITH_EDITOR_NO;
				break;
			case 'N':
				jvst->sysex_want_notify = true;
				break;
			case 'm':
				jvst->want_state_cc = strtol(optarg, NULL, 10);
				break;
			case 't':
				jvst->tempo = strtod(optarg, NULL);
				break;
			case 'u':
				jvst->uuid = strtol(optarg, NULL, 10);
				break;
			case 'U':
				jvst->sysex_ident_reply.model[1] =
				jvst->sysex_dump.uuid = strtol(optarg, NULL, 10);
				break;
			case 'V':
				jvst->volume = -1;
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

	plug_path = argv[optind];
	jvst_first = jvst;

	jvst_set_volume(jvst, 63);

	printf( "yo... lets see...\n" );
	if ((jvst->handle = fst_load (plug_path)) == NULL) {
		fst_error ("can't load plugin %s", plug_path);
		return 1;
	}

	if (!jvst->client_name)
		jvst->client_name = jvst->handle->name;

	printf( "Revive plugin: %s\n", jvst->client_name);
	if ((jvst->fst = fst_open (jvst->handle, (audioMasterCallback) jack_host_callback, jvst)) == NULL) {
		fst_error ("can't instantiate plugin %s", plug_path);
		return 1;
	}

        printf("Main Thread WineID: %d | LWP: %d\n", GetCurrentThreadId (), (int) syscall (SYS_gettid));

	fst = jvst->fst;
	plugin = fst->plugin;

	printf("Start Jack thread ...\n");
	char struuid[6];
	snprintf(struuid, 6, "%d", jvst->uuid);
	jvst->client = jack_client_open(jvst->client_name,JackSessionID,NULL,struuid);
	if (! jvst->client) {
		fst_error ("can't connect to JACK");
		return 1;
	}

	/* set rate and blocksize */
	sample_rate = (int) jack_get_sample_rate(jvst->client);
	block_size = jack_get_buffer_size(jvst->client);

	printf("Sample Rate = %d\n", sample_rate);
	printf("Block Size = %ld\n", block_size);

	plugin->dispatcher (plugin, effSetSampleRate, 0, 0, NULL, (float) sample_rate);
	plugin->dispatcher (plugin, effSetBlockSize, 0, jack_get_buffer_size (jvst->client), NULL, 0.0f);

	// ok.... plugin is running... lets bind to lash...
	
	jvst->midi_inport = 
		jack_port_register(jvst->client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	jvst->midi_outport =
		jack_port_register(jvst->client, "midi-out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if (fst->vst_version >= 2) {
		/* should we send the plugin VST events (i.e. MIDI) */
		if (fst->isSynth || fst->canReceiveVstEvents || fst->canReceiveVstMidiEvent) {
			jvst->want_midi_in = TRUE;

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
			jvst->want_midi_out = TRUE;
			jvst->ringbuffer = jack_ringbuffer_create(RINGBUFFER_SIZE);
			if (jvst->ringbuffer == NULL) {
				fst_error("Cannot create JACK ringbuffer.");
				return 1;
			}

			jack_ringbuffer_mlock(jvst->ringbuffer);
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

	// Save state on signal SIGUSR1 - mostly for ladish support
	if (sigusr1_save_state && jvst->default_state_file) {
		struct sigaction sa_sigusr1;
		memset(&sa_sigusr1, 0, sizeof(struct sigaction));
		sa_sigusr1.sa_sigaction = &sigusr1_handler;
		sa_sigusr1.sa_flags = SA_SIGINFO;
		sigaction(SIGUSR1, &sa_sigusr1, NULL);
	}

	// Handling SIGINT for clean quit
	struct sigaction sa_sigint;
	memset(&sa_sigint, 0, sizeof(struct sigaction));
	sa_sigint.sa_sigaction = &sigint_handler;
	sa_sigint.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sa_sigint, NULL);

#ifdef HAVE_LASH
	lash_event_t *event;
	lash_args_t* lash_args = lash_extract_args(&argc, &argv);

	int flags = LASH_Config_Data_Set;

	lash_client = lash_init(lash_args, jvst->client_name, flags, LASH_PROTOCOL(2, 0));

	if (!lash_client) {
		fprintf(stderr, "%s: could not initialise lash\n", __FUNCTION__);
		fprintf(stderr, "%s: running fst without lash session-support\n", __FUNCTION__);
		fprintf(stderr, "%s: to enable lash session-support launch the lash server prior fst\n", __FUNCTION__);
	}

	if (lash_enabled(lash_client)) {
		event = lash_event_new_with_type(LASH_Client_Name);
		lash_event_set_string(event, jvst->client_name);
		lash_send_event(lash_client, event);

		event = lash_event_new_with_type(LASH_Jack_Client_Name);
		lash_event_set_string(event, jvst->client_name);
		lash_send_event(lash_client, event);
	}
#endif
        // load state if requested
	if ( load_state && ! jvst_load_state (jvst, jvst->default_state_file) && ! sigusr1_save_state )
		return 1;

	// Activate plugin
	if (! jvst->bypassed) fst_resume(jvst->fst);

	printf("Jack Activate\n");
	jack_activate(jvst->client);

	// Init Glib main event loop
	glib_main_loop = g_main_loop_new(NULL, FALSE);
	g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 750,
		(GSourceFunc) jvst_idle_cb, jvst, NULL);

	// Auto connect on start
	if (connect_to)
		jvst_connect(jvst, connect_to);

	// Create GTK or GlibMain thread
	if (jvst->with_editor != WITH_EDITOR_NO) {
		printf( "Start GUI\n" );
		gtk_gui_init (&argc, &argv);

		if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) &gtk_gui_start, jvst, 0, NULL) == NULL) {
			fst_error ("could not create GTK Thread");
			return FALSE;
		}
	} else {
		printf("GUI Disabled - start GlibMainLoop\n");
		if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) &g_main_loop_run, glib_main_loop, 0, NULL) == NULL) {
			fst_error ("could not create GlibMainLoop thread");
			return FALSE;
		}
	}

	printf("Start FST GUI/event loop\n");
	fst_event_loop(hInst);

	printf("Unload plugin\n");
	fst_unload(jvst->handle);

	jvst_destroy(jvst);

	printf("Game Over\n");

	return 0;
}
