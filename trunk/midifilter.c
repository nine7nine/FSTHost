#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "midifilter.h"

#define MF_DEBUG_ENABLED

#ifdef MF_DEBUG_ENABLED
#define MF_DEBUG printf
#else
#define MF_DEBUG(...)
#endif

struct MIDI_MAP {
	short key;
	const char* name;
};

#define MIDI_STRING_MAP_LENGTH 13
static struct MIDI_MAP midi_string_map[MIDI_STRING_MAP_LENGTH] = {
	{ MM_ALL, "ALL" },
	{ MM_NOTE, "NOTE" },
	{ MM_NOTE_OFF, "NOTE OFF" },
	{ MM_NOTE_ON, "NOTE ON" },
	{ MM_AFTERTOUCH, "AFTERTOUCH" },
	{ MM_CONTROL_CHANGE, "CONTROL CHANGE" },
	{ MM_PROGRAM_CHANGE, "PROGRAM CHANGE" },
	{ MM_CHANNEL_PRESSURE, "CHANNEL PRESSURE" },
	{ MM_PITCH_BEND, "PITCH BEND" },
	{ CHANNEL_REDIRECT, "CHANNEL REDIRECT" },
	{ TRANSPOSE, "TRANSPOSE" },
	{ DROP_ALL, "DROP ALL" },
	{ ACCEPT, "ACCEPT" }
};

const char* midi_filter_key2name ( int key ) {
	short i;
	for (i=0; i < MIDI_STRING_MAP_LENGTH; i++)
		if ( key == midi_string_map[i].key )
			return midi_string_map[i].name;
	return NULL;
}

int midi_filter_name2key ( const char* name ) {
	short i;
	for (i=0; i < MIDI_STRING_MAP_LENGTH; i++)
		if ( strcmp(name, midi_string_map[i].name) == 0 )
			 return midi_string_map[i].key;
	return -1;
}

MIDIFILTER* midi_filter_add( MIDIFILTER **filters, MIDIFILTER *new ) {
	MIDIFILTER *f = *filters;
	MIDIFILTER *n =  malloc( sizeof(MIDIFILTER) );
	*n = *new;

	if (f) {
		while (f->next) f = f->next;
		f->next = n;
	} else {
		*filters = n;
	}
	return n;
}

void midi_filter_remove ( MIDIFILTER **filters, MIDIFILTER *toRemove ) {
	if (toRemove->built_in) {
		MF_DEBUG("FilterRemove: Filter is built_in %p\n", toRemove);
		return;
	}

	MIDIFILTER *f, *prev;
	MF_DEBUG("F0: %p\n", *filters);
	for (f = *filters, prev = NULL; f; prev = f, f = f->next) {
		MF_DEBUG("F: %p\n", f);
		if ( f == toRemove ) {
			if (prev) {
				prev->next = f->next;
			} else {
				*filters = f->next;
			}
			free(f);
			return;
		}
	}
	MF_DEBUG("FilterRemove: can't find %p\n", toRemove);
}

void midi_filter_cleanup( MIDIFILTER **filters, bool BuiltIn ) {
	MIDIFILTER *f = *filters;
	MIDIFILTER *prev = NULL;
	MIDIFILTER *next = NULL;
	while ( f ) {
		next = f->next;
		/* Are we remove this element ? */
		if (BuiltIn && f->built_in) {
			if (prev) {
				prev->next = next;
			} else {
				/* No previous element, so it was first, mean move pointer to next */
				*filters = next;
			}
			free(f);
		} else {
			/* Skip this element */
			prev = f;
		}
		f = next;
	}
}

bool midi_filter_check( MIDIFILTER **filters, uint8_t* data, size_t size ) {
	uint8_t type, channel; 
	bool ret = true;

	MIDIFILTER *f;
	for (f = *filters; f; f = f->next) {
		/* ... here because last filter would change data */
		type = (data[0] >> 4) & 0xF;
		channel = ( data[0] & 0xF ) + 1;

//		MF_DEBUG("DATA: MSG_TYPE: %X, CH: %X\n", type, channel);
		MF_DEBUG("FILTER: ENABLED: %X, TYPE: %X, CH: %X, RULE_TYPE: %X\n", f->enabled, f->type, f->channel, f->rule);
		if ( ! f->enabled ||
		     ( f->channel && f->channel != channel )
//		     (f->value1 && size > 2 && f->value1 != data[1]) ||
//		     (f->value2 && size > 3 && f->value2 != data[2])
		) continue;

		if (f->type) {
			if ( f->type == MM_NOTE ) {
				if (type != MM_NOTE_ON && type != MM_NOTE_OFF) continue;
			} else if ( f->type != type ) {
				continue;
			}
		}

		switch(f->rule) {
		case CHANNEL_REDIRECT:
			if (f->channel != channel) {
				MF_DEBUG("RedirectToChannel %X\n", f->rvalue);
				data[0] &= 0xF0;
				data[0] |= ( (f->rvalue - 1) & 0xF);
			}
			break;
		case TRANSPOSE:
			MF_DEBUG("Transposigion %d\n", f->rvalue);
			if ( (data[1] + f->rvalue > 0) && (data[1] + f->rvalue < 128) )
				data[1] += f->rvalue;
			break;
		case DROP_ALL:
			MF_DEBUG("FilterOut\n");
			return false;
		case ACCEPT:
			MF_DEBUG("Accept\n");
			return true;
		}
	}
	return ret;
}

void midi_filter_one_channel_init ( MIDIFILTER **filters, OCH_FILTERS* ochf ) {
	MIDIFILTER filter = {0};
	filter.enabled = false;
	filter.built_in = true;

	/* Filter out real channel 1 */
	filter.channel = 1;
	filter.rule = DROP_ALL;
	ochf->drop_real_one = midi_filter_add( filters, &filter );

	/* Redirect selected channel to 1 */
	filter.rule = CHANNEL_REDIRECT;
	filter.channel = 0;
	filter.rvalue = 1;
	ochf->redirect = midi_filter_add( filters, &filter );

	/* Accept channel 1 */
	filter.channel = 1;
	filter.rule = ACCEPT;
	ochf->accept = midi_filter_add( filters, &filter );

	/* Drop rest */
	filter.channel = 0;
	filter.rule = DROP_ALL;
	ochf->drop_rest = midi_filter_add( filters, &filter );
}

/* Shortcut for our old ComoboBox .. and example how to use filters */
void midi_filter_one_channel_set ( OCH_FILTERS* ochf, uint8_t channel ) {
	if (channel > 17) {
		MF_DEBUG("OneChannel: value out of range %d\n", channel);
		channel = 17;
	}

	switch (channel) {
	case 0:
		ochf->drop_real_one->enabled	= false;
		ochf->redirect->enabled		= false;
		ochf->accept->enabled		= false;
		ochf->drop_rest->enabled	= false;
		break;
	case 1:
		ochf->drop_real_one->enabled	= false;
		ochf->redirect->enabled		= false;
		ochf->accept->enabled		= true;
		ochf->drop_rest->enabled	= true;
		break;
	case 17: /* None */
		ochf->drop_real_one->enabled	= false;
		ochf->redirect->enabled		= false;
		ochf->accept->enabled		= false;
		ochf->drop_rest->enabled	= true;
		break;
	default:
		ochf->drop_real_one->enabled	= true;
		ochf->redirect->enabled		= true;
		ochf->accept->enabled		= true;
		ochf->drop_rest->enabled	= true;
	}
	ochf->redirect->channel = channel;
}

uint8_t midi_filter_one_channel_get ( OCH_FILTERS* ochf ) {
	return ochf->redirect->channel;
}

MIDIFILTER* midi_filter_transposition_init ( MIDIFILTER** filters ) {
	MIDIFILTER filter = {0};
	filter.enabled = false;
	filter.built_in = true;
	filter.type = MM_NOTE;
	filter.rule = TRANSPOSE;
	return midi_filter_add ( filters, &filter );
}

void midi_filter_transposition_set ( MIDIFILTER* t, int8_t value ) {
	t->enabled = (value == 0) ? false : true;
	t->rvalue = value;
}

int8_t midi_filter_transposition_get ( MIDIFILTER* t ) {
	return t->rvalue;
}

