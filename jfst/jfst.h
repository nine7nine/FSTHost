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
	CMD_KILL
};

struct PROTO_MAP {
	enum PROTO_CMD key;
	const char* name;
};

/* Structures & Prototypes for midi output and associated queue */
struct MidiMessage {
   jack_nframes_t   time;
   uint8_t          len; /* Length of MIDI message, in bytes. */
   jack_midi_data_t data[3];
};

typedef struct {
    int32_t         map[128];
    bool            wait;
    int8_t          cc;
    int32_t         param;
} MidiLearn;

typedef struct _JackVST {
    jack_client_t*  client;
    FST*            fst;
    EventQueue      event_queue;
    char*           client_name;
    char*           default_state_file;
    char*           dbinfo_file;
    int32_t         numIns;
    int32_t         numOuts;
    jack_nframes_t  buffer_size;
    jack_nframes_t  sample_rate;
    jack_port_t*    midi_inport;
    jack_port_t*    midi_outport;
    jack_port_t*    ctrl_outport;
    jack_port_t**   inports;
    jack_port_t**   outports;
    bool            bypassed;
    bool            want_port_aliases;
    short           want_state_cc;
    enum WithEditor with_editor;
    float           volume; /* where 0.0 mean silence */
    uint8_t         out_level; /* for VU-meter */
    uint16_t        ctrl_port_number;
    void (*gui_resize) ( struct _JackVST* );

    MidiLearn       midi_learn;

    enum MidiPC     midi_pc;
    MIDIFILTER*     filters;
    MIDIFILTER*     transposition;
    OCH_FILTERS     channel;

    /* SysEx send support */
    pthread_mutex_t   sysex_lock;
    pthread_cond_t    sysex_sent;
    SysExType         sysex_want;
    bool              sysex_want_notify;
    SysExDumpV1       sysex_dump;
    SysExIdentReply   sysex_ident_reply;

    /* SysEx receive support */
    jack_ringbuffer_t* sysex_ringbuffer;

    /* Jack Session support */
    char* uuid;

    /* For VSTi support - midi effects & synth source (like audio to midi VSTs) support */
    jack_ringbuffer_t* ringbuffer;
} JackVST;

static inline void jvst_set_gui_resize_cb ( JackVST* jvst, void (*f) ) {
	jvst->gui_resize = f;
}

/* jfst.c */
JackVST* jvst_new();
bool jvst_init( JackVST* jvst, int32_t max_in, int32_t max_out );
bool jvst_load(JackVST* jvst, const char* plug_spec, bool want_state_and_amc, bool state_can_fail);
bool jvst_load_state(JackVST* jvst, const char * filename);
bool jvst_save_state(JackVST* jvst, const char * filename);
bool jvst_session_callback( JackVST* jvst, const char* appname );
bool jvst_idle(JackVST* jvst, const char* appname);
void jvst_close ( JackVST* jvst );
void jvst_bypass(JackVST* jvst, bool bypass);

/* jack.c */
bool jvst_jack_init( JackVST* jvst, bool want_midi_out );
void jvst_connect_audio(JackVST *jvst, const char *audio_to);
void jvst_connect_midi_to_physical(JackVST* jvst);
void jvst_connect_to_ctrl_app(JackVST* jvst);

void jvst_set_volume(JackVST* jvst, short volume);
unsigned short jvst_get_volume(JackVST* jvst);

/* sysex.c */
void jvst_sysex_set_uuid(JackVST* jvst, uint8_t uuid);
void jvst_send_sysex(JackVST* jvst, SysExType type);

/* process.c */
void jvst_process( JackVST* jvst, jack_nframes_t nframes );

#endif /* __jack_vst_h__ */
