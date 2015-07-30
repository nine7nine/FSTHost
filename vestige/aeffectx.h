/*
 * aeffectx.h - simple header to allow VeSTige compilation and eventually work
 *
 * Copyright (c) 2006 Javier Serrano Polo <jasp00/at/users.sourceforge.net>
 * 
 * This file is part of Linux MultiMedia Studio - http://lmms.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#ifndef _AEFFECTX_H
#define _AEFFECTX_H

#include <stdint.h>
#include <windows.h>

#define VSTCALLBACK __cdecl

#define CCONST(a, b, c, d)( ( ( (int) a ) << 24 ) | ( ( (int) b ) << 16 ) | ( ( (int) c ) << 8 ) | ( ( (int) d ) << 0 ) )

#define kVstMaxProgNameLen 24
#define kVstMaxParamStrLen 8
#define kVstMaxVendorStrLen 64
#define kVstMaxProductStrLen 64
#define kVstMaxEffectNameLen 32 

#define audioMasterAutomate 0
#define audioMasterVersion 1
#define audioMasterCurrentId 2
#define audioMasterIdle 3
#define audioMasterPinConnected 4
// unsupported? 5
#define audioMasterWantMidi 6
#define audioMasterGetTime 7
#define audioMasterProcessEvents 8
#define audioMasterSetTime 9
#define audioMasterTempoAt 10
#define audioMasterGetNumAutomatableParameters 11
#define audioMasterGetParameterQuantization 12
#define audioMasterIOChanged 13
#define audioMasterNeedIdle 14
#define audioMasterSizeWindow 15
#define audioMasterGetSampleRate 16
#define audioMasterGetBlockSize 17
#define audioMasterGetInputLatency 18
#define audioMasterGetOutputLatency 19
#define audioMasterGetPreviousPlug 20
#define audioMasterGetNextPlug 21
#define audioMasterWillReplaceOrAccumulate 22
#define audioMasterGetCurrentProcessLevel 23
#define audioMasterGetAutomationState 24
#define audioMasterOfflineStart 25
#define audioMasterOfflineRead 26
#define audioMasterOfflineWrite 27
#define audioMasterOfflineGetCurrentPass 28
#define audioMasterOfflineGetCurrentMetaPass 29
#define audioMasterSetOutputSampleRate 30
// unsupported? 31
#define audioMasterGetSpeakerArrangement 31 // deprecated in 2.4?
#define audioMasterGetVendorString 32
#define audioMasterGetProductString 33
#define audioMasterGetVendorVersion 34
#define audioMasterVendorSpecific 35
#define audioMasterSetIcon 36
#define audioMasterCanDo 37
#define audioMasterGetLanguage 38
#define audioMasterOpenWindow 39
#define audioMasterCloseWindow 40
#define audioMasterGetDirectory 41
#define audioMasterUpdateDisplay 42
#define audioMasterBeginEdit 43
#define audioMasterEndEdit 44
#define audioMasterOpenFileSelector 45
#define audioMasterCloseFileSelector 46// currently unused
#define audioMasterEditFile 47// currently unused
#define audioMasterGetChunkFile 48// currently unused
#define audioMasterGetInputSpeakerArrangement 49 // currently unused

#define effFlagsHasEditor 1
#define effFlagsHasClip (1 << 1) // depracted
#define effFlagsHasVu (1 << 2) // depracted
#define effFlagsCanMono (1 << 3) // depracted
#define effFlagsCanReplacing (1 << 4)
#define effFlagsProgramChunks (1 << 5)
#define effFlagsIsSynth (1 << 8)
#define effFlagsNoSoundInStop (1 << 9)
#define effFlagsExtIsAsync (1 << 10) // depracted
#define effFlagsExtHasBuffer (1 << 11) // depracted
#define effFlagsCanDoubleReplacing (1 << 12) // v2.4 only

#define effOpen 0
#define effClose 1
#define effSetProgram 2
#define effGetProgram 3
#define effSetProgramName 4
#define effGetProgramName 5
#define effGetParamLabel 6
#define effGetParamDisplay 7
#define effGetParamName 8
// this is a guess
#define effSetSampleRate 10
#define effSetBlockSize 11
#define effMainsChanged 12
#define effEditGetRect 13
#define effEditOpen 14
#define effEditClose 15
#define effEditIdle 19
#define effEditTop 20
#define effGetChunk 23
#define effSetChunk 24
#define effProcessEvents 25
#define effGetProgramNameIndexed 29
#define effGetInputProperties 33
#define effGetOutputProperties 34
#define effGetEffectName 45
// missing
#define effGetParameterProperties 47
#define effGetVendorString 47
#define effGetProductString 48
#define effGetVendorVersion 49
#define effCanDo 51
#define effIdle 53
#define effGetVstVersion 58
#define effGetVstVersion 58
#define effBeginSetProgram 67
#define effEndSetProgram 68
#define effStartProcess 71
#define effStopProcess 72
#define effBeginLoadBank 75
#define effBeginLoadProgram 76

#define kEffectMagic (CCONST( 'V', 's', 't', 'P' ))
#define kVstLangEnglish 1
// MIDI Event Types
#define kVstMidiType 1		// Standard MIDI event
#define kVstAudioType 2		// Depracted Audio event
#define kVstVideoType 3		// Depracted Video event  
#define kVstParameterType 4	// Depracted Parameter event
#define kVstTriggerType 5	// Depracted Trigger event
#define kVstSysExType 6		// SystemExclusive event
#define kVstMidiEventIsRealtime 1
#define kVstTransportPlaying (1 << 1)
#define kVstTransportChanged 1

//struct RemoteVstPlugin;

#define kVstNanosValid (1 << 8)
#define kVstPpqPosValid (1 << 9)
#define kVstTempoValid (1 << 10)
#define kVstBarsValid (1 << 11)
#define kVstCyclePosValid (1 << 12)
#define kVstTimeSigValid (1 << 13)
#define kVstSmpteValid (1 << 14)
#define kVstClockValid (1 << 15)

#define kVstPinIsActive ( 1 << 0 )
#define kVstPinIsStereo ( 1 << 1 )
#define kVstPinUseSpeaker ( 1 << 2 )

typedef struct VstMidiEvent {
	int32_t type;
	int32_t byteSize;
	int32_t deltaFrames;
	int32_t flags;
	int32_t noteLength;
	int32_t noteOffset;
	char midiData[4];
	char detune;
	char noteOffVelocity;
	char reserved1;
	char reserved2;
} VstMidiEvent;

typedef void* VstEvent; // This only general cast for other structs

typedef struct VstEvents {
	int32_t numEvents;
	intptr_t reserved;
	VstEvent* events[2];
} VstEvents;

typedef struct VstParameterProperties {
	float stepFloat;
	float smallStepFloat;
	float largeStepFloat;
	char label[64];
	int32_t flags;
	int32_t minInteger;
	int32_t maxInteger;
	int32_t stepInteger;
	int32_t largeStepInteger;
	char shortLabel[8];
} VstParameterProperties;

enum VstParameterFlags {
	kVstParameterIsSwitch                = 1 << 0, /* parameter is a switch (on/off) */
	kVstParameterUsesIntegerMinMax       = 1 << 1, /* minInteger, maxInteger valid */
	kVstParameterUsesFloatStep           = 1 << 2, /* stepFloat, smallStepFloat, largeStepFloat valid */
	kVstParameterUsesIntStep             = 1 << 3, /* stepInteger, largeStepInteger valid */
	kVstParameterSupportsDisplayIndex    = 1 << 4, /* displayIndex valid */
	kVstParameterSupportsDisplayCategory = 1 << 5, /* category, etc. valid */
	kVstParameterCanRamp                 = 1 << 6  /* set if parameter value can ramp up/down */
};

typedef struct VstTimeInfo {
	double samplePos;
	double sampleRate;
	double nanoSeconds;
	double ppqPos;
	double tempo;
	double barStartPos;
	double cycleStartPos;
	double cycleEndPos;
	int32_t timeSigNumerator;
	int32_t timeSigDenominator;
	int32_t smpteOffset;
	int32_t smpteFrameRate;
	int32_t samplesToNextClock;
	int32_t flags;

} VstTimeInfo;

typedef struct AEffect { // Never use c++!!!
	int32_t magic;
	intptr_t (VSTCALLBACK *dispatcher) (struct AEffect *, int32_t, int32_t, intptr_t, void *, float);
	void (VSTCALLBACK *process)( struct AEffect * , float **, float **, int32_t );
	void (VSTCALLBACK *setParameter)( struct AEffect * , int32_t, float );
	float (VSTCALLBACK *getParameter)( struct AEffect * , int32_t );
	int32_t numPrograms;
	int32_t numParams;
	int32_t numInputs;
	int32_t numOutputs;
	int32_t flags;
	intptr_t *resvd1;
	intptr_t *resvd2;
	int32_t empty3[3];
	float ioRatio;
	void *ptr3;
	void *user;
	int32_t uniqueID;
	int32_t version;
	void (VSTCALLBACK *processReplacing)( struct AEffect* , float**, float**, int32_t );
	void (VSTCALLBACK *processDoubleReplacing)( struct AEffect*, double**, double**, int32_t );
	char future[56];
} AEffect;

typedef struct VstPinProperties {
	char label[64];
	int32_t flags;
	int32_t arrangementType;
	char shortLabel[8]; // This is broken in most plugins
	char future[48];
} VstPinProperties;

typedef struct VstPatchChunkInfo {
	int32_t version;        // Format Version (should be 1)
	int32_t pluginUniqueID; // UniqueID of the plug-in
	int32_t pluginVersion;  // Plug-in Version
	int32_t numElements;    // Number of Programs (Bank) or Parameters (Program)
	char future[48];        // Reserved for future use
} VstPatchChunkInfo;

struct ERect {
	int16_t top;
	int16_t left;
	int16_t bottom;
	int16_t right;
};

typedef intptr_t (VSTCALLBACK *audioMasterCallback) ( AEffect *, int32_t, int32_t, intptr_t, void *, float );

#endif
