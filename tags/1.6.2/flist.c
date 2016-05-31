#include <stddef.h>

extern int fst_info_list(const char* dbpath, const char* arch);

int main(int argc, char **argv) {
	char* path = ( argc == 2 ) ? argv[1] : NULL;

	return fst_info_list( argv[1], NULL );
}
