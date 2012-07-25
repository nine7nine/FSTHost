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
#include <libconfig.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include "../sysex.h"

static jack_port_t* inport;
static jack_port_t* outport;
jack_midi_data_t* sysex;
bool send = false;
sem_t sem_change;
bool quit = false;
short CtrlCh = 15; /* Our MIDI control channel */

struct FSTPlug {
	uint8_t id; /* 0 - 127 */
	char name[24];
};

struct FSTState {
	enum SysExState state;
	uint8_t program; /* 0 - 127 */
	uint8_t channel; /* 0 - 17 */
	uint8_t volume; /* 0 - 127 */
	char program_name[24];
};

struct Song {
	char name[24];
	struct FSTState* fst_state[127];
	struct Song* next;
};

struct FSTPlug* fst[127] = {NULL};
struct Song* song_first = NULL;
struct Song* song_current = NULL;

struct FSTState* state_new() {
	struct FSTState* fs = calloc(1,sizeof(struct FSTState));
	return fs;
}

struct FSTPlug* fst_new(uint8_t uuid) {
	struct Song* s;
	struct FSTPlug* f = malloc(sizeof(struct FSTPlug));
	sprintf(f->name, "Device%d", uuid);
	f->id = uuid;

	// Add to states to songs
	for(s = song_first; s; s = s->next) {
		s->fst_state[uuid] = state_new();
	}

	return f;
}

void song_new() {
	short i;
	struct Song** snptr;
	struct Song* s = calloc(1, sizeof(struct Song));

	// Add state for already known plugins
	for(i=0; i < 127; i++) {
		if (fst[i] == NULL) continue;

		s->fst_state[i] = state_new();
	}

	// Bind to song list
	snptr = &song_first;
	while(*snptr) { snptr = &(*snptr)->next; }
	*snptr = s;
}

void sigint_handler(int signum, siginfo_t *siginfo, void *context) {
        printf("Caught signal (SIGINT)\n");
	send = true;
	quit = true;
	sem_post(&sem_change);
}

int process (jack_nframes_t frames, void* arg) {
	void* inbuf;
	void* outbuf;
	jack_nframes_t count;
	jack_nframes_t i;
//	char description[256];
	unsigned short j;
	jack_midi_event_t event;
	struct FSTState* fs;

	inbuf = jack_port_get_buffer (inport, frames);
	assert (inbuf);

	outbuf = jack_port_get_buffer(outport, frames);
	assert (outbuf);
	jack_midi_clear_buffer(outbuf);
	
	count = jack_midi_get_event_count (inbuf);
	for (i = 0; i < count; ++i) {
		if (jack_midi_event_get (&event, inbuf, i) != 0)
			break;

		// My Midi control channel handling
		if ( (jackevent.buffer[0] & 0xF0) != 0xC0 &&
		     (jackevent.buffer[0] & 0x0F) != CtrlCh
		) {
			/* TODO: send SysExDumpV1 to all plugin */
		}

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
			if (fst[r->model[1]] == NULL) {
				fst[r->model[1]] = fst_new(r->model[1]);
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

			printf("Got SysEx Dump %X : %s : %s\n", d->uuid, d->plugin_name, d->program_name);
			if (fst[d->uuid] == NULL)
				fst[d->uuid] = fst_new(d->uuid);

			fs = song_current->fst_state[d->uuid];
			fs->state = d->state;
			fs->program = d->program;
			fs->channel = d->channel;
			fs->volume = d->volume;
			strcpy(fs->program_name, (char*) d->program_name);
			strcpy(fst[d->uuid]->name, (char*) d->plugin_name);
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
	struct FSTPlug* fp;
	struct FSTState* fs;
	struct Song* song = song_current;

	const char* format = "%02d %-24s %02d %-24s\n";

	// Header
	printf("%-2s %-24s %-2s %-24s\n", "ID", "DEVICE", "CH", "PROGRAM");

	for(i=0; i < 127; i++) {
		if (fst[i] == NULL) continue;
		fp = fst[i];
		fs = song->fst_state[i];

		printf(format, i, fp->name, fs->channel, fs->program_name);
	}
}

bool dump_state(char const* config_file) {
	short i, j;
	char name[10];
	struct Song* s;
	struct FSTState* fs;
	config_t cfg;
	config_setting_t* group;
	config_setting_t* list;

	config_init(&cfg);

	// Save plugs
	group = config_setting_add(cfg.root, "global", CONFIG_TYPE_GROUP);
	for (i = j = 0; i < 127; i++) {
		if (fst[i] == NULL) continue;

		sprintf(name, "plugin%d", j++);
		list = config_setting_add(group, name, CONFIG_TYPE_LIST);
		config_setting_set_int_elem(list, -1, fst[i]->id);
		config_setting_set_string_elem(list, -1, fst[i]->name);
	}

	for(s = song_first; s; s = s->next) {
		group = config_setting_add(cfg.root, "song0", CONFIG_TYPE_GROUP);
		for (i = j = 0; i < 127; i++) {
			if (fst[i] == NULL) continue;

			fs = s->fst_state[i];

			printf("save plug %d\n", i);
			sprintf(name, "plugin%d", j++);

			list = config_setting_add(group, name, CONFIG_TYPE_LIST);
			config_setting_set_int_elem(list, -1, fs->state);
			config_setting_set_int_elem(list, -1, fs->program);
			config_setting_set_int_elem(list, -1, fs->channel);
			config_setting_set_int_elem(list, -1, fs->volume);
			config_setting_set_string_elem(list, -1, fs->program_name);
		}
	}

	config_write_file(&cfg, config_file);

	config_destroy(&cfg);

	return true;
}

bool load_state(const char* config_file) {
	struct FSTPlug* f;
	struct FSTState* fs;
	struct Song* song;
	config_t cfg;
	config_setting_t* global;
	config_setting_t* list;
	char name[24];
	const char* sparam;
	const char* plugName;
	short id;
	short i, s;

	config_init(&cfg);
	if (!config_read_file(&cfg, config_file)) {
		fprintf(stderr, "%s:%d - %s\n",
			config_file,
			config_error_line(&cfg),
			config_error_text(&cfg)
		);
		config_destroy(&cfg);
		return false;
	}

	// Global section
	global = config_lookup(&cfg, "global");
	for(i=0; i < config_setting_length(global); i++) {
		list = config_setting_get_elem(global, i);
		plugName = config_setting_name(list);

		id = config_setting_get_int_elem(list, 0);
		if (fst[id] == NULL)
			fst[id] = fst_new(id);

		sparam = config_setting_get_string_elem(list, 1);
		f = fst[id];
		strcpy(f->name, sparam);

		// Songs iteration
		s = 0;
		song = song_first;

again:
		sprintf(name, "song%d", s);
		if (config_lookup(&cfg, name) == NULL)
			continue;

		sprintf(name, "song%d.%s", s++, plugName);
		list = config_lookup(&cfg, name);
		if (list != NULL) {
			fs = song->fst_state[id];
			fs->state = config_setting_get_int_elem(list, 0);
			fs->program = config_setting_get_int_elem(list, 1);
			fs->channel = config_setting_get_int_elem(list, 2);
			fs->volume = config_setting_get_int_elem(list, 3);
			sparam = config_setting_get_string_elem(list, 4);
			if (sparam)
				strcpy(fs->program_name, sparam);
		}
		if (! song->next) song_new();
		song = song->next;

		goto again;
	}

	config_destroy(&cfg);

	return true;
}

int main (int argc, char* argv[]) {
	jack_client_t* client;
	char const* client_name = "FST Control";
	char const* config_file = NULL;

	if (argv[1]) config_file = argv[1];

	song_new();
	song_current = song_first;

	// Try read file
	if (config_file != NULL)
		load_state(config_file);

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

	while (!quit) {
		show();
		sem_wait(&sem_change);
	}

//	if (config_file != NULL)
//		dump_state(config_file);

	return 0;
}

