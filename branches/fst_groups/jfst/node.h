#ifndef __node_h__
#define __node_h__

#include "jfst.h"
#include "serv/serv.h"

typedef struct _JFST_NODE {
	struct _JFST_NODE* next;
	JFST* jfst;
	ChangesLast changes_last[SERV_POLL_SIZE];
} JFST_NODE;

JFST_NODE* jfst_node_get_first();
JFST_NODE* jfst_node_new( const char* appname, FST_THREAD* fst_th );
void jfst_node_free_all();

#endif /* __node_h__ */
