#include <stdlib.h>

extern int fst_info_list(const char* dbpath);

int main(int argc, char **argv) {
	char* path = ( argc == 2 ) ? argv[1] : NULL;

	return fst_info_list( argv[1] );
}