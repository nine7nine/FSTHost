#include <math.h>

#include "jfst.h"
#include "fst/amc.h"
#include "log/log.h"

static void jfstamc_automate ( AMC* amc, int32_t param ) {
	JFST* jfst = (JFST*) amc->user_ptr;

	MidiLearn* ml = &(jfst->midi_learn);
	if ( ml->wait ) ml->param = param;
}

static void jfstamc_get_time ( AMC* amc, int32_t mask ) {
	JFST* jfst = (JFST*) amc->user_ptr;

	struct VstTimeInfo* timeInfo = &amc->timeInfo;

	// We always say that something was changed (are we lie ?)
	timeInfo->flags = ( kVstTransportChanged | kVstTempoValid | kVstPpqPosValid );
	// Query JackTransport
	jack_position_t jack_pos;
	jack_transport_state_t tstate = jack_transport_query (jfst->client, &jack_pos);
	// Are we play ?
	if (tstate == JackTransportRolling) timeInfo->flags |= kVstTransportPlaying;
	// samplePos - always valid
	timeInfo->samplePos = jack_pos.frame;
	// sampleRate - always valid
	timeInfo->sampleRate = jack_pos.frame_rate;
	// tempo - valid when kVstTempoValid is set ... but we always set tempo ;-)
	timeInfo->tempo = (jack_pos.valid & JackPositionBBT) ? jack_pos.beats_per_minute : 120;
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
		double ppqTick = jack_pos.tick / jack_pos.ticks_per_beat;
		if ( jack_pos.valid & JackBBTFrameOffset && jack_pos.bbt_offset > 0 )
			ppqOffset = jack_pos.bbt_offset / ppq; // ppq is frames per beat

		BBT_ppqPos = ppqBar + ppqBeat + ppqTick + ppqOffset;

		// barStartPos - valid when kVstBarsValid is set
		if (mask & kVstBarsValid) {
			BBT_barStartPos = ppqBar;
			timeInfo->flags |= kVstBarsValid;
		}

		// timeSigNumerator & timeSigDenominator - valid when kVstTimeSigValid is set
		if (mask & kVstTimeSigValid) {
			timeInfo->timeSigNumerator = (int32_t) floor (jack_pos.beats_per_bar);
			timeInfo->timeSigDenominator = (int32_t) floor (jack_pos.beat_type);
			timeInfo->flags |= kVstTimeSigValid;
		}

		timeInfo->ppqPos = BBT_ppqPos;
		timeInfo->barStartPos = BBT_barStartPos;
	} else {
		timeInfo->ppqPos = VST_ppqPos;
		VST_barStartPos = 4 * floor(VST_ppqPos / 4);
		timeInfo->barStartPos = VST_barStartPos;
	}

	log_debug("amc JACK: Bar %d, Beat %d, Tick %d, Offset %d, BeatsPerBar %f",
		jack_pos.bar, jack_pos.beat, jack_pos.tick, jack_pos.bbt_offset, jack_pos.beats_per_bar);

	log_debug("amc ppq: %f", ppq);
	log_debug("amc TIMEINFO BBT: ppqPos %f, barStartPos %6.4f, remain %4.2f, Offset %f",
		BBT_ppqPos, BBT_barStartPos, BBT_ppqPos - BBT_barStartPos, ppqOffset);

	log_debug("amc TIMEINFO VST: ppqPos %f, barStartPos %6.4f, remain %4.2f",
		VST_ppqPos, VST_barStartPos, VST_ppqPos - VST_barStartPos);

	log_debug("amc answer flags: %d", timeInfo->flags);

	// cycleStartPos & cycleEndPos - valid when kVstCyclePosValid is set
	// FIXME: not supported yet (acctually do we need this ?) 
	// smpteOffset && smpteFrameRate - valid when kVstSmpteValid is set
	// FIXME: not supported yet (acctually do we need this ?) 
	// samplesToNextClock - valid when kVstClockValid is set
	// FIXME: not supported yet (acctually do we need this ?)
}

static inline void
queue_midi_message(JFST* jfst, struct MidiMessage* mm) {
	uint8_t statusHi = (mm->status >> 4) & 0xF;
	uint8_t statusLo = mm->status & 0xF;

	log_debug("queue_new_message: sHi %d sLo %d, d1 %d, d2 %d", statusHi, statusLo, mm->d1, mm->d2);

	if (statusHi == 0xC || statusHi == 0xD) {
		mm->len = 2;
	} else if (statusHi == 0xF) {
		if (statusLo == 0 || statusLo == 2)
			mm->len = 3;
		else if (statusLo == 1 || statusLo == 3)
			mm->len = 2;
		else mm->len = 1;
	} else mm->len = 3;

	// Change delta to jack time
	mm->time += jack_frame_time(jfst->client);
	size_t size = sizeof(struct MidiMessage);

	jack_ringbuffer_t* ringbuffer = jfst->ringbuffer;
	if ( jack_ringbuffer_write_space(ringbuffer) < size ) {
		log_error("Not enough space in the ringbuffer, NOTE LOST.");
		return;
	}

	size_t written = jack_ringbuffer_write(ringbuffer, (char*) mm, size);
	if (written != size) log_error("jack_ringbuffer_write failed, NOTE LOST.");
}

static bool jfstamc_process_events ( AMC* amc, VstEvents* events ) {
	JFST* jfst = (JFST*) amc->user_ptr;

	int32_t i;
	for (i = 0; i < events->numEvents; i++) {
		VstMidiEvent* event = (VstMidiEvent*) events->events[i];
		//log_debug( "delta = %d\n", event->deltaFrames );

		struct MidiMessage mm = {
			.status = event->midiData[0],
			.time   = event->deltaFrames,
			.len    = 0, // Will be computed in queu_midi_message
			.d1     = event->midiData[1],
			.d2     = event->midiData[2]
		};

		queue_midi_message(jfst, &mm);
	}
	return true;
}

static intptr_t jfstamc_tempo ( AMC* amc, int32_t location ) {
	JFST* jfst = (JFST*) amc->user_ptr;
	
	jack_position_t jack_pos;
	jack_transport_query (jfst->client, &jack_pos);
	
	return (jack_pos.beats_per_minute) ? jack_pos.beats_per_minute : 120;
}

static void jfstamc_window_resize ( AMC* amc, int32_t width, int32_t height ) {
	JFST* jfst = (JFST*) amc->user_ptr;
	fst_editor_resize(amc->fst, width, height);

	/* Resize also GTK window in popup (embedded) mode */
	if ( jfst->gui_resize )
		jfst->gui_resize( jfst );
}

/* return true if editor is opened */
static bool jfstamc_update_display ( AMC* amc ) {
	return fst_has_window(amc->fst);
}

void jfstamc_init ( JFST* jfst, FST* fst ) {
	AMC* amc		= fst_amc(fst);
	amc->user_ptr		= jfst;
	amc->Automate		= &jfstamc_automate;
	amc->GetTime		= &jfstamc_get_time;
	amc->ProcessEvents	= &jfstamc_process_events;
	amc->TempoAt		= &jfstamc_tempo;
//	amc->NeedIdle		= &jfstamc_need_idle;
	amc->SizeWindow		= &jfstamc_window_resize;
	amc->UpdateDisplay	= &jfstamc_update_display;
}
