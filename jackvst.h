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

struct _JackVST {
    jack_client_t*  client;
    FSTHandle*      handle;
    FST*            fst;
    char*           client_name;
    char*           default_state_file;
    short           numIns;
    short           numOuts;
    float**         ins;
    float**         outs;
    jack_port_t*    midi_inport;
    jack_port_t*    midi_outport;
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

    int             midi_map[128];
    bool            midi_learn;
    short           midi_learn_CC;
    int             midi_learn_PARAM;

    /* SysEx send support */
    pthread_mutex_t   sysex_lock;
    pthread_cond_t    sysex_sent;
    enum SysExWant    sysex_want;
    bool              sysex_want_notify;
    SysExDumpV1       sysex_dump;
    SysExIdentReply   sysex_ident_reply;

    /* SysEx receive support */
    jack_midi_data_t* sysex_data;
    size_t sysex_size;

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

bool jvst_send_sysex(JackVST* jvst, enum SysExWant);
void jvst_bypass(JackVST* jvst, bool bypass);

static inline void
jvst_set_volume(JackVST* jvst, short volume)
{
	if (jvst->volume != -1) jvst->volume = powf(volume / 63.0f, 2);
}

static unsigned short
jvst_get_volume(JackVST* jvst)
{
	if (jvst->volume == -1) return 0;

	short ret = roundf(sqrtf(jvst->volume) * 63.0f);

	return (ret < 0) ? 0 : (ret > 127) ? 127 : ret;
}

#endif /* __jack_vst_h__ */
