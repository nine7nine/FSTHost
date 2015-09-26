#ifndef __gjfst_h__
#define __gjfst_h__

#include <jfst/jfst.h>

void gjfst_init(int *argc, char **argv[]);
void gjfst_add (JFST* jfst, bool editor);
void gjfst_start();
void gjfst_quit();
void gjfst_free(JFST* jfst);

#endif /* gjfst_h */
