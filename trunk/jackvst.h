#ifndef __jack_vst_h__
#define __jack_vst_h__

#include <sys/types.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/session.h>
#include <math.h>

#include "fst.h"

typedef struct _JackVST JackVST;

enum WantMode {
   WANT_MODE_NO		= 0,
   WANT_MODE_RESUME	= 1,
   WANT_MODE_BYPASS	= 2
};

enum WithEditor {
   WITH_EDITOR_NO,
   WITH_EDITOR_HIDE,
   WITH_EDITOR_SHOW
};

struct _JackVST {
    jack_client_t *client;
    FSTHandle*     handle;
    FST*           fst;
    char*          client_name;
    short          numIns;
    short          numOuts;
    float**        ins;
    float**        outs;
    jack_port_t*   midi_inport;
    jack_port_t*   midi_outport;
    jack_port_t**  inports;
    jack_port_t**  outports;
    int            channel;
    bool           bypassed;
    enum WantMode  want_mode;
    short          want_mode_cc;
    enum WithEditor with_editor;
    double         tempo;
    float          volume; /* where 0.0 mean silence */

    int            midi_map[128];
    bool           midi_learn;
    short          midi_learn_CC;
    int            midi_learn_PARAM;

    /* SysEx support */
    bool          sysex_send;
    bool          sysex_ident;
    unsigned char sysex_uuid;

    /* For VST/i support */
    bool want_midi_in;
    bool want_midi_out;
    struct VstMidiEvent* event_array;
    struct VstEvents* events;

    char*                 uuid;
    jack_session_event_t* session_event;

    /* For VST midi effects & synth source (like audio to midi VSTs) support */
    jack_ringbuffer_t* ringbuffer;
};

static inline float 
jvst_set_volume(JackVST* jvst, short volume) { jvst->volume = powf(volume / 63.0f, 2); }

static short
jvst_get_volume(JackVST* jvst)
{
	short ret = roundf(sqrtf(jvst->volume) * 63.0f);

	if (ret < 0) ret = 0;
	if (ret > 127) ret = 127;
	return ret;
}

#endif /* __jack_vst_h__ */
