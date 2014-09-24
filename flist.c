#include <stdlib.h>

#include "xmldb/info.h"

#define APPNAME "fsthost"

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
