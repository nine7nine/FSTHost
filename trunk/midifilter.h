#ifndef __midifilter_h__
#define __midifilter_h__

#include <stdint.h>
#include <stdbool.h>

#define MF_STR_ALL              "ALL"
#define MF_STR_NOTE             "NOTE"
#define MF_STR_NOTE_OFF         "NOTE OFF"
#define MF_STR_NOTE_ON          "NOTE ON"
#define MF_STR_AFTERTOUCH       "AFTERTOUCH"
#define MF_STR_CONTROL_CHANGE   "CONTROL CHANGE"
#define MF_STR_PROGRAM_CHANGE   "PROGRAM CHANGE"
#define MF_STR_CHANNEL_PRESSURE "CHANNEL PRESSURE"
#define MF_STR_PITCH_BEND       "PITCH BEND"

#define MF_STR_CHANNEL_REDIRECT "CHANNEL REDIRECT"
#define MF_STR_TRANSPOSE        "TRANSPOSE"
#define MF_STR_DROP_ALL         "DROP ALL"
#define MF_STR_ACCEPT           "ACCEPT"

enum MidiMessageType {
	MM_ALL = 0,
	MM_NOTE = 1, /* Fake message type for agregate note on/off */
	MM_NOTE_OFF = 0x8,
	MM_NOTE_ON = 0x9,
	MM_AFTERTOUCH = 0xA,
	MM_CONTROL_CHANGE = 0xB,
	MM_PROGRAM_CHANGE = 0xC,
	MM_CHANNEL_PRESSURE = 0xD,
	MM_PITCH_BEND = 0xE
};

typedef struct _MIDIFILTER {
	/* General part */
	struct _MIDIFILTER *next;
	bool enabled;
	bool built_in;

	/* Match part */
	enum MidiMessageType type;
	uint8_t channel;
	uint8_t value1;
	uint8_t value2;

	/* Rule part */
	enum {
		CHANNEL_REDIRECT,
		TRANSPOSE,
		DROP_ALL,
		ACCEPT
	} rule;
	int8_t rvalue;
} MIDIFILTER;

typedef struct _OCH_FILTERS {
	MIDIFILTER* drop_real_one;
	MIDIFILTER* redirect;
	MIDIFILTER* accept;
	MIDIFILTER* drop_rest;
} OCH_FILTERS;

MIDIFILTER* midi_filter_add( MIDIFILTER **filters, MIDIFILTER *new );
bool midi_filter_check( MIDIFILTER **filters, uint8_t* data, size_t size );
void midi_filter_remove ( MIDIFILTER **filters, MIDIFILTER *toRemove );
void midi_filter_one_channel_init ( MIDIFILTER **filters, OCH_FILTERS* );
void midi_filter_one_channel_set ( OCH_FILTERS* ochf, uint8_t channel );
MIDIFILTER* midi_filter_transposition_init ( MIDIFILTER** filters );
void midi_filter_transposition_set ( MIDIFILTER* t, int8_t value );
int8_t midi_filter_transposition_get ( MIDIFILTER* t );

uint8_t midi_filter_one_channel_get ( OCH_FILTERS* ochf );
void midi_filter_cleanup( MIDIFILTER **filters, bool BuiltIn );

#endif /* __midifilter_h__ */
