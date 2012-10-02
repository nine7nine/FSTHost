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
#include <libconfig.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include "../../trunk/sysex.h"
#include "fhctrl.h"

extern void nfhc(struct Song *song_first, struct FSTPlug **fst);

char const* client_name = "FHControl";
static jack_port_t* inport;
static jack_port_t* outport;
static jack_nframes_t jack_buffer_size;
static jack_nframes_t jack_sample_rate;
short CtrlCh = 15; /* Our default MIDI control channel */
short SongCount = 0;
bool ident_request = false;

struct FSTPlug* fst[128] = {NULL};
struct Song* song_first = NULL;
SysExIdentRqst* sysex_ident_request;
SysExDumpRequestV1* sysex_dump_request;
SysExDumpV1* sysex_dump;

struct FSTState* state_new() {
	struct FSTState* fs = calloc(1,sizeof(struct FSTState));
	fs->state = FST_NA; // Initial state is Inactive
	return fs;
}

void fst_new(uint8_t uuid) {
	struct Song* s;
	struct FSTPlug* f = malloc(sizeof(struct FSTPlug));
	sprintf(f->name, "Device%d", uuid);
	f->id = uuid;
	f->state = state_new();
	f->change = true;

	// Add to states to songs
	for(s = song_first; s; s = s->next) {
		s->fst_state[uuid] = state_new();
	}

	// Fill our global array
	fst[uuid] = f;
}


struct FSTPlug* fst_get(uint8_t uuid) {
	if (fst[uuid] == NULL)
		fst_new(uuid);

	return fst[uuid];	
}

struct Song* song_new() {
	short i;
	struct Song** snptr;
	struct Song* s = calloc(1, sizeof(struct Song));

	//printf("Creating new song\n");
	// Add state for already known plugins
	for(i=0; i < 128; i++) {
		if (fst[i] == NULL) continue;

		s->fst_state[i] = state_new();
	}

	// Bind to song list
	if (song_first) {
		snptr = &song_first;
		while(*snptr) { snptr = &(*snptr)->next; }
		*snptr = s;
	} else {
		song_first = s;
	}

	sprintf(s->name, "Song %d", SongCount);
	SongCount++;

	return s;
}

struct Song* song_get(int SongNumber) {
	if (SongNumber >= SongCount)
		return NULL;

	struct Song* song;
	int s = 0;
	for(song=song_first; s < SongNumber; s++) {
		song = song->next;
		if (song == NULL) return NULL;
	}

	return song;
}

void send_ident_request() {
	short i;

	// Reset states to non-active
	for(i=0; i < 128; i++) {
		if (fst[i] == NULL) continue;
		fst[i]->state->state = FST_NA;
	}

	ident_request = true;
}

int process (jack_nframes_t frames, void* arg) {
	void* inbuf;
	void* outbuf;
	jack_nframes_t count;
	jack_nframes_t i;
	unsigned short s;
	jack_midi_event_t event;
	struct FSTPlug* fp;
	struct Song* song = NULL;

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
		if ( (event.buffer[0] & 0xF0) == 0xC0 &&
		     (event.buffer[0] & 0x0F) == CtrlCh
		) {
			song = song_get(event.buffer[1]);
			continue;
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
			nLOG("Got SysEx Identity Reply from ID %X : %X", r->id, r->model[1]);
			fp = fst_get(r->model[1]);
			fp->change = true;

			// If this is FSTPlug then dump it state
			if (r->id == SYSEX_MYID)
				fp->dump_request = true;
			// don't forward this message
			continue;
		case SYSEX_MYID:
			if (event.size < sizeof(SysExDumpV1))
				continue;

			SysExDumpV1* d = (SysExDumpV1*) event.buffer;
			if (d->type != SYSEX_TYPE_DUMP)
				goto further;

			nLOG("Got SysEx Dump %X : %s : %s", d->uuid, d->plugin_name, d->program_name);
			fp = fst_get(d->uuid);
			fp->state->state = d->state;
			fp->state->program = d->program;
			fp->state->channel = d->channel;
			fp->state->volume = d->volume;
			strcpy(fp->state->program_name, (char*) d->program_name);
			strcpy(fp->name, (char*) d->plugin_name);
			fp->change = true;

			// don't forward this message
			continue;
		}

further:
		// Forward messages
		jack_midi_event_write(outbuf, event.time, event.buffer, event.size);
	}

	// Send Identity Request
	if (ident_request) {
		ident_request = false;
		jack_midi_event_write(outbuf, jack_buffer_size - 1, (jack_midi_data_t*) sysex_ident_request, sizeof(SysExIdentRqst));
	}

	// Send Dump Request
	for (s=0; s < 128; s++) {
		fp = fst[s];
		if (!fp) continue;
		if (!fp->dump_request) continue;
		fp->dump_request = false;

		sysex_dump_request->uuid = fp->id;
		jack_midi_event_write(outbuf, jack_buffer_size - 1, (jack_midi_data_t*) sysex_dump_request, sizeof(SysExDumpRequestV1));
	}

	if (song != NULL) {
		nLOG("Send Song \"%s\" SysEx", song->name);
		// Dump states via SysEx
		for (s=0; s < 128; s++) {
			fp = fst[s];
			if (!fp) continue;

			*fp->state = *song->fst_state[s];
			sysex_dump->program = fp->state->program;
			sysex_dump->channel = fp->state->channel;
			sysex_dump->volume = fp->state->volume;
			sysex_dump->state = fp->state->state;
			strcpy((char*) sysex_dump->program_name, fp->state->program_name);
			strcpy((char*) sysex_dump->plugin_name, fp->name);
		
			jack_midi_event_write(outbuf, jack_buffer_size - 1, (jack_midi_data_t*) sysex_dump, sizeof(SysExDumpV1));
		}
	}

	return 0;
}

bool dump_state(char const* config_file) {
	short i, j, sn = 0;
	char name[10];
	struct Song* s;
	struct FSTState* fs;
	config_t cfg;
	config_setting_t* group;
	config_setting_t* list;

	nLOG("Dump state to %s", config_file);

	config_init(&cfg);

	// Save plugs
	group = config_setting_add(cfg.root, "global", CONFIG_TYPE_GROUP);
	config_setting_set_int(group, CtrlCh); // Control MIDI channel
	for (i = j = 0; i < 128; i++) {
		if (fst[i] == NULL) continue;

		sprintf(name, "plugin%d", j++);
		list = config_setting_add(group, name, CONFIG_TYPE_LIST);
		config_setting_set_int_elem(list, -1, fst[i]->id);
		config_setting_set_string_elem(list, -1, fst[i]->name);
	}

	// Save songs
	for(s = song_first; s; s = s->next) {
		sprintf(name, "song%d", sn++);
		group = config_setting_add(cfg.root, name, CONFIG_TYPE_GROUP);
		for (i = j = 0; i < 128; i++) {
			if (fst[i] == NULL) continue;

			fs = s->fst_state[i];

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
	unsigned short id, i, s;

	config_init(&cfg);
	if (!config_read_file(&cfg, config_file)) {
		nLOG("%s:%d - %s",
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
		f = fst_get(id);

		sparam = config_setting_get_string_elem(list, 1);
		strcpy(f->name, sparam);

		// Songs iteration
		s = 0;
		song = song_first;

again:
		sprintf(name, "song%d", s);
		if (config_lookup(&cfg, name) == NULL)
			continue;

		if (! song) song = song_new();

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
		song = song->next;

		goto again;
	}

	config_destroy(&cfg);

	return true;
}

int main (int argc, char* argv[]) {
	jack_client_t* client;
	char const* config_file = NULL;

	if (argv[1]) config_file = argv[1];

	// Try read file
	if (config_file != NULL)
		load_state(config_file);

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) {
		fprintf (stderr, "Could not create JACK client.\n");
		exit (EXIT_FAILURE);
	}

	jack_set_process_callback (client, process, 0);

	inport = jack_port_register (client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	outport = jack_port_register (client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	jack_buffer_size = jack_get_buffer_size(client);
	jack_sample_rate = jack_get_sample_rate(client);


	if ( jack_activate (client) != 0 ) {
		fprintf (stderr, "Could not activate client.\n");
		exit (EXIT_FAILURE);
	}

	// Init sysex comunicates
	sysex_ident_request = sysex_ident_request_new();
	sysex_dump_request = sysex_dump_request_v1_new();
	sysex_dump = sysex_dump_v1_new();

	// ncurses loop
	nfhc(song_first, fst);

	if (config_file != NULL)
		dump_state(config_file);

	return 0;
}
