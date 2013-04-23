#include <stdlib.h>
#include <stdio.h>
#include "midifilter.h"

//#define MF_DEBUG_ENABLED

#ifdef MF_DEBUG_ENABLED
#define MF_DEBUG printf
#else
#define MF_DEBUG(...)
#endif


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
		MF_DEBUG("FilterRemove: Filter is built_in %p\n", *toRemove);
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
		if (BuiltIn || f->built_in) {
			if (prev) {
				prev->next = next;
			} else {
				/* No previous element, so it was first, mean move pointer to next */
				*filters = next;
			}
			free(f);
		} else {
			/* Skip element this */
			prev = f;
		}
		f = next;
	}
}

bool midi_filter_check( MIDIFILTER **filters, uint8_t* data, size_t size ) {
	uint8_t type, channel; 
	bool ret = true;

	MF_DEBUG("DATA: MSG_TYPE: %X, CH: %X\n", type, channel);

	MIDIFILTER *f;
	for (f = *filters; f; f = f->next) {
		/* ... here because last filter would change data */
		type = (data[0] >> 4) & 0xF;
		channel = ( data[0] & 0xF ) + 1;

		MF_DEBUG("FILTER: ENABLED: %X, TYPE: %X, CH: %X, RULE_TYPE: %X\n", f->enabled, f->type, f->channel, f->rule);
		if (	( ! f->enabled ) ||
			! ( f->type && f->type != type ) ||
			! ( f->channel && f->channel != channel )
//		     ! (f->value1 && size > 2 && f->value1 != data[1]) ||
//		     ! (f->value2 && size > 3 && f->value2 != data[2])
		) continue;

		switch(f->rule) {
		case CHANNEL_REDIRECT:
			if (f->channel != channel) {
				MF_DEBUG("RedirectToChannel %X\n", f->rvalue);
				data[0] &= 0xF0;
				data[0] |= ( (f->rvalue - 1) & 0xF);
			}
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

