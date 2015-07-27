#ifndef __info_h__
#define __info_h__

#include "fst/fst.h"

/* info.c */
int fst_info_update(const char *dbpath, const char *fst_path);
FST* fst_info_load_open ( const char* dbpath, const char* plug_spec );

/* list.c */
int fst_info_list(const char* dbpath, const char* arch);
char* fst_info_default_path();

#endif /* __info_h__ */
