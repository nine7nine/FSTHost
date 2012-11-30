#include <dirent.h>
#include "fst.h"

const char* my_motherfuckin_name = "fsthost_info";
extern void fst_error (const char *fmt, ...);

// most simple one :) could be sufficient.... 
static long
simple_master_callback( struct AEffect *fx, long opcode, long index, long value, void *ptr, float opt ) {
	if ( opcode == audioMasterVersion ) {
		return 2;
	} else {
		return 0;
	}
}

void fst_get_info(char* path) {
	FST*		fst;
	FSTHandle*	handle;

	printf("Load plugin %s\n", path);
	handle = fst_load(path);
	if (! handle) {
		fst_error ("can't load plugin %s", path);
		return;
	}

	printf( "Revive plugin: %s\n", handle->name);
	fst = fst_open(handle, (audioMasterCallback) simple_master_callback, NULL);
	if (! fst) {
		fst_error ("can't instantiate plugin %s", handle->name);
		return;
	}
	printf("Close plugin: %s\n", handle->name);
	fst_close(fst);

	printf("Unload plugin: %s\n", path);
	fst_unload(handle);
}

void scandirectory( const char *dir ) {
	struct dirent *entry;
	DIR *d = opendir(dir);

	if ( !d ) return;

	char fullname[PATH_MAX];
	while ( (entry = readdir( d )) ) {
		if (entry->d_type & DT_DIR) {
			/* Check that the directory is not "d" or d's parent. */
            
			if (! strcmp (entry->d_name, "..") || ! strcmp (entry->d_name, "."))
				continue;

			snprintf( fullname, PATH_MAX, "%s/%s", dir, entry->d_name );

			scandirectory(fullname);
		} else if (entry->d_type & DT_REG) {
			if (! strstr( entry->d_name, ".dll" ) && ! strstr( entry->d_name, ".DLL" ))
				continue;

			snprintf( fullname, PATH_MAX, "%s/%s", dir, entry->d_name );
    
			fst_get_info(fullname);
		}
	}
	closedir(d);
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	LPWSTR*		szArgList;
	int		argc;
	char**		argv;
	short		i;

	// Parse command line
	szArgList = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (szArgList == NULL) {
		fprintf(stderr, "Unable to parse command line\n");
		return 10;
	}

    	argv = alloca(argc * sizeof(char*));
	for (i=0; i < argc; ++i) {
		int nsize = WideCharToMultiByte(CP_UNIXCP, 0, szArgList[i], -1, NULL, 0, NULL, NULL);
		
		argv[i] = malloc( nsize );
		WideCharToMultiByte(CP_UNIXCP, 0, szArgList[i], -1, (LPSTR) argv[i], nsize, NULL, NULL);
	}
	LocalFree(szArgList);
	strcpy(argv[0], my_motherfuckin_name); // Force APP name

//	fst_get_info(argv[1]);
	scandirectory(argv[1]);
	
	return 0;
}
