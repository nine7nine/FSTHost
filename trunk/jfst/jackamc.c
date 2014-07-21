#include <math.h>

#include "jfst.h"
#include "../fst/amc.h"

static void jvstamc_automate ( AMC* amc, int32_t param ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	if ( ! jvst ) return;

	MidiLearn* ml = &(jvst->midi_learn);
	if ( ml->wait ) ml->param = param;
}

static VstTimeInfo* jvstamc_get_time ( AMC* amc, int32_t mask ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	if ( ! jvst ) return NULL;

	struct VstTimeInfo* timeInfo = &amc->timeInfo;

	// We always say that something was changed (are we lie ?)
	timeInfo->flags = ( kVstTransportChanged | kVstTempoValid | kVstPpqPosValid );
	// Query JackTransport
	jack_position_t jack_pos;
	jack_transport_state_t tstate = jack_transport_query (jvst->client, &jack_pos);
	// Are we play ?
	if (tstate == JackTransportRolling) timeInfo->flags |= kVstTransportPlaying;
	// samplePos - always valid
	timeInfo->samplePos = jack_pos.frame;
	// sampleRate - always valid
	timeInfo->sampleRate = jack_pos.frame_rate;
	// tempo - valid when kVstTempoValid is set ... but we always set tempo ;-)
	if ( jvst->tempo != -1 ) {
		timeInfo->tempo = jvst->tempo;
	} else if (jack_pos.valid & JackPositionBBT) {
		timeInfo->tempo = jack_pos.beats_per_minute;
	} else {
		timeInfo->tempo = 120;
	}
	// nanoSeconds - valid when kVstNanosValid is set
	if (mask & kVstNanosValid) {
		timeInfo->nanoSeconds = jack_pos.usecs / 1000;
		timeInfo->flags |= kVstNanosValid;
	}
	// ppqPos - valid when kVstPpqPosValid is set
	// ... but we always compute it - could be needed later
	double ppq = timeInfo->sampleRate * 60 / timeInfo->tempo;
	double VST_ppqPos = timeInfo->samplePos / ppq;
	double VST_barStartPos = 0.0;

	double BBT_ppqPos = 0.0;
	double BBT_barStartPos = 0.0;
	double ppqOffset = 0.0;

	if (jack_pos.valid & JackPositionBBT) {
		double ppqBar = (jack_pos.bar - 1) * jack_pos.beats_per_bar;
		double ppqBeat = jack_pos.beat - 1;
		double ppqTick = (double) jack_pos.tick / jack_pos.ticks_per_beat;
		if (jack_pos.valid & JackBBTFrameOffset) {
			jack_nframes_t nframes = jack_get_buffer_size (jvst->client);
			ppqOffset = (double) jack_pos.bbt_offset / nframes;
		}
		BBT_ppqPos = ppqBar + ppqBeat + ppqTick;

		// barStartPos - valid when kVstBarsValid is set
		if (mask & kVstBarsValid) {
			BBT_barStartPos = ppqBar;
			VST_barStartPos = jack_pos.beats_per_bar * floor(VST_ppqPos / jack_pos.beats_per_bar);
			timeInfo->flags |= kVstBarsValid;
		}

		// timeSigNumerator & timeSigDenominator - valid when kVstTimeSigValid is set
		if (mask & kVstTimeSigValid) {
			timeInfo->timeSigNumerator = (int32_t) floor (jack_pos.beats_per_bar);
			timeInfo->timeSigDenominator = (int32_t) floor (jack_pos.beat_type);
			timeInfo->flags |= kVstTimeSigValid;
		}
	}

	if ( jvst->bbt_sync ) {
		timeInfo->ppqPos = BBT_ppqPos;
		timeInfo->barStartPos = BBT_barStartPos;
	} else {
		timeInfo->ppqPos = VST_ppqPos;
		timeInfo->barStartPos = VST_barStartPos;
	}

	// Workadound for warning
	ppqOffset = ppqOffset;

#ifdef DEBUG_TIME
	fst_error("amc JACK: Bar %d, Beat %d, Tick %d, Offset %d, BeatsPerBar %f",
		jack_pos.bar, jack_pos.beat, jack_pos.tick, jack_pos.bbt_offset, jack_pos.beats_per_bar);

	fst_error("amc ppq: %f", ppq);
	fst_error("amc TIMEINFO BBT: ppqPos %f, barStartPos %6.4f, remain %4.2f, Offset %f",
		BBT_ppqPos, BBT_barStartPos, BBT_ppqPos - BBT_barStartPos, ppqOffset);

	fst_error("amc TIMEINFO VST: ppqPos %f, barStartPos %6.4f, remain %4.2f",
		VST_ppqPos, VST_barStartPos, VST_ppqPos - VST_barStartPos);

	fst_error("amc answer flags: %d", timeInfo->flags);
#endif
	// cycleStartPos & cycleEndPos - valid when kVstCyclePosValid is set
	// FIXME: not supported yet (acctually do we need this ?) 
	// smpteOffset && smpteFrameRate - valid when kVstSmpteValid is set
	// FIXME: not supported yet (acctually do we need this ?) 
	// samplesToNextClock - valid when kVstClockValid is set
	// FIXME: not supported yet (acctually do we need this ?)

	return timeInfo;
}

static void
queue_midi_message(JackVST* jvst, uint8_t status, uint8_t d1, uint8_t d2, jack_nframes_t delta ) {
	uint8_t statusHi = (status >> 4) & 0xF;
	uint8_t statusLo = status & 0xF;

	/* fst_error("queue_new_message = 0x%hhX, %d, %d", status, d1, d2);*/
	/* fst_error("statusHi = %d, statusLo = %d", statusHi, statusLo);*/

	struct  MidiMessage ev;
	ev.data[0] = status;
	if (statusHi == 0xC || statusHi == 0xD) {
		ev.len = 2;
		ev.data[1] = d1;
	} else if (statusHi == 0xF) {
		if (statusLo == 0 || statusLo == 2) {
			ev.len = 3;
			ev.data[1] = d1;
			ev.data[2] = d2;
		} else if (statusLo == 1 || statusLo == 3) {
			ev.len = 2;
			ev.data[1] = d1;
		} else ev.len = 1;
	} else {
		ev.len = 3;
		ev.data[1] = d1;
		ev.data[2] = d2;
	}

	ev.time = jack_frame_time(jvst->client) + delta;

	jack_ringbuffer_t* ringbuffer = jvst->ringbuffer;
	if (jack_ringbuffer_write_space(ringbuffer) < sizeof(ev)) {
		fst_error("Not enough space in the ringbuffer, NOTE LOST.");
		return;
	}

	size_t written = jack_ringbuffer_write(ringbuffer, (char*)&ev, sizeof(ev));
	if (written != sizeof(ev)) fst_error("jack_ringbuffer_write failed, NOTE LOST.");
}

static bool jvstamc_process_events ( AMC* amc, VstEvents* events ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	if ( ! jvst ) return false;

	int32_t numEvents = events->numEvents;
	int32_t i;
	for (i = 0; i < numEvents; i++) {
		VstMidiEvent* event = (VstMidiEvent*) events->events[i];
		//printf( "delta = %d\n", event->deltaFrames );
		char* midiData = event->midiData;
		queue_midi_message(jvst, midiData[0], midiData[1], midiData[2], event->deltaFrames);
	}
	return true;
}

static intptr_t jvstamc_tempo ( struct _AMC* amc, int32_t location ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	if ( jvst ) {
		if (jvst->tempo != -1) {
			return jvst->tempo;
		} else {
			jack_position_t jack_pos;
			jack_transport_query (jvst->client, &jack_pos);
			if (jack_pos.beats_per_minute) return (intptr_t) jack_pos.beats_per_minute;
		}
	}

	return 120;
}

static void jvstamc_need_idle ( struct _AMC* amc ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	if ( jvst && jvst->fst )
		jvst->fst->wantIdle = TRUE;
}

static void jvstamc_window_resize ( struct _AMC* amc, int32_t width, int32_t height ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	if ( ! ( jvst && jvst->fst ) ) return;

	jvst->fst->width = width;
	jvst->fst->height = height;
	jvst->fst->wantResize = TRUE;
	/* Resize also GTK window in popup (embedded) mode */
	if (jvst->fst->editor_popup) jvst->want_resize = TRUE;
}

static intptr_t jvstamc_get_sample_rate ( struct _AMC* amc ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	return ( jvst ) ? jack_get_sample_rate( jvst->client ) : 44100;
}

static intptr_t jvstamc_get_buffer_size ( struct _AMC* amc ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	return ( jvst) ? jack_get_buffer_size( jvst->client ) : 1024;
}

/* return true if editor is opened */
static bool jvstamc_update_display ( struct _AMC* amc ) {
	JackVST* jvst = (JackVST*) amc->user_ptr;
	return ( jvst && jvst->fst && jvst->fst->window ) ? true : false;
}

void jvstamc_init ( JackVST* jvst, AMC* amc ) {
	amc->user_ptr		= jvst;
	amc->Automate		= &jvstamc_automate;
	amc->GetTime		= &jvstamc_get_time;
	amc->ProcessEvents	= &jvstamc_process_events;
	amc->TempoAt		= &jvstamc_tempo;
	amc->NeedIdle		= &jvstamc_need_idle;
	amc->SizeWindow		= &jvstamc_window_resize;
	amc->GetSampleRate	= &jvstamc_get_sample_rate;
	amc->GetBlockSize	= &jvstamc_get_buffer_size;
	amc->UpdateDisplay	= &jvstamc_update_display;
}
