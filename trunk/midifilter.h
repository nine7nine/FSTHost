#ifndef __midifilter_h__
#define __midifilter_h__

#include <stdint.h>
#include <stdbool.h>

typedef struct _MIDIFILTER MIDIFILTER;

#define MF_STR_NOTE_OFF         "NOTE OFF"
#define MF_STR_ALL              "ALL"
#define MF_STR_NOTE_ON          "NOTE ON"
#define MF_STR_AFTERTOUCH       "AFTERTOUCH"
#define MF_STR_CONTROL_CHANGE   "CONTROL CHANGE"
#define MF_STR_PROGRAM_CHANGE   "PROGRAM CHANGE"
#define MF_STR_CHANNEL_PRESSURE "CHANNEL PRESSURE"
#define MF_STR_PITCH_BEND       "PITCH BEND"

#define MF_STR_CHANNEL_REDIRECT "CHANNEL REDIRECT"
#define MF_STR_DROP_ALL         "DROP ALL"
#define MF_STR_ACCEPT           "ACCEPT"

enum MidiMessageType {
	MM_ALL = 0,
	MM_NOTE_OFF = 0x8,
	MM_NOTE_ON = 0x9,
	MM_AFTERTOUCH = 0xA,
	MM_CONTROL_CHANGE = 0xB,
	MM_PROGRAM_CHANGE = 0xC,
	MM_CHANNEL_PRESSURE = 0xD,
	MM_PITCH_BEND = 0xE
};

struct _MIDIFILTER {
	/* General part */
	struct _MIDIFILTER *next;
	bool enabled;

	/* Match part */
	enum MidiMessageType type;
	uint8_t channel;
	uint8_t value1;
	uint8_t value2;

	/* Rule part */
	enum {
		CHANNEL_REDIRECT,
		DROP_ALL,
		ACCEPT
	} rule;
	uint8_t rvalue;
};

MIDIFILTER* midi_filter_add( MIDIFILTER **filters, MIDIFILTER *new );
bool midi_filter_check( MIDIFILTER **filters, uint8_t* data, size_t size );
void midi_filter_remove ( MIDIFILTER **filters, MIDIFILTER *toRemove );
void midi_filter_one_channel( MIDIFILTER **filters, uint8_t channel);
void midi_filter_cleanup( MIDIFILTER **filters );

#endif /* __midifilter_h__ */
