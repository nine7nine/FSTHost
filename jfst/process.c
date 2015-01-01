#include <math.h>
#include "jfst.h"

/* sysex.c */
extern void jfst_sysex_handler ( JFST* jfst );
extern void jfst_sysex_rt_send ( JFST* jfst, void *port_buffer );
extern void jfst_queue_sysex ( JFST* jfst, jack_midi_data_t* data, size_t size );

/* jack.c */
extern void jfst_apply_volume ( JFST* jfst, jack_nframes_t nframes, float** outs );

static inline void process_midi_output(JFST* jfst, jack_nframes_t nframes) {
	if (! fst_want_midi_out(jfst->fst) ) return;

	// Do not process anything if MIDI OUT port is not connected
	if (! jack_port_connected ( jfst->midi_outport ) ) return;

	/* This jack ringbuffer consume code was largely taken from jack-keyboard */
	/* written by Edward Tomasz Napierala <trasz@FreeBSD.org>                 */
	void *port_buffer = jack_port_get_buffer(jfst->midi_outport, nframes);
	/* We need always clear buffer if port is connected someware */
	jack_midi_clear_buffer(port_buffer);

	jack_nframes_t last_frame_time = jack_last_frame_time(jfst->client);
	jack_ringbuffer_t* ringbuffer = jfst->ringbuffer;
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

static inline void process_ctrl_output(JFST* jfst, jack_nframes_t nframes) {
	if ( ! jack_port_connected ( jfst->ctrl_outport ) ) return;

	void *port_buffer = jack_port_get_buffer(jfst->ctrl_outport, nframes);
	/* We need always clear buffer if port is connected someware */
	jack_midi_clear_buffer(port_buffer);

	jfst_sysex_rt_send ( jfst, port_buffer );
}

/* True if this message should be passed to plugin */
static inline void process_midi_in_msg ( JFST* jfst, jack_midi_event_t* jackevent, VstEvents* events )
{
	/* TODO: SysEx
	For now we simply drop all SysEx messages because VST standard
	require special Event type for this (kVstSysExType)
	and this type is not supported (yet ;-)
	... but we take over our messages
	*/
	if ( jackevent->buffer[0] == SYSEX_BEGIN) {
		jfst_queue_sysex(jfst, jackevent->buffer, jackevent->size);
		return;
	}

	/* Copy this MIDI event beacuse Jack gives same buffer to all clients and we cannot work on this data */
	jack_midi_data_t buf[jackevent->size];
	memcpy(&buf, jackevent->buffer, jackevent->size);

	/* MIDI FILTERS */
	if ( ! midi_filter_check( &jfst->filters, (uint8_t*) &buf, jackevent->size ) ) return;

	switch ( (buf[0] >> 4) & 0xF ) {
	case MM_CONTROL_CHANGE: ;
		// CC assigments
		uint8_t CC = buf[1];
		uint8_t VALUE = buf[2];

		// Want Mode
		if (CC == jfst->want_state_cc) {
			// 0-63 mean bypass, 64-127 mean resume
			event_queue_send_val ( &jfst->event_queue, EVENT_BYPASS, (VALUE <= 63) );
			return;
		// If Volume control is enabled then grab CC7 messages
		} else if (CC == 7 && jfst->volume != -1) {
			jfst_set_volume(jfst, VALUE);
			return;
		}

		// In bypass mode do not touch plugin
		if (jfst->bypassed) return;

		// Mapping MIDI CC
		MidiLearn* ml = &jfst->midi_learn;
		if ( ml->wait ) {
			ml->cc = CC;
		// handle mapped MIDI CC
		} else if ( ml->map[CC] != -1 ) {
			int32_t parameter = ml->map[CC];
			float value = 1.0/127.0 * (float) VALUE;
			fst_set_param( jfst->fst, parameter, value );
		}
		break;
	case MM_PROGRAM_CHANGE:
		// Self Program Change
		if (jfst->midi_pc != MIDI_PC_SELF) break;
		event_queue_send_val ( &jfst->event_queue, EVENT_PC, buf[1] );
		// OFC don't forward this message to plugin
		return;
	}

	// ... wanna play at all ?
	if ( jfst->bypassed || ! fst_want_midi_in(jfst->fst) ) return;
	
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

void jfst_process( JFST* jfst, jack_nframes_t nframes ) {
	int32_t i;
	FST* fst = jfst->fst;

	// Skip processing if we are during SetProgram, Suspend, Resume
	if ( ! fst_process_trylock ( fst ) ) return;

	// Do not process anything if MIDI IN port is not connected
	if ( ! jack_port_connected ( jfst->midi_inport ) ) goto no_midi_in;

	// ******************* Process MIDI Input ************************************

	// NOTE: we process MIDI even in bypass mode for want_state handling and our SysEx
	void *port_buffer = jack_port_get_buffer( jfst->midi_inport, nframes );
	jack_nframes_t num_jackevents = jack_midi_get_event_count( port_buffer );

	/* Allocate space for VST MIDI - preallocate space for all messages even
	   if we use only few - cause of alloca scope. Can't move this to separate
	   function because VST plug need this space during process call
	   NOTE: can't use VLA here because of goto above
	*/
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

		process_midi_in_msg ( jfst, &jackevent, events );
	}

	// ... let's the music play
	if ( events->numEvents > 0 )
		fst_process_events ( fst, events );

no_midi_in: ;
	/* Process AUDIO */
	float* ins[ fst_num_ins(fst) ];
	float* outs[ fst_num_outs(fst) ];

	// swap area ( nonused ports )
	float swap[jfst->buffer_size];
	memset ( swap, 0, jfst->buffer_size * sizeof(float) );

	// Get addresses of input buffers
	for (i = 0; i < fst_num_ins(fst); ++i)
		ins[i] = ( i < jfst->numIns ) ? (float*) jack_port_get_buffer(jfst->inports[i],nframes) : swap;

	// Initialize output buffers
	for (i = 0; i < fst_num_outs(fst); ++i) {
		if ( i < jfst->numOuts ) {
			// Get address
			outs[i]  = (float*) jack_port_get_buffer (jfst->outports[i], nframes);
	
			// If bypassed then copy In's to Out's
			if ( jfst->bypassed && i < jfst->numIns ) {
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
	if (jfst->bypassed) goto midi_out;

	// Deal with plugin
	fst_process( fst, ins, outs, nframes );

#ifdef VUMETER
	/* Compute output level for VU Meter */
	float avg_level = 0;
	jack_nframes_t n;
	for (n=0; n < nframes; n++) avg_level += fabs( outs[0][n] );
	avg_level /= nframes;

	jfst->out_level = avg_level * 100;
	if (jfst->out_level > 100) jfst->out_level = 100;
#endif

	jfst_apply_volume ( jfst, nframes, outs );

midi_out:
	// Process MIDI Output
	process_midi_output(jfst, nframes);
	process_ctrl_output(jfst, nframes);

	fst_process_unlock ( fst );
}
