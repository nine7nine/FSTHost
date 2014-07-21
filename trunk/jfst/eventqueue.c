#include "jfst.h"

void event_queue_init ( EventQueue* eq ) {
	eq->front = eq->rear = 0;
}

static inline void eq_index_next ( uint8_t* index ) {
	(*index)++;
	if ( *index >= MAX_EVENTS )
		*index = 0;
}

void event_queue_send ( EventQueue* eq, EventType type, uint32_t value ) {
	eq_index_next( &eq->front );

	if ( eq->front == eq->rear ) {
		printf ( "Event Queue overflow !\n" );
		eq_index_next( &eq->rear );
	}
	Event* ev = &( eq->events[eq->front] );
	ev->type = type;
	ev->value = value;
}

Event* event_queue_get ( EventQueue* eq ) {
	if ( eq->front == eq->rear ) return NULL; /* Empty queue */

	eq_index_next( &eq->rear );
	return &( eq->events[eq->rear] );
}
