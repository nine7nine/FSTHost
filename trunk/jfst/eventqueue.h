#ifndef __eventqueue_h__
#define __eventqueue_h__

#define MAX_EVENTS 5

typedef enum {
	JFST_EVENT_PC,
	JFST_EVENT_STATE
} EventType;

typedef struct {
	EventType	type;
	uint32_t	value;
} Event;

typedef struct {
	Event		events[MAX_EVENTS];
	uint8_t		front;
	uint8_t		rear;
} EventQueue;

void event_queue_init ( EventQueue* eq );
void event_queue_send ( EventQueue* eq, EventType type, uint32_t value );
Event* event_queue_get ( EventQueue* eq );

#endif /* __eventqueue_h__ */
