#include "fst.h"

const char* my_motherfuckin_name = "fsthost_info";
extern void fst_error (const char *fmt, ...);

// most simple one :) could be sufficient.... 
static long simple_master_callback( struct AEffect *fx, long opcode, long index, long value, void *ptr, float opt ) {
    if( opcode == audioMasterVersion )
        return 2;
    else
        return 0;
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	LPWSTR*		szArgList;
	int		argc;
	char**		argv;
	short		i;

	FST*		fst;
	FSTHandle*	handle;

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

	printf("Load plugin %s\n", argv[1]);
	if ((handle = fst_load (argv[1])) == NULL) {
		fst_error ("can't load plugin %s", argv[1]);
		return 1;
	}

	printf( "Revive plugin: %s\n", handle->name);
	if ((fst = fst_open (handle, (audioMasterCallback) simple_master_callback, NULL)) == NULL) {
		fst_error ("can't instantiate plugin %s", handle->name);
		return 1;
	}
	printf("Close plugin: %s\n", handle->name);
	fst_close(fst);

	printf("Unload plugin: %s\n", argv[1]);
	fst_unload(handle);

	return 0;
}
