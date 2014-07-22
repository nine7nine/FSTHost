#include "jfst.h"

void event_queue_init ( EventQueue* eq ) {
	eq->front = eq->rear = 0;
}

static inline void eq_index_next ( uint8_t* index ) {
	(*index)++;
	if ( *index >= MAX_EVENTS )
		*index = 0;
}

static inline void event_queue_send ( EventQueue* eq, Event* ev ) {
	eq_index_next( &eq->front );

	if ( eq->front == eq->rear ) {
		printf ( "Event Queue overflow !\n" );
		eq_index_next( &eq->rear );
	}

	eq->events[eq->front] = *ev;
//	printf( "Send Event: %d Value: %d\n", type, value );
}

void event_queue_send_val ( EventQueue* eq, EventType type, uint32_t value ) {
	Event ev;
	ev.type = type;
	ev.value = value;
	event_queue_send ( eq, &ev );
}

void event_queue_send_ptr ( EventQueue* eq, EventType type, void* ptr ) {
	Event ev;
	ev.type = type;
	ev.ptr = ptr;
	event_queue_send ( eq, &ev );
}

Event* event_queue_get ( EventQueue* eq ) {
	if ( eq->front == eq->rear ) return NULL; /* Empty queue */

	eq_index_next( &eq->rear );
	return &( eq->events[eq->rear] );
}
