#ifndef __jack_vst_h__
#define __jack_vst_h__

#include <sys/types.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/session.h>
#include <jack/midiport.h>
#include <math.h>

#include "sysex.h"
#include "fst.h"
#include "midifilter.h"

#define MIDI_PC_SELF -1
#define MIDI_PC_PLUG -2

typedef struct _JackVST JackVST;

enum WantState {
   WANT_STATE_NO     = 0,
   WANT_STATE_RESUME = 1,
   WANT_STATE_BYPASS = 2
};

enum WithEditor {
   WITH_EDITOR_NO,
   WITH_EDITOR_HIDE,
   WITH_EDITOR_SHOW
};

enum SysExWant {
   SYSEX_WANT_NO          = 0,
   SYSEX_WANT_IDENT_REPLY = 1,
   SYSEX_WANT_DUMP        = 2
};

/* Structures & Prototypes for midi output and associated queue */
struct MidiMessage {
   jack_nframes_t   time;
   uint8_t          len; /* Length of MIDI message, in bytes. */
   jack_midi_data_t data[3];
};

struct _JackVST {
    jack_client_t*  client;
    FST*            fst;
    char*           client_name;
    char*           default_state_file;
    short           numIns;
    short           numOuts;
    float**         ins;
    float**         outs;
    jack_port_t*    midi_inport;
    jack_port_t*    midi_outport;
    jack_port_t*    ctrl_inport;
    jack_port_t*    ctrl_outport;
    jack_port_t**   inports;
    jack_port_t**   outports;
    int             channel; /* 0 Omni, 17 None */
    bool            bypassed;
    enum WantState  want_state;
    short           want_state_cc;
    enum WithEditor with_editor;
    bool            want_resize;
    double          tempo;
    float           volume; /* where 0.0 mean silence */
    bool            graph_order_change;

    int32_t         midi_map[128];
    bool            midi_learn;
    short           midi_learn_CC;
    int             midi_learn_PARAM;
    short           midi_pc;
    MIDIFILTER*     filters;

    /* SysEx send support */
    pthread_mutex_t   sysex_lock;
    pthread_cond_t    sysex_sent;
    enum SysExWant    sysex_want;
    bool              sysex_want_notify;
    SysExDumpV1       sysex_dump;
    SysExIdentReply   sysex_ident_reply;

    /* SysEx receive support */
    jack_ringbuffer_t* sysex_ringbuffer;

    /* For VST/i support */
    bool                 want_midi_in;
    struct VstMidiEvent* event_array;
    struct VstEvents*    events;

    /* Jack Session support */
    char* uuid;
    jack_session_event_t* session_event;

    /* For VST midi effects & synth source (like audio to midi VSTs) support */
    bool               want_midi_out;
    jack_ringbuffer_t* ringbuffer;
};

JackVST* jvst_new();
bool jvst_load(JackVST* jvst, const char* path);
void jvst_log(const char *msg);
void jvst_destroy(JackVST* jvst);
void jvst_send_sysex(JackVST* jvst, enum SysExWant);
void jvst_bypass(JackVST* jvst, bool bypass);
bool jvst_load_state(JackVST* jvst, const char * filename);
bool jvst_save_state(JackVST* jvst, const char * filename);
void jvst_set_volume(JackVST* jvst, short volume);
void jvst_sysex_set_uuid(JackVST* jvst, uint8_t uuid);
unsigned short jvst_get_volume(JackVST* jvst);

#endif /* __jack_vst_h__ */
