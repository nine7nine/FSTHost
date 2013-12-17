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

typedef struct VstMidiEvent
{
	// 00
	int32_t type;
	// 04
	int32_t byteSize;
	// 08
	int32_t deltaFrames;
	// 0c?
	int32_t flags;
	// 10?
	int32_t noteLength;
	// 14?
	int32_t noteOffset;
	// 18
	char midiData[4];
	// 1c?
	char detune;
	// 1d?
	char noteOffVelocity;
	// 1e?
	char reserved1;
	// 1f?
	char reserved2;
} VstMidiEvent;

typedef struct VstEvent
{
	char dump[sizeof( VstMidiEvent )];

} VstEvent;

typedef struct VstEvents
{
	// 00
	int32_t numEvents;
	// 04
	intptr_t reserved;
	// 08
	VstEvent* events[2];
} VstEvents;

typedef struct VstParameterProperties
{
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

enum VstParameterFlags
{
	kVstParameterIsSwitch                = 1 << 0, /* parameter is a switch (on/off) */
	kVstParameterUsesIntegerMinMax       = 1 << 1, /* minInteger, maxInteger valid */
	kVstParameterUsesFloatStep           = 1 << 2, /* stepFloat, smallStepFloat, largeStepFloat valid */
	kVstParameterUsesIntStep             = 1 << 3, /* stepInteger, largeStepInteger valid */
	kVstParameterSupportsDisplayIndex    = 1 << 4, /* displayIndex valid */
	kVstParameterSupportsDisplayCategory = 1 << 5, /* category, etc. valid */
	kVstParameterCanRamp                 = 1 << 6  /* set if parameter value can ramp up/down */
};

typedef struct VstTimeInfo
{
	// 00
	double samplePos;
	// 08
	double sampleRate;
	// 10 - 18
	double nanoSeconds;
	double ppqPos;
	// 20
	double tempo;
	// 28, 30, 38
	double barStartPos;
	double cycleStartPos;
	double cycleEndPos;
	// 40
	int32_t timeSigNumerator;
	// 44
	int32_t timeSigDenominator;
	// 48, 4c, 50
	int32_t smpteOffset;
	int32_t smpteFrameRate;
	int32_t samplesToNextClock;
	// 54
	int32_t flags;

} VstTimeInfo;

typedef struct AEffect {
	// Never use c++!!!
	// 00-03
	int32_t magic;
	// dispatcher 04-07
	intptr_t (VSTCALLBACK *dispatcher) (struct AEffect *, int32_t, int32_t, intptr_t, void *, float);
	// process, quite sure 08-0b
	void (VSTCALLBACK *process)( struct AEffect * , float **, float **, int32_t );
	// setParameter 0c-0f
	void (VSTCALLBACK *setParameter)( struct AEffect * , int32_t, float );
	// getParameter 10-13
	float (VSTCALLBACK *getParameter)( struct AEffect * , int32_t );
	// programs 14-17
	int32_t numPrograms;
	// Params 18-1b
	int32_t numParams;
	// Input 1c-1f
	int32_t numInputs;
	// Output 20-23
	int32_t numOutputs;
	// flags 24-27
	int32_t flags;
	// Fill somewhere 28-2b
	intptr_t *resvd1;
	intptr_t *resvd2;
	// Zeroes 2c-2f 30-33 34-37 38-3b
	int32_t empty3[3];
	// 1.0f 3c-3f
	float ioRatio;
	// An object? pointer 40-43
	void *ptr3;
	// Zeroes 44-47
	void *user;
	// Id 48-4b
	int32_t uniqueID;
	// version 4c-4f
	int32_t version;
	// processReplacing 50-53
	void (VSTCALLBACK *processReplacing)( struct AEffect* , float**, float**, int32_t );
	// processReplacing 54-57
	void (VSTCALLBACK *processDoubleReplacing)( struct AEffect*, double**, double**, int32_t );
	// future ?
	char future[56];
} AEffect;

typedef struct VstPatchChunkInfo
{
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
