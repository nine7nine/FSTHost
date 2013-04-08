/*
    Copyright (C) 2002 Kjetil S. Matheussen / Notam.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdio.h>
#include <math.h>

#include "jackvst.h"

/* The following audioMaster opcode handlings are copied
 * (with a light modification) from the vst tilde source
 * made by Mark Williamson.
*/

//#define DEBUG_CALLBACKS
//#define DEBUG_TIME

#ifdef DEBUG_CALLBACKS
#define SHOW_CALLBACK fst_error
#else
#define SHOW_CALLBACK(...)
#endif

extern void queue_midi_message(JackVST* jvst, int status, int d1, int d2, jack_nframes_t delta );

#ifdef DEBUG_TIME
#define SHOW_TIME show_time_flags
static void show_time_flags(intptr_t value) {
	char msg[512] = "";
	if (value & kVstNanosValid) strncat(msg, " kVstNanosValid", sizeof(msg) - 1);
	if (value & kVstPpqPosValid) strncat(msg, " kVstPpqPosValid", sizeof(msg) - 1);
	if (value & kVstTempoValid) strncat(msg, " kVstTempoValid", sizeof(msg) - 1);
	if (value & kVstBarsValid) strncat(msg, " kVstBarsValid", sizeof(msg) - 1);
	if (value & kVstCyclePosValid) strncat(msg, " kVstCyclePosVali", sizeof(msg) - 1);
	if (value & kVstTimeSigValid) strncat(msg, " kVstTimeSigValid", sizeof(msg) - 1);
	if (value & kVstSmpteValid) strncat(msg, " kVstSmpteValid", sizeof(msg) - 1);
	if (value & kVstClockValid) strncat(msg, " kVstClockValid", sizeof(msg) - 1);
	msg[511] = '\0';
	fst_error("amc time:%s\n", msg);
}
#else
#define SHOW_TIME(...)
#endif

intptr_t VSTCALLBACK
jack_host_callback (struct AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
	JackVST* jackvst = effect ? ((JackVST*) effect->resvd1) : NULL;

	//SHOW_CALLBACK ("am callback, opcode = %d", opcode);
	
	switch(opcode) {

	case audioMasterAutomate:
		SHOW_CALLBACK ("amc: audioMasterAutomate\n");
		// index, value, returns 0
		if( jackvst && jackvst->midi_learn )
			jackvst->midi_learn_PARAM = index;
		return 0;

	case audioMasterVersion:
		SHOW_CALLBACK ("amc: audioMasterVersion\n");
		// vst version, currently 2 (0 for older)
		return 2;

	case audioMasterCurrentId:	
		SHOW_CALLBACK ("amc: audioMasterCurrentId\n");
		// returns the unique id of a plug that's currently
		// loading
		return 0;
		
	case audioMasterIdle:
		SHOW_CALLBACK ("amc: audioMasterIdle\n");
		// call application idle routine (this will
		// call effEditIdle for all open editors too) 
		effect->dispatcher(effect, effEditIdle, 0, 0, NULL, 0.0f);
		return 0;

	case audioMasterPinConnected:		
		SHOW_CALLBACK ("amc: audioMasterPinConnected\n");
		// inquire if an input or output is beeing connected;
		// index enumerates input or output counting from zero:
		// value is 0 for input and != 0 otherwise. note: the
		// return value is 0 for <true> such that older versions
		// will always return true.
		return 0;

	case audioMasterWantMidi:
		SHOW_CALLBACK ("amc: audioMasterWantMidi\n");
		// <value> is a filter which is currently ignored
		return 0;

	case audioMasterGetTime:
		SHOW_CALLBACK ("amc: audioMasterGetTime\n");
		SHOW_TIME(value);
		// returns const VstTimeInfo* (or 0 if not supported)
		// <value> should contain a mask indicating which fields are required
		// (see valid masks above), as some items may require extensive
		// conversions
		if (! jackvst) return 0;
		struct VstTimeInfo* timeInfo = &jackvst->fst->timeInfo;

		// We always say that something was changed (are we lie ?)
		timeInfo->flags = ( kVstTransportChanged | kVstTempoValid | kVstPpqPosValid );
		// Query JackTransport
		jack_position_t jack_pos;
		jack_transport_state_t tstate = jack_transport_query (jackvst->client, &jack_pos);
		// Are we play ?
		if (tstate == JackTransportRolling) timeInfo->flags |= kVstTransportPlaying;
		// samplePos - always valid
		timeInfo->samplePos = jack_pos.frame;
		// sampleRate - always valid
		timeInfo->sampleRate = jack_pos.frame_rate;
		// nanoSeconds - valid when kVstNanosValid is set
		if (value & kVstNanosValid) {
			timeInfo->nanoSeconds = jack_pos.usecs / 1000;
			timeInfo->flags |= kVstNanosValid;
		}
		// tempo - valid when kVstTempoValid is set
		// ... but we always set tempo ;-)
		timeInfo->tempo = (jackvst->tempo == -1) ? 
			( (jack_pos.beats_per_minute) ? jack_pos.beats_per_minute : 120 ) :
			jackvst->tempo;
		// ppqPos - valid when kVstPpqPosValid is set
		// ... but we always compute it - could be needed later
		double ppq = timeInfo->sampleRate * 60 / timeInfo->tempo;
		timeInfo->ppqPos = timeInfo->samplePos / ppq;
		if (jack_pos.valid & JackPositionBBT) {
			// barStartPos - valid when kVstBarsValid is set
			if (value & kVstBarsValid) {
				timeInfo->barStartPos = timeInfo->ppqPos - jack_pos.beat + 1;
				timeInfo->flags |= kVstBarsValid;
			}
			// timeSigNumerator & timeSigDenominator - valid when kVstTimeSigValid is set
			if (value & kVstTimeSigValid) {
				timeInfo->timeSigNumerator = (int32_t) floor (jack_pos.beats_per_bar);
				timeInfo->timeSigDenominator = (int32_t) floor (jack_pos.beat_type);
				timeInfo->flags |= kVstTimeSigValid;
			}
		}
#ifdef DEBUG_TIME
		fst_error("amc ppq: %g\n", ppq);
		fst_error("amc ppqPos: %g\n", timeInfo->ppqPos);
		fst_error("amc barStartPos: %g\n", timeInfo->barStartPos);
		fst_error("amc answer flags: %d\n", timeInfo->flags);
#endif
		// cycleStartPos & cycleEndPos - valid when kVstCyclePosValid is set
		// FIXME: not supported yet (acctually do we need this ?) 
		// smpteOffset && smpteFrameRate - valid when kVstSmpteValid is set
		// FIXME: not supported yet (acctually do we need this ?) 
		// samplesToNextClock - valid when kVstClockValid is set
		// FIXME: not supported yet (acctually do we need this ?)
		return (intptr_t) timeInfo;
	case audioMasterProcessEvents:
		SHOW_CALLBACK ("amc: audioMasterProcessEvents\n");
		// VstEvents* in <ptr>
		if (! jackvst) return 0;

		int i;
		long numEvents;
		VstEvents* events = (VstEvents*)ptr;

		numEvents = events->numEvents;
		for (i = 0; i < numEvents; i++) {
			char* midiData;
			VstMidiEvent* event = (VstMidiEvent*)events->events[i];
			//printf( "delta = %d\n", (int) event->deltaFrames );
			midiData = event->midiData;
			queue_midi_message(jackvst, midiData[0], midiData[1], midiData[2], event->deltaFrames);
		}
		return 1;
	case audioMasterSetTime:
		SHOW_CALLBACK ("amc: audioMasterSetTime\n");
		// VstTimenfo* in <ptr>, filter in <value>, not supported
		return 0;

	case audioMasterTempoAt:
		SHOW_CALLBACK ("amc: audioMasterTempoAt\n");
		intptr_t ret = 120;
		if (jackvst) {
			if (jackvst->tempo != -1) {
				ret = jackvst->tempo;
			} else {
				jack_position_t jack_pos;
				jack_transport_query (jackvst->client, &jack_pos);
				if (jack_pos.beats_per_minute) ret = (intptr_t) jack_pos.beats_per_minute;
			}
		}
		
		// returns tempo (in bpm * 10000) at sample frame location passed in <value>
		return ret * 10000;
	case audioMasterGetNumAutomatableParameters:
		SHOW_CALLBACK ("amc: audioMasterGetNumAutomatableParameters\n");
		return 0;

	case audioMasterGetParameterQuantization:	
		SHOW_CALLBACK ("amc: audioMasterGetParameterQuantization\n");
		// returns the integer value for +1.0 representation,
		// or 1 if full single float precision is maintained
		// in automation. parameter index in <value> (-1: all, any)
		return 0;

	case audioMasterIOChanged:
		SHOW_CALLBACK ("amc: audioMasterIOChanged\n");
		// numInputs and/or numOutputs has changed
		return 0;

	case audioMasterNeedIdle:
		SHOW_CALLBACK ("amc: audioMasterNeedIdle\n");
		// plug needs idle calls (outside its editor window)
                if( jackvst ) jackvst->fst->wantIdle = TRUE;
		return 1;
	case audioMasterSizeWindow:
		SHOW_CALLBACK ("amc: audioMasterSizeWindow %d %d\n", index, value);
		// index: width, value: height
		if (! jackvst) return 0;

		jackvst->fst->width = index + 6;
		jackvst->fst->height = value + 24;
		jackvst->fst->wantResize = TRUE;
		/* Resize also GTK window in popup (embedded) mode */
		if (jackvst->fst->editor_popup) jackvst->want_resize = TRUE;
		return 1;
	case audioMasterGetSampleRate:
		SHOW_CALLBACK ("amc: audioMasterGetSampleRate\n");
		if( jackvst )
		    return jack_get_sample_rate( jackvst->client );

		return 44100;

	case audioMasterGetBlockSize:
		SHOW_CALLBACK ("amc: audioMasterGetBlockSize\n");
		if( jackvst )
		    return jack_get_buffer_size( jackvst->client );

		return 1024;

	case audioMasterGetInputLatency:
		SHOW_CALLBACK ("amc: audioMasterGetInputLatency\n");
		return 0;

	case audioMasterGetOutputLatency:
		SHOW_CALLBACK ("amc: audioMasterGetOutputLatency\n");
		return 0;

	case audioMasterGetPreviousPlug:
		SHOW_CALLBACK ("amc: audioMasterGetPreviousPlug\n");
	       // input pin in <value> (-1: first to come), returns cEffect*
		return 0;

	case audioMasterGetNextPlug:
		SHOW_CALLBACK ("amc: audioMasterGetNextPlug\n");
	       // output pin in <value> (-1: first to come), returns cEffect*

	case audioMasterWillReplaceOrAccumulate:
		SHOW_CALLBACK ("amc: audioMasterWillReplaceOrAccumulate\n");
	       // returns: 0: not supported, 1: replace, 2: accumulate
		return 0;

	case audioMasterGetCurrentProcessLevel:
		SHOW_CALLBACK ("amc: audioMasterGetCurrentProcessLevel\n");
		// returns: 0: not supported,
		// 1: currently in user thread (gui)
		// 2: currently in audio thread (where process is called)
		// 3: currently in 'sequencer' thread (midi, timer etc)
		// 4: currently offline processing and thus in user thread
		// other: not defined, but probably pre-empting user thread.
		return 0;
		
	case audioMasterGetAutomationState:
		SHOW_CALLBACK ("amc: audioMasterGetAutomationState\n");
		// returns 0: not supported, 1: off, 2:read, 3:write, 4:read/write
		// offline
		return 4;

	case audioMasterOfflineStart:
		SHOW_CALLBACK ("amc: audioMasterOfflineStart\n");
		return 0;

	case audioMasterOfflineRead:
		SHOW_CALLBACK ("amc: audioMasterOfflineRead\n");
		// ptr points to offline structure, see below. return 0: error, 1 ok
		return 0;

	case audioMasterOfflineWrite:
		SHOW_CALLBACK ("amc: audioMasterOfflineWrite\n");
		// same as read
		return 0;

	case audioMasterOfflineGetCurrentPass:
		SHOW_CALLBACK ("amc: audioMasterOfflineGetCurrentPass\n");
		return 0;

	case audioMasterOfflineGetCurrentMetaPass:
		SHOW_CALLBACK ("amc: audioMasterOfflineGetCurrentMetaPass\n");
		return 0;

	case audioMasterSetOutputSampleRate:
		SHOW_CALLBACK ("amc: audioMasterSetOutputSampleRate\n");
		// for variable i/o, sample rate in <opt>
		return 0;

	case audioMasterGetSpeakerArrangement:
		SHOW_CALLBACK ("amc: audioMasterGetSpeakerArrangement\n");
		// (long)input in <value>, output in <ptr>
		return 0;

	case audioMasterGetVendorString:
		SHOW_CALLBACK ("amc: audioMasterGetVendorString\n");
		// fills <ptr> with a string identifying the vendor (max 64 char)
		strcpy ((char*) ptr, "Xj");
		return 0;

	case audioMasterGetProductString:
		SHOW_CALLBACK ("amc: audioMasterGetProductString\n");
		// fills <ptr> with a string with product name (max 64 char)
		strcpy ((char*) ptr, "FSTHost");
		return 0;

	case audioMasterGetVendorVersion:
		SHOW_CALLBACK ("amc: audioMasterGetVendorVersion\n");
		// returns vendor-specific version
		return 1000;
		
	case audioMasterVendorSpecific:
		SHOW_CALLBACK ("amc: audioMasterVendorSpecific\n");
		// no definition, vendor specific handling
		return 0;
		
	case audioMasterSetIcon:
		SHOW_CALLBACK ("amc: audioMasterSetIcon\n");
		// void* in <ptr>, format not defined yet
		return 0;
	case audioMasterCanDo:
		SHOW_CALLBACK ("amc: audioMasterCanDo %s\n", (char*)ptr);
		/* Supported */
		if (
			!strcmp((char*)ptr, "sendVstEvents") ||
			!strcmp((char*)ptr, "sendVstMidiEvent") ||
			!strcmp((char*)ptr, "sendVstTimeInfo") ||
			!strcmp((char*)ptr, "receiveVstEvents") ||
			!strcmp((char*)ptr, "receiveVstMidiEvent") ||
			!strcmp((char*)ptr, "sizeWindow") ||
			!strcmp((char*)ptr, "supplyIdle")
		) return 1;

		/* Not Supported */
		if (
			!strcmp((char*)ptr, "reportConnectionChanges") ||
			!strcmp((char*)ptr, "acceptIOChanges") ||
			!strcmp((char*)ptr, "offline") ||
			!strcmp((char*)ptr, "openFileSelector") ||
			!strcmp((char*)ptr, "closeFileSelector") ||
			!strcmp((char*)ptr, "startStopProcess") ||
			!strcmp((char*)ptr, "shellCategory") ||
			!strcmp((char*)ptr, "asyncProcessing") ||
			!strcmp((char*)ptr, "sendVstMidiEventFlagIsRealtime")
		) return -1;

		/* What this plugin want from us ? */
		return 0;
	case audioMasterGetLanguage:
		SHOW_CALLBACK ("amc: audioMasterGetLanguage\n");
		// see enum
		return 0;
		
	case audioMasterOpenWindow:
		SHOW_CALLBACK ("amc: audioMasterOpenWindow\n");
		// returns platform specific ptr
		return 0;
		
	case audioMasterCloseWindow:
		SHOW_CALLBACK ("amc: audioMasterCloseWindow\n");
		// close window, platform specific handle in <ptr>
		return 0;
		
	case audioMasterGetDirectory:
		SHOW_CALLBACK ("amc: audioMasterGetDirectory\n");
		// get plug directory, FSSpec on MAC, else char*
		return 0;
		
	case audioMasterUpdateDisplay:
		SHOW_CALLBACK ("amc: audioMasterUpdateDisplay\n");
		// something has changed, update 'multi-fx' display
		effect->dispatcher(effect, effEditIdle, 0, 0, NULL, 0.0f);
		return 0;
		
	case audioMasterBeginEdit:
		SHOW_CALLBACK ("amc: audioMasterBeginEdit\n");
		// begin of automation session (when mouse down), parameter index in <index>
		return 0;
		
	case audioMasterEndEdit:
		SHOW_CALLBACK ("amc: audioMasterEndEdit\n");
		// end of automation session (when mouse up),     parameter index in <index>
		return 0;
		
	case audioMasterOpenFileSelector:
		SHOW_CALLBACK ("amc: audioMasterOpenFileSelector\n");
		// open a fileselector window with VstFileSelect* in <ptr>
		return 0;
		
	default:
		SHOW_CALLBACK ("VST master dispatcher: undefed: %d\n", opcode);
		break;
	}	
	
	return 0;
}

