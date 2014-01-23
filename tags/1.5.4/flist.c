#include <stdlib.h>

#define APPNAME "fsthost"

/* From info.c */
extern char* fst_info_default_path(const char* appname);
extern int fst_info_list(const char* dbpath);

int main(int argc, char **argv) {
	if ( argc == 2) {
		return fst_info_list( argv[1] );
	} else {
		char* dbpath = fst_info_default_path( APPNAME );
		int ret = fst_info_list( dbpath );
		free ( dbpath );
		return ret;
	}
}
