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

void midi_filter_cleanup( MIDIFILTER **filters ) {
	MIDIFILTER *f, *prev;
	for (f = *filters, prev = NULL; f; prev = f, f = f->next) {
		if (prev) free(prev);
	}
	if (f) free(f);
	*filters = NULL;
}

bool midi_filter_check( MIDIFILTER **filters, uint8_t* data, size_t size ) {
	uint8_t type, channel; 
	bool ret = true;

	MF_DEBUG("DATA: MSG_TYPE: %X, CH: %X\n", type, channel);

	MIDIFILTER *f;
	for (f = *filters; f; f = f->next) {
		/* ... here because last filter would change data */
		type = (data[0] >> 4) & 0xF;
		channel = data[0] & 0xF;

		MF_DEBUG("FILTER: ENABLED: %X, TYPE: %X, CH: %X, RULE_TYPE: %X\n", f->enabled, f->type, f->channel, f->rule);
		if ( ! f->enabled ||
		     ( f->type && f->type != type ) ||
		     ( f->channel && f->channel != channel + 1 )
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

/* Shortcut for our old ComoboBox .. and example how to use filters */
void midi_filter_one_channel( MIDIFILTER **filters, uint8_t channel) {
	midi_filter_cleanup( filters );
        if (! channel) return;

        MIDIFILTER filter = {0};
        filter.enabled = true;

        if (channel != 1) {
                /* Filter out real channel 1 */
                filter.channel = 1;
                filter.rule = DROP_ALL;
                midi_filter_add( filters, &filter );

                /* Redirect selected channel to 1 */
                filter.channel = channel;
                filter.rule = CHANNEL_REDIRECT;
                filter.rvalue = 1;
                midi_filter_add( filters, &filter );
        }

        /* Accept channel 1 */
        filter.channel = 1;
        filter.rule = ACCEPT;
        midi_filter_add( filters, &filter );

        /* Drop rest */
        filter.channel = 0;
        filter.rule = DROP_ALL;
        midi_filter_add( filters, &filter );
}
