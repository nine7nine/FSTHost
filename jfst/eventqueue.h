#ifndef __eventqueue_h__
#define __eventqueue_h__

#define MAX_EVENTS 16

typedef enum {
	EVENT_PC,
	EVENT_STATE,
	EVENT_GRAPH,
	EVENT_SESSION
} EventType;

typedef struct {
	EventType	type;
	union {
		uint32_t	value;
		void*		ptr;
	};
} Event;

typedef struct {
	Event		events[MAX_EVENTS];
	uint8_t		front;
	uint8_t		rear;
} EventQueue;

void event_queue_init ( EventQueue* eq );
void event_queue_send_val ( EventQueue* eq, EventType type, uint32_t value );
void event_queue_send_ptr ( EventQueue* eq, EventType type, void* ptr );
Event* event_queue_get ( EventQueue* eq );

#endif /* __eventqueue_h__ */
