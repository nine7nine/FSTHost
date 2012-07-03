/*
   FSTHost Control by XJ / Pawel Piatek /

   This is part of FSTHost sources

   Based on jack-midi-dump by Carl Hetherington
*/

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <semaphore.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include "../sysex.h"

static jack_port_t* inport;
static jack_port_t* outport;
jack_midi_data_t* sysex;
bool send = false;
sem_t sem_change;

enum FSTState {
	FST_ACTIVE = 1,
	FST_BYPASS = 2
};

struct FSTPlug {
	uint8_t id; /* 0 - 127 */
	enum FSTState state;
	uint8_t program; /* 0 - 127 */
	uint8_t channel; /* 0 - 17 */
	uint8_t volume; /* 0 - 127 */
	char program_name[24];
	char plugin_name[24];
};

struct FSTPlug* fst_new(uint8_t id) {
	struct FSTPlug* f = calloc(1, sizeof(struct FSTPlug));

	f->id = id;

	return f;
};

struct FSTPlug* fst[127] = {NULL};

void sigint_handler(int signum, siginfo_t *siginfo, void *context) {
        printf("Caught signal (SIGINT)\n");

	send = true;
}

int process (jack_nframes_t frames, void* arg) {
	void* inbuf;
	void* outbuf;
	jack_nframes_t count;
	jack_nframes_t i;
//	char description[256];
	unsigned short j;
	jack_midi_event_t event;

	inbuf = jack_port_get_buffer (inport, frames);
	assert (inbuf);

	outbuf = jack_port_get_buffer(outport, frames);
	assert (outbuf);
	jack_midi_clear_buffer(outbuf);
	
	count = jack_midi_get_event_count (inbuf);
	for (i = 0; i < count; ++i) {
		if (jack_midi_event_get (&event, inbuf, i) != 0)
			break;

		// Ident Reply
		if ( event.size < 5 || event.buffer[0] != SYSEX_BEGIN )
			goto further;

		switch (event.buffer[1]) {
		case SYSEX_NON_REALTIME:
			// event.buffer[2] is target_id - in our case always 7F
			if ( event.buffer[3] != SYSEX_GENERAL_INFORMATION ||
				event.buffer[4] != SYSEX_IDENTITY_REPLY
			) goto further;

			SysExIdentReply* r = (SysExIdentReply*) event.buffer;
//			printf("Got SysEx Identity Reply from ID %X : %X\n", r->id, r->model[1]);
			if (fst[r->id] == NULL) {
				fst[r->id] = fst_new(r->id);
				sem_post(&sem_change);
			}

			// don't forward this message
			continue;
		case SYSEX_MYID:
			if (event.size < sizeof(SysExDumpV1))
				continue;

			SysExDumpV1* d = (SysExDumpV1*) event.buffer;
			if (d->type != SYSEX_TYPE_DUMP)
				goto further;

//			printf("Got SysEx Dump %X : %s : %s\n", d->id, d->plugin_name, d->program_name);
			if (fst[d->id] == NULL)
				fst[d->id] = fst_new(d->id);

			fst[d->id]->state = d->state;
			fst[d->id]->program = d->program;
			fst[d->id]->channel = d->channel;
			fst[d->id]->volume = d->volume;
			strcpy(fst[d->id]->program_name, (char*) d->program_name);
			strcpy(fst[d->id]->plugin_name, (char*) d->plugin_name);
			sem_post(&sem_change);

			// don't forward this message
			continue;
		}

further:	printf ("%d:", event.time);
		for (j = 0; j < event.size; ++j)
			printf (" %X", event.buffer[j]);

		printf ("\n");
	
		jack_midi_event_write(outbuf, event.time, event.buffer, event.size);
	}

	if (send) {
		send = false;

		jack_midi_event_write(outbuf, 0, (jack_midi_data_t*) sysex, sizeof(SysExIdentRqst));
		printf("Request Identity was send\n");
	}

	return 0;
}

void show() {
	short i;
	struct FSTPlug* f;

	const char* format = "%-24s %02d %-24s\n";

	// Header
	printf("%-24s %-2s %-24s\n", "DEVICE", "CH", "PROGRAM");

	for(i=0; i < 127; i++) {
		if (fst[i] == NULL) continue;
		f = fst[i];

		printf(format, f->plugin_name, f->channel, f->program_name);
	}
}

int main (int argc, char* argv[]) {
	jack_client_t* client;
	char const * client_name = "FST Control";

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = &sigint_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sa, NULL);

	sem_init(&sem_change, 0, 0);

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) {
		fprintf (stderr, "Could not create JACK client.\n");
		exit (EXIT_FAILURE);
	}

	jack_set_process_callback (client, process, 0);

	inport = jack_port_register (client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	outport = jack_port_register (client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if ( jack_activate (client) != 0 ) {
		fprintf (stderr, "Could not activate client.\n");
		exit (EXIT_FAILURE);
	}


	sysex = (jack_midi_data_t*) sysex_ident_request_new();

	while (true) {
		sem_wait(&sem_change);

		show();
	}

	return 0;
}

