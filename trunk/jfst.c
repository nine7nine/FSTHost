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
#include <errno.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include "jackvst.h"
#include <jack/thread.h>

#define CTRLAPP "FHControl"
#define VERSION "1.5.5"
#ifdef __x86_64__
#define ARCH "64"
#else
#define ARCH "32"
#endif
#define APPNAME "fsthost"
#define APPNAME_ARCH APPNAME ARCH

#define RINGBUFFER_SIZE 16 * sizeof(struct MidiMessage)
#define SYSEX_RINGBUFFER_SIZE 16 * SYSEX_MAX_SIZE
//#define SEP_THREAD

/* gtk.c */
extern void gtk_gui_init (int* argc, char** argv[]);
extern int gtk_gui_start (JackVST * jvst);
extern void gtk_gui_quit();

/* list.c */
extern char* fst_info_default_path(const char* appname);
extern int fst_info_list(const char* dbpath);

/* jvstproto.c */
bool jvst_proto_init ( JackVST* jvst );
bool jvst_proto_close ( JackVST* jvst );

/* lash.c */
#ifdef HAVE_LASH
extern void jvst_lash_init(JackVST *jvst, int* argc, char** argv[]);
#endif

GMainLoop* glib_main_loop;
volatile JackVST *jvst_first = NULL;

static void sysex_makeASCII(uint8_t* ascii_midi_dest, char* name, size_t size_dest) {
	size_t i;
	for (i=0; i < strlen(name) && i < size_dest - 1; i++)
		if ( isprint( toascii( name[i]) ) )
			ascii_midi_dest[i] = name[i];
	memset(ascii_midi_dest + i, 0, size_dest - i - 1); /* Set rest to 0 */
}

static void jvst_generate_random_id(JackVST* jvst) {
	srand(GetTickCount()); /* Init ramdom generator */
	printf("Random SysEx ID:");
	unsigned short g;
	for(g=0; g < sizeof(jvst->sysex_ident_reply.version); g++) {
		jvst->sysex_ident_reply.version[g] = rand() % 128;
		printf(" %02X", jvst->sysex_ident_reply.version[g]);
	}
	putchar('\n');
}

// Prepare data for RT thread and wait for send
void jvst_send_sysex(JackVST* jvst, enum SysExWant sysex_want) {
	/* Do not send anything if we are not connected */
	if (! jack_port_connected ( jvst->ctrl_outport  ) ) return;

	pthread_mutex_lock (&jvst->sysex_lock);

	uint8_t id;
	if (sysex_want == SYSEX_WANT_DUMP) {
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
	} else {
		/* Assume WANT_IDENT_REPLY */
		id = jvst->sysex_ident_reply.model[0];
	}

	jvst->sysex_want = sysex_want;
	pthread_cond_wait (&jvst->sysex_sent, &jvst->sysex_lock);
	pthread_mutex_unlock (&jvst->sysex_lock);
	printf("SysEx Dumped (type: %d ID: %d)\n", sysex_want, id);
}

static void jvst_queue_sysex(JackVST* jvst, jack_midi_data_t* data, size_t size) {
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
static void jvst_parse_sysex_input(JackVST* jvst, jack_midi_data_t* data, size_t size) {
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
				printf("DUMP - ");

				SysExDumpV1* sysex = (SysExDumpV1*) data;
				if (sysex->uuid != jvst->sysex_dump.uuid) {
					printf("Not to Us (ID:%d)\n", sysex->uuid);
					break;
				}

				printf("OK | uuid:%d state:%d program:%d channel:%d volume:%d\n", sysex->uuid,
					sysex->state, sysex->program, sysex->channel, sysex->volume);
				jvst_bypass(jvst, (sysex->state == SYSEX_STATE_ACTIVE) ? FALSE : TRUE);
				fst_program_change(jvst->fst, sysex->program);
				midi_filter_one_channel_set(&jvst->channel, sysex->channel);
				jvst_set_volume(jvst, sysex->volume);

				// Copy sysex state for preserve resending SysEx Dump
				memcpy(&jvst->sysex_dump,sysex,sizeof(SysExDumpV1));
				break;
			case SYSEX_TYPE_RQST: ;
				SysExDumpRequestV1* sysex_request = (SysExDumpRequestV1*) data;
				printf("REQUEST - ID %X - ", sysex_request->uuid);
				if (sysex_request->uuid == jvst->sysex_dump.uuid) {
					puts("OK");
					jvst_send_sysex(jvst, SYSEX_WANT_DUMP);
				} else {
					puts("Not to Us");
				}
				/* If we got DumpRequest then it mean FHControl is here and we wanna notify */
				jvst->sysex_want_notify = true;
				break;
			case SYSEX_TYPE_OFFER: ;
				SysExIdOffer* sysex_id_offer = (SysExIdOffer*) data;
				printf("ID OFFER - %X - ", sysex_id_offer->uuid);
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
			default:
				puts("BROKEN");
			}
			break;
		default:
			puts("not supported");
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

static void jvst_sysex_handler(JackVST* jvst) {
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

void jvst_quit(JackVST* jvst) {
#ifdef NO_GTK
	g_main_loop_quit(glib_main_loop);
#else
	if (jvst->with_editor == WITH_EDITOR_NO) {
		g_main_loop_quit(glib_main_loop);
	} else {
		gtk_gui_quit();
	}
#endif
}

static void signal_handler (int signum) {
	JackVST *jvst = (JackVST*) jvst_first;

	switch(signum) {
	case SIGINT:
		puts("Caught signal to terminate (SIGINT)");
		g_idle_add( (GSourceFunc) jvst_quit, jvst);
		break;
	case SIGUSR1:
		puts("Caught signal to save state (SIGUSR1)");
		jvst_save_state(jvst, jvst->default_state_file);
		break;
	case SIGUSR2:
		puts("Caught signal to open editor (SIGUSR2)");
		g_idle_add( (GSourceFunc) fst_run_editor, jvst->fst);
		break;
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

        printf("Audio Thread W32ID: %d | LWP: %d\n", GetCurrentThreadId (), (int) syscall (SYS_gettid));

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

static inline void process_midi_output(JackVST* jvst, jack_nframes_t nframes) {
	if (! jvst->want_midi_out) return;
	// Do not process anything if MIDI OUT port is not connected
	if (! jack_port_connected ( jvst->midi_outport ) ) return;

	/* This jack ringbuffer consume code was largely taken from jack-keyboard */
	/* written by Edward Tomasz Napierala <trasz@FreeBSD.org>                 */
	void *port_buffer = jack_port_get_buffer(jvst->midi_outport, nframes);
	/* We need always clear buffer if port is connected someware */
	jack_midi_clear_buffer(port_buffer);

	jack_nframes_t last_frame_time = jack_last_frame_time(jvst->client);
	jack_ringbuffer_t* ringbuffer = jvst->ringbuffer;
	while (jack_ringbuffer_read_space(ringbuffer)) {
		struct MidiMessage ev;
		int read = jack_ringbuffer_peek(ringbuffer, (char*)&ev, sizeof(ev));
		if (read != sizeof(ev)) {
			fst_error("Short read from the ringbuffer, possible note loss.");
			jack_ringbuffer_read_advance(ringbuffer, read);
			continue;
		}

		int t = ev.time + nframes - last_frame_time;

		/* If computed time is too much into the future, we'll send it later. */
		if (t >= nframes) return;

		/* If computed time is < 0, we missed a cycle because of xrun. */
		if (t < 0) t = 0;

		jack_ringbuffer_read_advance(ringbuffer, sizeof(ev));

		if ( jack_midi_event_write(port_buffer, t, ev.data, ev.len) )
			fst_error("queue: jack_midi_event_write failed, NOTE LOST.");
	}
}

static inline void process_ctrl_output(JackVST* jvst, jack_nframes_t nframes) {
	if ( ! jack_port_connected ( jvst->ctrl_outport ) ) return;

	void *port_buffer = jack_port_get_buffer(jvst->ctrl_outport, nframes);
	/* We need always clear buffer if port is connected someware */
	jack_midi_clear_buffer(port_buffer);

	if (jvst->sysex_want == SYSEX_WANT_NO) return;

	// Are our lock is ready for us ?
	// If not then we try next time
	if (pthread_mutex_trylock(&jvst->sysex_lock) != 0) return;

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

/* True if this message should be passed to plugin */
static inline void process_midi_in_msg ( JackVST* jvst, jack_midi_event_t* jackevent, VstEvents* events, bool ctrl )
{
	/* TODO: SysEx
	For now we simply drop all SysEx messages because VST standard
	require special Event type for this (kVstSysExType)
	and this type is not supported (yet ;-)
	*/
	if ( jackevent->buffer[0] == SYSEX_BEGIN) return;

	/* Copy this MIDI event beacuse Jack gives same buffer to all clients and we cannot work on this data */
	jack_midi_data_t buf[jackevent->size];
	memcpy(&buf, jackevent->buffer, jackevent->size);

	/* MIDI FILTERS */
	if ( ! midi_filter_check( &jvst->filters, (uint8_t*) &buf, jackevent->size ) ) return;

	switch ( (buf[0] >> 4) & 0xF ) {
	case MM_CONTROL_CHANGE: ;
		// CC assigments
		uint8_t CC = buf[1];
		uint8_t VALUE = buf[2];

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
			return;
		// If Volume control is enabled then grab CC7 messages
		} else if (CC == 7 && jvst->volume != -1) {
			jvst_set_volume(jvst, VALUE);
			return;
		}

		// In bypass mode do not touch plugin
		if (jvst->bypassed) return;

		// Mapping MIDI CC
		if ( jvst->midi_learn ) {
			jvst->midi_learn_CC = CC;
		// handle mapped MIDI CC
		} else if ( jvst->midi_map[CC] != -1 ) {
			int32_t parameter = jvst->midi_map[CC];
			float value = 1.0/127.0 * (float) VALUE;
			AEffect* plugin = jvst->fst->plugin;
			plugin->setParameter( plugin, parameter, value );
		}
		break;
	case MM_PROGRAM_CHANGE:
		// Self Program Change
		if (jvst->midi_pc != MIDI_PC_SELF) break;
		jvst->midi_pc = buf[1];
		// OFC don't forward this message to plugin
		return;
	}

	// ... wanna play at all ?
	if ( jvst->bypassed || ! jvst->want_midi_in || ctrl ) return;
	
	/* Prepare MIDI events */
	VstMidiEvent* midi_event = (VstMidiEvent*) events->events[events->numEvents];
	midi_event->type = kVstMidiType;
	midi_event->byteSize = sizeof (VstMidiEvent);
	midi_event->deltaFrames = jackevent->time;
	midi_event->flags = kVstMidiEventIsRealtime; // All our MIDI data are realtime, it's clear ?

	uint8_t j;
	for (j=0; j < 3; j++) midi_event->midiData[j] = ( j < jackevent->size ) ? buf[j] : 0;
	/* event_array[3] remain 0 (according to VST Spec) */
	midi_event->midiData[3] = 0;

	events->numEvents++;
}

static inline void process_ctrl_input(JackVST* jvst, jack_nframes_t nframes) {
	// Do not process anything if MIDI IN port is not connected
	if ( ! jack_port_connected ( jvst->ctrl_inport ) ) return;
	
	void *port_buffer = jack_port_get_buffer( jvst->ctrl_inport, nframes );
	jack_nframes_t num_jackevents = jack_midi_get_event_count( port_buffer );
	jack_nframes_t i;
	for( i=0; i < num_jackevents; i++ ) {
		jack_midi_event_t jackevent;
		if ( jack_midi_event_get( &jackevent, port_buffer, i ) ) break;

		process_midi_in_msg ( jvst, &jackevent, NULL, TRUE );

		/* Drop all not SysEx messages */
		if ( jackevent.buffer[0] != SYSEX_BEGIN) continue;

		if (jackevent.size > SYSEX_MAX_SIZE) {
			fst_error("Sysex is too big. Skip. Requested %d, but MAX is %d", jackevent.size, SYSEX_MAX_SIZE);
		} else {
			jvst_queue_sysex(jvst, jackevent.buffer, jackevent.size);
		}
	}
}

static int process_callback( jack_nframes_t nframes, void* data) {
	int32_t i;
	JackVST* jvst = (JackVST*) data;
	AEffect* plugin = jvst->fst->plugin;

	// Process MIDI Input
	// NOTE: we process MIDI even in bypass mode bacause of want_state handling
	if ( ! jvst->want_midi_in ) goto no_midi_in;

	// Do not process anything if MIDI IN port is not connected
	if ( ! jack_port_connected ( jvst->midi_inport ) ) goto no_midi_in;

	void *port_buffer = jack_port_get_buffer( jvst->midi_inport, nframes );
	jack_nframes_t num_jackevents = jack_midi_get_event_count( port_buffer );

	// Allocate space for VST MIDI
	// NOTE: can't use VLA here because of goto above
	size_t size = sizeof(VstEvents) + ((num_jackevents - 2) * sizeof(VstEvent*));
	VstEvents* events = alloca ( size );
	VstMidiEvent* event_array = alloca ( num_jackevents * sizeof(VstMidiEvent) );
	memset ( event_array, 0, num_jackevents * sizeof(VstMidiEvent) );

	events->numEvents = 0;
	for (i = 0; i < num_jackevents; i++) {
		jack_midi_event_t jackevent;
		if ( jack_midi_event_get( &jackevent, port_buffer, i ) != 0 ) break;

		/* Bind MIDI event to collection */
		events->events[i] = (VstEvent*) &( event_array[i] );

		process_midi_in_msg ( jvst, &jackevent, events, FALSE );
	}

	// ... let's the music play
	if ( events->numEvents == 0 ) goto no_midi_in;
	plugin->dispatcher (plugin, effProcessEvents, 0, 0, events, 0.0f);

no_midi_in:
	process_ctrl_input(jvst, nframes);

	/* Process AUDIO */
	float* ins[plugin->numInputs];
	float* outs[plugin->numOutputs];

	// swap area ( nonused ports )
	float swap[jvst->buffer_size];
	memset ( swap, 0, jvst->buffer_size * sizeof(float) );

	// Get addresses of input buffers
	for (i = 0; i < plugin->numInputs; ++i) {
		if ( i < jvst->numIns ) { 
			ins[i] = (float*) jack_port_get_buffer (jvst->inports[i], nframes);
		} else {
			ins[i] = swap;
		}
	}

	// Initialize output buffers
	for (i = 0; i < plugin->numOutputs; ++i) {
		if ( i < jvst->numOuts ) {
			// Get address
			outs[i]  = (float*) jack_port_get_buffer (jvst->outports[i], nframes);
	
			// If bypassed then copy In's to Out's
			if ( jvst->bypassed && i < jvst->numIns ) {
				memcpy ( outs[i], ins[i], sizeof (float) * nframes );
			// Zeroing output buffers
			} else {
				memset ( outs[i], 0, sizeof (float) * nframes );
			}
		} else {
			outs[i] = swap;
		}
	}

	// Bypass - because all audio jobs are done  - simply return
	if (jvst->bypassed) goto midi_out;

	// Deal with plugin
	if (plugin->flags & effFlagsCanReplacing) {
		plugin->processReplacing (plugin, ins, outs, nframes);
	} else {
		plugin->process (plugin, ins, outs, nframes);
	}

#ifndef NO_VUMETER
	/* Compute output level for VU Meter */
	float avg_level = 0;
	jack_nframes_t n;
	for (n=0; n < nframes; n++) avg_level += fabs( outs[0][n] );
	avg_level /= nframes;

	jvst->out_level = avg_level * 100;
	if (jvst->out_level > 100) jvst->out_level = 100;
#endif

	jvst_apply_volume ( jvst, nframes, outs );

midi_out:
	// Process MIDI Output
	process_midi_output(jvst, nframes);
	process_ctrl_output(jvst, nframes);

	return 0;
}

static bool session_callback( JackVST* jvst ) {
	puts("session callback");

	jack_session_event_t *event = jvst->session_event;

	// Save state
	char filename[MAX_PATH];
	snprintf( filename, sizeof(filename), "%sstate.fps", event->session_dir );
	if ( ! jvst_save_state( jvst, filename ) ) {
		puts("SAVE ERROR");
		event->flags |= JackSessionSaveError;
	}

	// Reply to session manager
	char retval[256];
	snprintf( retval, sizeof(retval), "%s -u %s -s \"${SESSION_DIR}state.fps\"", APPNAME_ARCH, event->client_uuid);
	event->command_line = strndup( retval, strlen(retval) + 1  );

	jack_session_reply(jvst->client, event);

	if (event->type == JackSessionSaveAndQuit) {
		puts("JackSession manager ask for quit");
		jack_session_event_free(event);
		jvst_quit(jvst);
	}

	jack_session_event_free(event);

	return FALSE;
}

static void session_callback_aux( jack_session_event_t *event, void* arg ) {
	JackVST* jvst = (JackVST*) arg;

        jvst->session_event = event;

        g_idle_add( (GSourceFunc) session_callback, jvst );
}

static inline bool jack_connect_wrap ( jack_client_t* client , const char* source_port, const char* destination_port ) {
	int ret = jack_connect( client, source_port, destination_port );
	if ( ret == EEXIST ) return FALSE;
	printf( "Connect: %s -> %s [ %s ]\n", source_port, destination_port, (ret==0)?"DONE":"FAIL" );
	return (ret==0) ? TRUE : FALSE;
}

static void jvst_connect_audio(JackVST *jvst, const char *audio_to) {
	// Connect audio port
	const char **jports = jack_get_ports(jvst->client, audio_to, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
	if (!jports) {
		printf("Can't find any ports for %s\n", audio_to);
		return;
	}

	const char *pname;
	unsigned short i;
	for (i=0; jports[i] && i < jvst->numOuts; i++) {
		pname = jack_port_name(jvst->outports[i]);
		jack_connect_wrap ( jvst->client, pname, jports[i] );
	}
	jack_free(jports);
}

static void jvst_connect_midi_to_physical(JackVST* jvst) {
	if ( ! jvst->want_midi_in ) return;

	const char **jports = jack_get_ports(jvst->client, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput|JackPortIsPhysical);
        if (!jports) return;

	const char *pname = jack_port_name(jvst->midi_inport);
	unsigned short i;
        for (i=0; jports[i]; i++)
		jack_connect_wrap (jvst->client, jports[i], pname);

        jack_free(jports);
}

static int graph_order_callback( void *arg ) {
	JackVST* jvst = arg;
	jvst->graph_order_change = TRUE;
	return 0;
}

static inline void jvst_sysex_notify(JackVST* jvst) {
	// Wait until program change
	if (jvst->fst->event_call.type != PROGRAM_CHANGE) return;
	// Do not notify if have not SysEx ID
	if (jvst->sysex_ident_reply.model[0] == SYSEX_AUTO_ID) return;

	SysExDumpV1* d = &jvst->sysex_dump;
	if ( d->program != jvst->fst->current_program ||
		d->channel != midi_filter_one_channel_get( &jvst->channel ) ||
		d->state   != ( (jvst->bypassed) ? SYSEX_STATE_NOACTIVE : SYSEX_STATE_ACTIVE ) ||
		d->volume  != jvst_get_volume(jvst)
	) jvst_send_sysex(jvst, SYSEX_WANT_DUMP);
}

static inline void jvst_connect_to_ctrl_app(JackVST* jvst) {
	const char **jports = jack_get_ports(jvst->client, CTRLAPP, JACK_DEFAULT_MIDI_TYPE, 0);
	if (!jports) return;

	bool done = false;
	unsigned short i;
	for (i=0; jports[i]; i++) {
		const char *src, *dst;
		jack_port_t* port = jack_port_by_name(jvst->client, jports[i]);
		if (jack_port_flags(port) & JackPortIsInput) {
			/* Do not connect to forward input port */
			if ( strstr( jports[i], "forward" ) != NULL ) continue;
			port = jvst->ctrl_outport;
			src = jack_port_name(port);
			dst = jports[i];
		} else if (jack_port_flags(port) & JackPortIsOutput) {
			/* forward_output -> midi_in | output -> ctrl_in */
			if ( strstr( jports[i], "forward" ) != NULL ) {
				if ( ! jvst->want_midi_in ) continue;
				port = jvst->midi_inport;
			} else {
				port = jvst->ctrl_inport;
			}
			src = jports[i];
			dst = jack_port_name(port);
		} else continue;

		/* Already connected ? */
		if ( jack_port_connected_to(port, jports[i]) ) continue;

		if ( jack_connect_wrap (jvst->client, src, dst) )
			done = true;
	}
        jack_free(jports);

	/* Now we are connected to CTRL APP - send announce */
	if (done) jvst_send_sysex(jvst, SYSEX_WANT_IDENT_REPLY);
}

static bool jvst_idle(JackVST* jvst) {
	// Handle SysEx Input
	jvst_sysex_handler(jvst);

	// Check state
	switch(jvst->want_state) {
	case WANT_STATE_BYPASS: jvst_bypass(jvst,TRUE); break;
	case WANT_STATE_RESUME: jvst_bypass(jvst,FALSE); break;
	case WANT_STATE_NO:; /* because of GCC warning */
	}

	// Self Program change support
	if (jvst->midi_pc > MIDI_PC_SELF) {
		fst_program_change(jvst->fst, jvst->midi_pc);
		jvst->midi_pc = MIDI_PC_SELF;
	}

	// Attempt to connect MIDI ports to control app if Graph order change
	if (jvst->graph_order_change) {
		jvst->graph_order_change = FALSE;
		jvst_connect_to_ctrl_app(jvst);
	}

	// Send notify if we want notify and something change
	if (jvst->sysex_want_notify) jvst_sysex_notify(jvst);

	return TRUE;
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
	argv[0] = (char*) APPNAME_ARCH; // Force APP name
	*pargv = argv;
}

static void usage(char* appname) {
	const char* format = "%-20s%s\n";

	fprintf(stderr, "\nUsage: %s [ options ] <plugin>\n", appname);
	fprintf(stderr, "  or\n");
	fprintf(stderr, "Usage: %s -L [ -d <xml_db_info> ]\n", appname);
	fprintf(stderr, "  or\n");
	fprintf(stderr, "Usage: %s -g [ -d <xml_db_info> ] <path_for_add_to_db>\n", appname);
	fprintf(stderr, "  or\n");
	fprintf(stderr, "Usage: %s -s <FPS state file>\n\n", appname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, format, "-g", "Create/Update XML info DB.");
	fprintf(stderr, format, "-L", "List plugins from XML info DB.");
	fprintf(stderr, format, "-d xml_db_path", "Custom path to XML DB");
	fprintf(stderr, format, "-b", "Start in bypass mode");
	fprintf(stderr, format, "-n", "Disable Editor and GTK GUI");
	fprintf(stderr, format, "-N", "Notify changes by SysEx");
#ifndef NO_GTK
	fprintf(stderr, format, "-e", "Hide Editor");
#endif
	fprintf(stderr, format, "-s <state_file>", "Load <state_file>");
	fprintf(stderr, format, "-S <port>", "Start CTRL server on port <port>");
	fprintf(stderr, format, "-c <client_name>", "Jack Client name");
	fprintf(stderr, format, "-k channel", "MIDI Channel (0: all, 17: none)");
	fprintf(stderr, format, "-i num_in", "Jack number In ports");
	fprintf(stderr, format, "-j <connect_to>", "Connect Audio Out to <connect_to>");
	fprintf(stderr, format, "-l", "save state to state_file on SIGUSR1 (require -s)");
	fprintf(stderr, format, "-m mode_midi_cc", "Bypass/Resume MIDI CC (default: 122)");
	fprintf(stderr, format, "-p", "Connect MIDI In port to all physical");
	fprintf(stderr, format, "-P", "Self MIDI Program Change handling");
	fprintf(stderr, format, "-o num_out", "Jack number Out ports");
	fprintf(stderr, format, "-B", "Use BBT JackTransport sync");
	fprintf(stderr, format, "-t tempo", "Set fixed Tempo rather than using JackTransport");
	fprintf(stderr, format, "-u uuid", "JackSession UUID");
	fprintf(stderr, format, "-U SysExID", "SysEx ID (1-127). 0 is default (do not use it)");
	fprintf(stderr, format, "-V", "Disable Volume control / filtering CC7 messages");
}

static bool jvst_jack_init( JackVST* jvst ) {
	int32_t i;

	jack_set_info_function(jvst_log);
	jack_set_error_function(jvst_log);

	printf("Starting Jack thread ... ");
	jack_status_t status;
	jvst->client = jack_client_open(jvst->client_name,JackSessionID,&status,jvst->uuid);
	if (! jvst->client) {
		fst_error ("can't connect to JACK");
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
	jack_set_process_callback (jvst->client, (JackProcessCallback) process_callback, jvst);
	jack_set_session_callback( jvst->client, session_callback_aux, jvst );
	jack_set_graph_order_callback(jvst->client, graph_order_callback, jvst);

	/* set rate and blocksize */
	jvst->sample_rate = jack_get_sample_rate(jvst->client);
	jvst->buffer_size = jack_get_buffer_size(jvst->client);

	// Register input ports
	jvst->inports = malloc(sizeof(jack_port_t*) * jvst->numIns); // jack_port_t**
	mlock ( jvst->inports, sizeof(jack_port_t*) * jvst->numIns );
	for (i = 0; i < jvst->numIns; ++i) {
		char buf[6];
		snprintf (buf, sizeof(buf), "in%d", i+1);
		jvst->inports[i] = jack_port_register (jvst->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	}

	// Register output ports
	jvst->outports = malloc (sizeof(jack_port_t*) * jvst->numOuts); //jack_port_t**
	mlock ( jvst->outports, sizeof(jack_port_t*) * jvst->numOuts );
	for (i = 0; i < jvst->numOuts; ++i) {
		char buf[7];
		snprintf (buf, sizeof(buf), "out%d", i+1);
		jvst->outports[i] = jack_port_register (jvst->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	}

	// Register MIDI input port (if needed)
	if ( jvst->want_midi_in )
		jvst->midi_inport = jack_port_register(jvst->client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	// Register MIDI output port (if needed)
	if ( jvst->want_midi_out ) {
		jvst->midi_outport = jack_port_register(jvst->client, "midi-out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
		jvst->ringbuffer = jack_ringbuffer_create(RINGBUFFER_SIZE);
		if (! jvst->ringbuffer) {
			fst_error("Cannot create JACK ringbuffer.");
			return false;
		}
		jack_ringbuffer_mlock(jvst->ringbuffer);
	}

	// Register Control MIDI ports
	jvst->ctrl_inport = jack_port_register(jvst->client, "ctrl-in",
		JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	jvst->ctrl_outport = jack_port_register(jvst->client, "ctrl-out",
		JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	/* Init MIDI Input sysex buffer */
	jvst->sysex_ringbuffer = jack_ringbuffer_create(SYSEX_RINGBUFFER_SIZE);
	if (! jvst->sysex_ringbuffer) {
		fst_error("Cannot create JACK ringbuffer.");
		return false;
	}
	jack_ringbuffer_mlock(jvst->sysex_ringbuffer);

	return true;
}

static bool jvst_fst_init( JackVST* jvst ) {
	FST* fst = jvst->fst;

	// MIDI	
	/* No MIDI at all - very old/rare v1 plugins */
	if (fst->vst_version < 2) goto audio; 

	/**************** MIDI IN ***********************/
	if (fst->isSynth || fst->canReceiveVstEvents || fst->canReceiveVstMidiEvent)
		jvst->want_midi_in = true;

	/**************** MIDI OUT **********************/
	if (fst->canSendVstEvents || fst->canSendVstMidiEvent)
		jvst->want_midi_out = true;

audio:
	// Jack Audio
	if ( ! jvst_jack_init ( jvst ) ) return false;

	// Lock our crucial memory ( which is used in process callback )
	// TODO: this is not all
	mlock ( jvst, sizeof(JackVST) );
	mlock ( jvst->fst, sizeof(FST) );

	// Set block size / sample rate
	fst_call_dispatcher (fst, effSetSampleRate, 0, 0, NULL, (float) jvst->sample_rate);
	fst_call_dispatcher (fst, effSetBlockSize, 0, (intptr_t) jvst->buffer_size, NULL, 0.0f);
	printf("Sample Rate: %d | Block Size: %d\n", jvst->sample_rate, jvst->buffer_size);
	return true;
}

void jvst_cleanup ( JackVST* jvst ) {
	free ( jvst->inports );
	free ( jvst->outports );
}

#ifdef SEP_THREAD
const char* kibel;
sem_t chuj;
static DWORD WINAPI
sep_thread ( LPVOID arg ) {
	JackVST* jvst = (JackVST*) arg;
	jvst_load( jvst, kibel );

	sem_post( &chuj );

	fst_event_loop();
	return 0;
}
#endif

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	int		argc = -1;
	char**		argv = NULL;
	short		i;
	int32_t		opt_numIns = -1;
	int32_t		opt_numOuts = -1;
	bool		opt_generate_dbinfo = false;
	bool		opt_list_plugins = false;
	bool		sigusr1_save_state = FALSE;
	bool		want_midi_physical = false;
	const char*	connect_to = NULL;
	const char*	custom_path = NULL;

	printf("FSTHost Version: %s (%s)\n", VERSION, ARCH "bit");

	JackVST*	jvst = jvst_new();
	jvst_first = jvst;

	jvst->dbinfo_file = fst_info_default_path(APPNAME);

	// Handle FSTHOST_GUI environment
	char* menv = getenv("FSTHOST_GUI");
	if ( menv ) jvst->with_editor = strtol(menv, NULL, 10);

        // Parse command line options
	cmdline2arg(&argc, &argv, cmdline);
	while ( (i = getopt (argc, argv, "bBd:egs:S:c:k:i:j:lLnNm:pPo:t:u:U:V")) != -1) {
		switch (i) {
			case 'b': jvst->bypassed = TRUE; break;
			case 'd': free(jvst->dbinfo_file); jvst->dbinfo_file = optarg; break;
			case 'e': jvst->with_editor = WITH_EDITOR_HIDE; break;
			case 'g': opt_generate_dbinfo = true; break;
			case 'L': opt_list_plugins = true; break;
			case 'B': jvst->bbt_sync = true; break;
			case 's': jvst->default_state_file = optarg; break;
			case 'S': jvst->ctrl_port_number = strtol(optarg,NULL,10); break;
			case 'c': jvst->client_name = optarg; break;
			case 'k': midi_filter_one_channel_set(&jvst->channel, strtol(optarg, NULL, 10)); break;
			case 'i': opt_numIns = strtol(optarg, NULL, 10); break;
			case 'j': connect_to = optarg; break;
			case 'l': sigusr1_save_state = TRUE; break;
			case 'p': want_midi_physical = TRUE; break;
			case 'P': jvst->midi_pc = MIDI_PC_SELF; break; /* used but not enabled */
			case 'o': opt_numOuts = strtol(optarg, NULL, 10); break;
			case 'n': jvst->with_editor = WITH_EDITOR_NO; break;
			case 'N': jvst->sysex_want_notify = true; break;
			case 'm': jvst->want_state_cc = strtol(optarg, NULL, 10); break;
			case 't': jvst->tempo = strtod(optarg, NULL); break;
			case 'u': jvst->uuid = optarg; break;
			case 'U': jvst_sysex_set_uuid( jvst, strtol(optarg, NULL, 10) ); break;
			case 'V': jvst->volume = -1; break;
			default: usage (argv[0]); return 1;
		}
	}

	/* If use want to list plugins then abandon other tasks */
	if (opt_list_plugins) return fst_info_list ( jvst->dbinfo_file );

	/* We have more arguments than getops options */
	if (optind < argc) custom_path = argv[optind];

	/* If NULL then Generate using VST_PATH */
	if (opt_generate_dbinfo) {
		int ret = fst_info_update ( jvst->dbinfo_file, custom_path );
		if (ret > 0) usage ( argv[0] );
		return ret;
	}

#ifdef SEP_THREAD
	kibel = custom_path;
	sem_init( &chuj, 0, 0 );
	CreateThread( NULL, 0, sep_thread, jvst, 0, 0 );
	sem_wait ( &chuj );
#else
	/* User provided plugin that want to load (as name or full path) */
	if ( custom_path ) jvst_load( jvst, custom_path );
#endif

        /* load state if requested - state file may contain plugin path */
	if ( jvst->default_state_file ) {
		bool loaded = jvst_load_state (jvst, jvst->default_state_file);
		if ( ! loaded && ! sigusr1_save_state ) return 1;
	}

	/* Well .. Are we loaded plugin ? */
	if (! jvst->fst) {
		usage ( argv[0] );
		return 1;
	}

	// Our shourcuts
	FST* fst = jvst->fst;
	AEffect* plugin = fst->plugin;

	// Set client name (if user did not provide own)
	if (!jvst->client_name) jvst->client_name = jvst->fst->handle->name;

	// Set Thread policy - usefull only with WineRT/LPA patch
	HANDLE* h_thread = GetCurrentThread();
	//SetPriorityClass ( h_thread, REALTIME_PRIORITY_CLASS);
	SetPriorityClass ( h_thread, ABOVE_NORMAL_PRIORITY_CLASS);
	//SetThreadPriority ( h_thread, THREAD_PRIORITY_TIME_CRITICAL);
	SetThreadPriority ( h_thread, THREAD_PRIORITY_ABOVE_NORMAL);
        printf("Main Thread W32ID: %d | LWP: %d | W32 Class: %d | W32 Priority: %d\n",
		GetCurrentThreadId (), (int) syscall (SYS_gettid), GetPriorityClass (h_thread), GetThreadPriority(h_thread));

	// Count audio ports
	jvst->numIns = (opt_numIns >= 0 && opt_numIns < plugin->numInputs) ? opt_numIns : plugin->numInputs;
	jvst->numOuts = (opt_numOuts >= 0 && opt_numOuts < plugin->numOutputs) ? opt_numOuts : plugin->numOutputs;
	printf("Port Layout (FSTHost/plugin) IN: %d/%d OUT: %d/%d\n", 
		jvst->numIns, plugin->numInputs, jvst->numOuts, plugin->numOutputs);

	// Init Jack
	if ( ! jvst_fst_init ( jvst ) ) return 1;

	// Handling SIGINT for clean quit
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);

	// Handling SIGUSR1 for save state - mostly for ladish support
	if (sigusr1_save_state && jvst->default_state_file)
		sigaction(SIGUSR1, &sa, NULL);

#ifdef HAVE_LASH
	jvst_lash_init(jvst, &argc, &argv);
#endif

	// Activate plugin
	if (! jvst->bypassed) fst_call ( jvst->fst, RESUME );

	puts("Jack Activate");
	jack_activate(jvst->client);

	// Init Glib main event loop
	glib_main_loop = g_main_loop_new(NULL, FALSE);
	g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 750, (GSourceFunc) jvst_idle, jvst, NULL);

	// Auto connect on start
	if (connect_to) jvst_connect_audio(jvst, connect_to);
	if (want_midi_physical) jvst_connect_midi_to_physical(jvst);

	// Generate random SysEx ID
	jvst_generate_random_id(jvst);

#ifndef SEP_THREAD
	// Add FST event callback to Gblib main loop
	g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 100, (GSourceFunc) fst_event_callback, NULL, NULL);
#endif

	/* Socket stuff */
	if ( jvst->ctrl_port_number ) {
		if ( ! jvst_proto_init(jvst) )
			goto sock_err;
		
	}

	// Create GTK or GlibMain thread
	if (jvst->with_editor != WITH_EDITOR_NO) {
#ifdef NO_GTK
		puts("run editor");
		fst_run_editor (jvst->fst, false);
	}
#else
		puts( "Start GUI" );
		gtk_gui_init(&argc, &argv);
		gtk_gui_start(jvst);
	} else {
#endif
		puts("GUI Disabled - start GlibMainLoop");
		g_main_loop_run ( glib_main_loop );
#ifndef NO_GTK
	}
#endif

	/* Close CTRL socket */
	jvst_proto_close ( jvst );

sock_err:

	puts("Jack Deactivate");
	jack_deactivate(jvst->client);
	jack_client_close ( jvst->client );

	jvst_cleanup(jvst);

	fst_close(jvst->fst);

	jvst_destroy(jvst);

	puts("Game Over");

	return 0;
}
