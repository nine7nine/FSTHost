#ifndef __jack_vst_h__
#define __jack_vst_h__

#include <sys/types.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/session.h>
#include <jack/midiport.h>

#include "eventqueue.h"
#include "sysex.h"
#include "fst/fst.h"
#include "midifilter/midifilter.h"

#define CTRLAPP "FHControl"

enum MidiPC {
	MIDI_PC_SELF,
	MIDI_PC_PLUG
};

enum WantState {
	WANT_STATE_RESUME,
	WANT_STATE_BYPASS
};

enum WithEditor {
	WITH_EDITOR_NO   = 0,
	WITH_EDITOR_HIDE = 1,
	WITH_EDITOR_SHOW = 2
};

enum PROTO_CMD {
	CMD_UNKNOWN,
	CMD_EDITOR_OPEN,
	CMD_EDITOR_CLOSE,
	CMD_LIST_PROGRAMS,
	CMD_GET_PROGRAM,
	CMD_SET_PROGRAM,
	CMD_SUSPEND,
	CMD_RESUME,
	CMD_CPU,
	CMD_QUIT,
	CMD_KILL
};

struct PROTO_MAP {
	enum PROTO_CMD key;
	const char* name;
};

/* Structures & Prototypes for midi output and associated queue */
struct MidiMessage {
	jack_nframes_t		time;
	uint8_t			len; /* Length of MIDI message, in bytes. */
	jack_midi_data_t	data[3];
};

typedef struct {
	bool bypassed;
	uint8_t channel;
	unsigned short volume;
	int32_t program;
} DetectChangesLast;

typedef enum {
	CHANGE_QUIT	= 1 << 0,
	CHANGE_BYPASS	= 1 << 1,
	CHANGE_CHANNEL	= 1 << 2,
	CHANGE_VOLUME	= 1 << 3,
	CHANGE_PROGRAM	= 1 << 4
} Changes;

typedef struct {
	int32_t		map[128];
	bool		wait;
	int8_t		cc;
	int32_t		param;
} MidiLearn;

typedef struct _JFST {
	const char*	appname;
	jack_client_t*	client;
	FST*		fst;
	EventQueue	event_queue;
	char*		client_name;
	char*		default_state_file;
	char*		dbinfo_file;
	int32_t		numIns;
	int32_t		numOuts;
	jack_nframes_t	buffer_size;
	jack_nframes_t	sample_rate;
	jack_port_t*	midi_inport;
	jack_port_t*	midi_outport;
	jack_port_t*	ctrl_outport;
	jack_port_t**	inports;
	jack_port_t**	outports;
	bool		bypassed;
	bool		want_port_aliases;
	bool		want_auto_midi_physical;
	short		want_state_cc;
	enum WithEditor	with_editor;
	float		volume;		/* where 0.0 mean silence */
	uint8_t		out_level;	/* for VU-meter */
	uint16_t	ctrl_port_number;
	char*		uuid;		/* Jack Session support */

	MidiLearn	midi_learn;
	DetectChangesLast last;

	enum MidiPC	midi_pc;
	MIDIFILTER*	filters;
	MIDIFILTER*	transposition;
	OCH_FILTERS	channel;

	/* SysEx send support */
	pthread_mutex_t	sysex_lock;
	pthread_cond_t	sysex_sent;
	SysExType	sysex_want;
	bool		sysex_want_notify;
	SysExDumpV1	sysex_dump;
	SysExIdentReply	sysex_ident_reply;

	/* SysEx receive support */
	jack_ringbuffer_t* sysex_ringbuffer;

	/* For VSTi support - midi effects & synth source (like audio to midi VSTs) support */
	jack_ringbuffer_t* ringbuffer;

	// Method for GUI resize
	void (*gui_resize) ( struct _JFST* );
} JFST;

static inline void jfst_set_gui_resize_cb ( JFST* jfst, void (*f) ) {
	jfst->gui_resize = f;
}

/* jfst.c */
JFST* jfst_new( const char* appname );
bool jfst_init( JFST* jfst, int32_t max_in, int32_t max_out );
bool jfst_load(JFST* jfst, const char* plug_spec, bool want_state_and_amc, bool state_can_fail);
bool jfst_load_state(JFST* jfst, const char * filename);
bool jfst_save_state(JFST* jfst, const char * filename);
bool jfst_session_callback( JFST* jfst, const char* appname );
void jfst_close ( JFST* jfst );
void jfst_bypass(JFST* jfst, bool bypass);
Changes jfst_idle(JFST* jfst);

/* jack.c */
bool jfst_jack_init( JFST* jfst, bool want_midi_out );
void jfst_connect_audio(JFST *jfst, const char *audio_to);
void jfst_connect_midi_to_physical(JFST* jfst);
void jfst_connect_to_ctrl_app(JFST* jfst);

void jfst_set_volume(JFST* jfst, short volume);
unsigned short jfst_get_volume(JFST* jfst);

/* sysex.c */
void jfst_sysex_set_uuid(JFST* jfst, uint8_t uuid);
void jfst_send_sysex(JFST* jfst, SysExType type);

/* process.c */
void jfst_process( JFST* jfst, jack_nframes_t nframes );

#endif /* __jack_vst_h__ */
