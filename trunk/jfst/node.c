#include "node.h"
#include "log/log.h"

static JFST_NODE* jfst_node_first = NULL;

JFST_NODE* jfst_node_get_first() {
	return jfst_node_first;
}

JFST_NODE* jfst_node_new( const char* appname ) {
	JFST_NODE* n = malloc ( sizeof(JFST_NODE) );
	n->jfst = jfst_new( appname );
	n->next = NULL;

	// Reset changes
	int i;
	for ( i=0; i < SERV_POLL_SIZE; i++ )
		n->changes[i] = 0;

	/* Link to list */
	if ( jfst_node_first ) {
		JFST_NODE* p = jfst_node_first;
		while ( p->next ) p = p->next;
		p->next = n;
	} else {
		jfst_node_first = n; // I'm the first !
	}

	return n;
}

void jfst_node_free_all() {
	JFST_NODE* n = jfst_node_first;
	while ( n ) {
		JFST_NODE* t = n;
		n = n->next;

		jfst_close(t->jfst);
		free(t);
	}
}
