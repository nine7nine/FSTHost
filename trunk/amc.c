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

#include "amc.h"

/* The following audioMaster opcode handlings are copied
 * (with a light modification) from the vst tilde source
 * made by Mark Williamson.
*/

//#define DEBUG_CALLBACKS
//#define DEBUG_TIME

#ifdef DEBUG_CALLBACKS
extern void fst_error (const char *fmt, ...);
#define SHOW_CALLBACK fst_error
#else
#define SHOW_CALLBACK(...)
#endif

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
	fst_error("amc time:%s", msg);
}
#else
#define SHOW_TIME(...)
#endif

// most simple one :) could be sufficient.... 
intptr_t VSTCALLBACK
amc_simple_callback( AEffect *fx, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt )
{
	if ( opcode == audioMasterVersion ) return 2;
	return 0;
}

intptr_t VSTCALLBACK
amc_callback (struct AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
	AMC* amc = effect ? ((AMC*) effect->resvd1) : NULL;

	//SHOW_CALLBACK ("am callback, opcode = %d", opcode);
	
	switch ( opcode ) {
	case audioMasterAutomate:
		SHOW_CALLBACK ("amc: audioMasterAutomate\n");
		// index, value, returns 0
		if ( amc && amc->Automate ) amc->Automate ( amc, index );
		break;
	case audioMasterVersion:
		SHOW_CALLBACK ("amc: audioMasterVersion\n");
		// vst version, currently 2 (0 for older)
		return 2;
	case audioMasterCurrentId:	
		SHOW_CALLBACK ("amc: audioMasterCurrentId\n");
		// returns the unique id of a plug that's currently
		// loading
		break;
	case audioMasterIdle:
		SHOW_CALLBACK ("amc: audioMasterIdle\n");
		// call application idle routine (this will
		// call effEditIdle for all open editors too) 
		effect->dispatcher(effect, effEditIdle, 0, 0, NULL, 0.0f);
		break;
	case audioMasterPinConnected:		
		SHOW_CALLBACK ("amc: audioMasterPinConnected\n");
		// inquire if an input or output is beeing connected;
		// index enumerates input or output counting from zero:
		// value is 0 for input and != 0 otherwise. note: the
		// return value is 0 for <true> such that older versions
		// will always return true.
		break;
	case audioMasterWantMidi:
		SHOW_CALLBACK ("amc: audioMasterWantMidi\n");
		// <value> is a filter which is currently ignored
		break;
	case audioMasterGetTime:
		SHOW_CALLBACK ("amc: audioMasterGetTime\n");
		SHOW_TIME(value);
		// returns const VstTimeInfo* (or 0 if not supported)
		// <value> should contain a mask indicating which fields are required
		// (see valid masks above), as some items may require extensive
		// conversions
		if ( amc && amc->GetTime ) return (intptr_t) amc->GetTime ( amc, value );
		break;
	case audioMasterProcessEvents:
		SHOW_CALLBACK ("amc: audioMasterProcessEvents\n");
		// VstEvents* in <ptr>
		if ( amc && amc->ProcessEvents ) return amc->ProcessEvents( amc, (VstEvents*) ptr );
		break;
	case audioMasterSetTime:
		SHOW_CALLBACK ("amc: audioMasterSetTime\n");
		// VstTimenfo* in <ptr>, filter in <value>, not supported
		break;
	case audioMasterTempoAt:
		SHOW_CALLBACK ("amc: audioMasterTempoAt\n");
		// returns tempo (in bpm * 10000) at sample frame location passed in <value>
		intptr_t ret = 120;
		if ( amc && amc->TempoAt ) ret = amc->TempoAt( amc, value );
		return ret * 10000;
	case audioMasterGetNumAutomatableParameters:
		SHOW_CALLBACK ("amc: audioMasterGetNumAutomatableParameters\n");
		break;
	case audioMasterGetParameterQuantization:	
		SHOW_CALLBACK ("amc: audioMasterGetParameterQuantization\n");
		// returns the integer value for +1.0 representation,
		// or 1 if full single float precision is maintained
		// in automation. parameter index in <value> (-1: all, any)
		break;
	case audioMasterIOChanged:
		SHOW_CALLBACK ("amc: audioMasterIOChanged\n");
		// numInputs and/or numOutputs has changed
		break;
	case audioMasterNeedIdle:
		SHOW_CALLBACK ("amc: audioMasterNeedIdle\n");
		// plug needs idle calls (outside its editor window)
		if ( amc && amc->NeedIdle ) amc->NeedIdle( amc );
		return 1;
	case audioMasterSizeWindow:
		SHOW_CALLBACK ("amc: audioMasterSizeWindow %d %d\n", index, value);
		// index: width, value: height
		if (amc && amc->SizeWindow) {
			amc->SizeWindow( amc, index, value );
			return 1; /* Say : support it */
		}
		break;
	case audioMasterGetSampleRate:
		SHOW_CALLBACK ("amc: audioMasterGetSampleRate\n");
		if ( amc && amc->GetSampleRate )
			return amc->GetSampleRate ( amc );
		return 44100;
	case audioMasterGetBlockSize:
		SHOW_CALLBACK ("amc: audioMasterGetBlockSize\n");
		if ( amc && amc->GetBlockSize )
			return amc->GetBlockSize ( amc );
		return 1024;
	case audioMasterGetInputLatency:
		SHOW_CALLBACK ("amc: audioMasterGetInputLatency\n");
		break;
	case audioMasterGetOutputLatency:
		SHOW_CALLBACK ("amc: audioMasterGetOutputLatency\n");
		break;
	case audioMasterGetPreviousPlug:
		SHOW_CALLBACK ("amc: audioMasterGetPreviousPlug\n");
		// input pin in <value> (-1: first to come), returns cEffect*
		break;
	case audioMasterGetNextPlug:
		SHOW_CALLBACK ("amc: audioMasterGetNextPlug\n");
		// output pin in <value> (-1: first to come), returns cEffect*
		break;
	case audioMasterWillReplaceOrAccumulate:
		SHOW_CALLBACK ("amc: audioMasterWillReplaceOrAccumulate\n");
		// returns: 0: not supported, 1: replace, 2: accumulate
		break;
	case audioMasterGetCurrentProcessLevel:
		SHOW_CALLBACK ("amc: audioMasterGetCurrentProcessLevel\n");
		// returns: 0: not supported,
		// 1: currently in user thread (gui)
		// 2: currently in audio thread (where process is called)
		// 3: currently in 'sequencer' thread (midi, timer etc)
		// 4: currently offline processing and thus in user thread
		// other: not defined, but probably pre-empting user thread.
		break;
	case audioMasterGetAutomationState:
		SHOW_CALLBACK ("amc: audioMasterGetAutomationState\n");
		// returns 0: not supported, 1: off, 2:read, 3:write, 4:read/write
		// offline
		return 4;
	case audioMasterOfflineStart:
		SHOW_CALLBACK ("amc: audioMasterOfflineStart\n");
		break;
	case audioMasterOfflineRead:
		SHOW_CALLBACK ("amc: audioMasterOfflineRead\n");
		// ptr points to offline structure, see below. return 0: error, 1 ok
		break;
	case audioMasterOfflineWrite:
		SHOW_CALLBACK ("amc: audioMasterOfflineWrite\n");
		// same as read
		break;
	case audioMasterOfflineGetCurrentPass:
		SHOW_CALLBACK ("amc: audioMasterOfflineGetCurrentPass\n");
		break;
	case audioMasterOfflineGetCurrentMetaPass:
		SHOW_CALLBACK ("amc: audioMasterOfflineGetCurrentMetaPass\n");
		break;
	case audioMasterSetOutputSampleRate:
		SHOW_CALLBACK ("amc: audioMasterSetOutputSampleRate\n");
		// for variable i/o, sample rate in <opt>
		break;
	case audioMasterGetSpeakerArrangement:
		SHOW_CALLBACK ("amc: audioMasterGetSpeakerArrangement\n");
		// (long)input in <value>, output in <ptr>
		break;
	case audioMasterGetVendorString:
		SHOW_CALLBACK ("amc: audioMasterGetVendorString\n");
		// fills <ptr> with a string identifying the vendor (max 64 char)
		strncpy ( (char*) ptr, "Xj" , 64 );
		break;
	case audioMasterGetProductString:
		SHOW_CALLBACK ("amc: audioMasterGetProductString\n");
		// fills <ptr> with a string with product name (max 64 char)
		strncpy ( (char*) ptr, "FSTHost", 64);
		break;
	case audioMasterGetVendorVersion:
		SHOW_CALLBACK ("amc: audioMasterGetVendorVersion\n");
		// returns vendor-specific version
		return 1000;
	case audioMasterVendorSpecific:
		SHOW_CALLBACK ("amc: audioMasterVendorSpecific\n");
		// no definition, vendor specific handling
		break;
	case audioMasterSetIcon:
		SHOW_CALLBACK ("amc: audioMasterSetIcon\n");
		// void* in <ptr>, format not defined yet
		break;
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
		break;
	case audioMasterGetLanguage:
		SHOW_CALLBACK ("amc: audioMasterGetLanguage\n");
		// see enum
		break;
	case audioMasterOpenWindow:
		SHOW_CALLBACK ("amc: audioMasterOpenWindow\n");
		// returns platform specific ptr
		break;
	case audioMasterCloseWindow:
		SHOW_CALLBACK ("amc: audioMasterCloseWindow\n");
		// close window, platform specific handle in <ptr>
		break;
	case audioMasterGetDirectory:
		SHOW_CALLBACK ("amc: audioMasterGetDirectory\n");
		// get plug directory, FSSpec on MAC, else char*
		break;
	case audioMasterUpdateDisplay:
		SHOW_CALLBACK ("amc: audioMasterUpdateDisplay\n");
		// something has changed, update 'multi-fx' display
		if ( amc && amc->UpdateDisplay ) {
			if ( amc->UpdateDisplay ( amc ) )
				effect->dispatcher(effect, effEditIdle, 0, 0, NULL, 0.0f);
		}
		break;
	case audioMasterBeginEdit:
		SHOW_CALLBACK ("amc: audioMasterBeginEdit\n");
		// begin of automation session (when mouse down), parameter index in <index>
		break;
	case audioMasterEndEdit:
		SHOW_CALLBACK ("amc: audioMasterEndEdit\n");
		// end of automation session (when mouse up),     parameter index in <index>
		break;
	case audioMasterOpenFileSelector:
		SHOW_CALLBACK ("amc: audioMasterOpenFileSelector\n");
		// open a fileselector window with VstFileSelect* in <ptr>
		break;
	default:
		SHOW_CALLBACK ("VST master dispatcher: undefed: %d\n", opcode);
		break;
	}
	return 0;
}
