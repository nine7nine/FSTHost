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

#include "log/log.h"
#include "amc.h"

/* The following audioMaster opcode handlings are copied
 * (with a light modification) from the vst tilde source
 * made by Mark Williamson.
*/

#define DEBUG log_debug

static void show_time_flags(intptr_t value) {
	char msg[512] = "";
	if (value & kVstNanosValid) strcat(msg, " kVstNanosValid");
	if (value & kVstPpqPosValid) strcat(msg, " kVstPpqPosValid");
	if (value & kVstTempoValid) strcat(msg, " kVstTempoValid");
	if (value & kVstBarsValid) strcat(msg, " kVstBarsValid");
	if (value & kVstCyclePosValid) strcat(msg, " kVstCyclePosVali");
	if (value & kVstTimeSigValid) strcat(msg, " kVstTimeSigValid");
	if (value & kVstSmpteValid) strcat(msg, " kVstSmpteValid");
	if (value & kVstClockValid) strcat(msg, " kVstClockValid");
	msg[511] = '\0';
	DEBUG("amc time:%s", msg);
}

// most simple one :) could be sufficient.... 
intptr_t VSTCALLBACK
amc_simple_callback ( AEffect *effect, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt )
{
	AMC* amc = effect ? ((AMC*) effect->resvd1) : NULL;

	if ( opcode == audioMasterVersion ) return 2;

	if ( opcode == audioMasterGetTime && amc )
		return (intptr_t) &amc->timeInfo;

	return 0;
}

intptr_t VSTCALLBACK
amc_callback ( AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt )
{
	AMC* amc = effect ? ((AMC*) effect->resvd1) : NULL;

	//DEBUG ("am callback, opcode = %d", opcode);
	
	switch ( opcode ) {
	case audioMasterAutomate:
		DEBUG ("amc: audioMasterAutomate\n");
		// index, value, returns 0
		if ( amc && amc->Automate ) amc->Automate ( amc, index );
		break;
	case audioMasterVersion:
		DEBUG ("amc: audioMasterVersion\n");
		// vst version, currently 2 (0 for older)
		return 2;
	case audioMasterCurrentId:	
		DEBUG ("amc: audioMasterCurrentId\n");
		// returns the unique id of a plug that's currently
		// loading
		break;
	case audioMasterIdle:
		DEBUG ("amc: audioMasterIdle\n");
		// call application idle routine (this will
		// call effEditIdle for all open editors too) 
		effect->dispatcher(effect, effEditIdle, 0, 0, NULL, 0.0f);
		break;
	case audioMasterPinConnected:		
		DEBUG ("amc: audioMasterPinConnected\n");
		// inquire if an input or output is beeing connected;
		// index enumerates input or output counting from zero:
		// value is 0 for input and != 0 otherwise. note: the
		// return value is 0 for <true> such that older versions
		// will always return true.
		break;
	case audioMasterWantMidi:
		DEBUG ("amc: audioMasterWantMidi\n");
		// <value> is a filter which is currently ignored
		break;
	case audioMasterGetTime:
		DEBUG ("amc: audioMasterGetTime\n");
		show_time_flags(value);
		// returns const VstTimeInfo* (or 0 if not supported)
		// <value> should contain a mask indicating which fields are required
		// (see valid masks above), as some items may require extensive
		// conversions
		if ( ! amc ) break;
		
		if (amc->GetTime) amc->GetTime(amc,value);
		return (intptr_t) &amc->timeInfo;
	case audioMasterProcessEvents:
		DEBUG ("amc: audioMasterProcessEvents\n");
		// VstEvents* in <ptr>
		if ( amc && amc->ProcessEvents ) return amc->ProcessEvents( amc, (VstEvents*) ptr );
		break;
	case audioMasterSetTime:
		DEBUG ("amc: audioMasterSetTime\n");
		// VstTimenfo* in <ptr>, filter in <value>, not supported
		break;
	case audioMasterTempoAt:
		DEBUG ("amc: audioMasterTempoAt\n");
		// returns tempo (in bpm * 10000) at sample frame location passed in <value>
		intptr_t ret = 120;
		if ( amc && amc->TempoAt ) ret = amc->TempoAt( amc, value );
		return ret * 10000;
	case audioMasterGetNumAutomatableParameters:
		DEBUG ("amc: audioMasterGetNumAutomatableParameters\n");
		break;
	case audioMasterGetParameterQuantization:	
		DEBUG ("amc: audioMasterGetParameterQuantization\n");
		// returns the integer value for +1.0 representation,
		// or 1 if full single float precision is maintained
		// in automation. parameter index in <value> (-1: all, any)
		break;
	case audioMasterIOChanged:
		DEBUG ("amc: audioMasterIOChanged\n");
		// numInputs and/or numOutputs has changed
		break;
	case audioMasterNeedIdle:
		DEBUG ("amc: audioMasterNeedIdle\n");
		// plug needs idle calls (outside its editor window)
		if ( amc ) {
			amc->need_idle = true;
			if (amc->NeedIdle) amc->NeedIdle(amc);
		}
		return 1;
	case audioMasterSizeWindow:
		DEBUG ("amc: audioMasterSizeWindow %d %d\n", index, value);
		// index: width, value: height
		if (amc && amc->SizeWindow) {
			amc->SizeWindow( amc, index, value );
			return 1; /* Say : support it */
		}
		break;
	case audioMasterGetSampleRate:
		DEBUG ("amc: audioMasterGetSampleRate\n");
		if ( amc && amc->sample_rate )
			return amc->sample_rate;
		return 44100;
	case audioMasterGetBlockSize:
		DEBUG ("amc: audioMasterGetBlockSize\n");
		if ( amc && amc->block_size )
			return amc->block_size;
		return 1024;
	case audioMasterGetInputLatency:
		DEBUG ("amc: audioMasterGetInputLatency\n");
		break;
	case audioMasterGetOutputLatency:
		DEBUG ("amc: audioMasterGetOutputLatency\n");
		break;
	case audioMasterGetPreviousPlug:
		DEBUG ("amc: audioMasterGetPreviousPlug\n");
		// input pin in <value> (-1: first to come), returns cEffect*
		break;
	case audioMasterGetNextPlug:
		DEBUG ("amc: audioMasterGetNextPlug\n");
		// output pin in <value> (-1: first to come), returns cEffect*
		break;
	case audioMasterWillReplaceOrAccumulate:
		DEBUG ("amc: audioMasterWillReplaceOrAccumulate\n");
		// returns: 0: not supported, 1: replace, 2: accumulate
		break;
	case audioMasterGetCurrentProcessLevel:
		DEBUG ("amc: audioMasterGetCurrentProcessLevel\n");
		// returns: 0: not supported,
		// 1: currently in user thread (gui)
		// 2: currently in audio thread (where process is called)
		// 3: currently in 'sequencer' thread (midi, timer etc)
		// 4: currently offline processing and thus in user thread
		// other: not defined, but probably pre-empting user thread.
		break;
	case audioMasterGetAutomationState:
		DEBUG ("amc: audioMasterGetAutomationState\n");
		// returns 0: not supported, 1: off, 2:read, 3:write, 4:read/write
		// offline
		return 4;
	case audioMasterOfflineStart:
		DEBUG ("amc: audioMasterOfflineStart\n");
		break;
	case audioMasterOfflineRead:
		DEBUG ("amc: audioMasterOfflineRead\n");
		// ptr points to offline structure, see below. return 0: error, 1 ok
		break;
	case audioMasterOfflineWrite:
		DEBUG ("amc: audioMasterOfflineWrite\n");
		// same as read
		break;
	case audioMasterOfflineGetCurrentPass:
		DEBUG ("amc: audioMasterOfflineGetCurrentPass\n");
		break;
	case audioMasterOfflineGetCurrentMetaPass:
		DEBUG ("amc: audioMasterOfflineGetCurrentMetaPass\n");
		break;
	case audioMasterSetOutputSampleRate:
		DEBUG ("amc: audioMasterSetOutputSampleRate\n");
		// for variable i/o, sample rate in <opt>
		break;
	case audioMasterGetSpeakerArrangement:
		DEBUG ("amc: audioMasterGetSpeakerArrangement\n");
		// (long)input in <value>, output in <ptr>
		break;
	case audioMasterGetVendorString:
		DEBUG ("amc: audioMasterGetVendorString\n");
		// fills <ptr> with a string identifying the vendor (max 64 char)
		strncpy ( (char*) ptr, "Xj" , 64 );
		break;
	case audioMasterGetProductString:
		DEBUG ("amc: audioMasterGetProductString\n");
		// fills <ptr> with a string with product name (max 64 char)
		strncpy ( (char*) ptr, "FSTHost", 64);
		break;
	case audioMasterGetVendorVersion:
		DEBUG ("amc: audioMasterGetVendorVersion\n");
		// returns vendor-specific version
		return 1000;
	case audioMasterVendorSpecific:
		DEBUG ("amc: audioMasterVendorSpecific\n");
		// no definition, vendor specific handling
		break;
	case audioMasterSetIcon:
		DEBUG ("amc: audioMasterSetIcon\n");
		// void* in <ptr>, format not defined yet
		break;
	case audioMasterCanDo:
		DEBUG ("amc: audioMasterCanDo %s\n", (char*)ptr);
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
		DEBUG ("amc: audioMasterGetLanguage\n");
		// see enum
		break;
	case audioMasterOpenWindow:
		DEBUG ("amc: audioMasterOpenWindow\n");
		// returns platform specific ptr
		break;
	case audioMasterCloseWindow:
		DEBUG ("amc: audioMasterCloseWindow\n");
		// close window, platform specific handle in <ptr>
		break;
	case audioMasterGetDirectory:
		DEBUG ("amc: audioMasterGetDirectory\n");
		// get plug directory, FSSpec on MAC, else char*
		break;
	case audioMasterUpdateDisplay:
		DEBUG ("amc: audioMasterUpdateDisplay\n");
		// something has changed, update 'multi-fx' display
		if ( amc && amc->UpdateDisplay ) {
			if ( amc->UpdateDisplay ( amc ) )
				effect->dispatcher(effect, effEditIdle, 0, 0, NULL, 0.0f);
		}
		break;
	case audioMasterBeginEdit:
		DEBUG ("amc: audioMasterBeginEdit\n");
		// begin of automation session (when mouse down), parameter index in <index>
		break;
	case audioMasterEndEdit:
		DEBUG ("amc: audioMasterEndEdit\n");
		// end of automation session (when mouse up),     parameter index in <index>
		break;
	case audioMasterOpenFileSelector:
		DEBUG ("amc: audioMasterOpenFileSelector\n");
		// open a fileselector window with VstFileSelect* in <ptr>
		break;
	default:
		DEBUG ("VST master dispatcher: undefed: %d\n", opcode);
		break;
	}
	return 0;
}
