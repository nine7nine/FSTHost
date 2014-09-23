#include <unistd.h>
#include <signal.h>
#include <stdio.h>

#include "../fst/fst.h"

volatile bool quit = false;
static void signal_handler (int signum) {
	switch(signum) {
	case SIGINT:
		puts("Caught signal to terminate (SIGINT)");
		quit = true;
		break;
	}
}

static void cmdline2arg(int *argc, char ***pargv, LPSTR cmdline) {
	LPWSTR* szArgList = CommandLineToArgvW(GetCommandLineW(), argc);
	if (!szArgList) {
		fputs("Unable to parse command line", stderr);
		*argc = -1;
		return;
	}

	char** argv = malloc(*argc * sizeof(char*));
	unsigned short i;
	for (i=0; i < *argc; ++i) {
		int nsize = WideCharToMultiByte(CP_UNIXCP, 0, szArgList[i], -1, NULL, 0, NULL, NULL);
		
		argv[i] = malloc( nsize );
		WideCharToMultiByte(CP_UNIXCP, 0, szArgList[i], -1, (LPSTR) argv[i], nsize, NULL, NULL);
	}
	LocalFree(szArgList);
	argv[0] = (char*) "fsthost_multi"; // Force APP name
	*pargv = argv;
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	int	argc = -1;
	char**	argv = NULL;
	cmdline2arg(&argc, &argv, cmdline);

	if ( argc < 1 ) {
		puts ( "Kibel" );
		return 1;
	}

	// Handling signals
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &signal_handler;
	sigaction(SIGINT, &sa, NULL); // SIGINT for clean quit

	FST* fst[argc];

	int i;
	for ( i=0; i < argc - 1; i++ ) {
		fst[i] = fst_load_open(argv[i+1]);
		if (! fst[i]) return 1;

		fst_call ( fst[i], RESUME );

//		fst_run_editor(fst[i], false);
	}

	fst_error("CTRL+C to cancel");
	while ( ! quit ) {
		fst_event_callback();
		usleep ( 300000 );
	}

	for ( i=0; i < argc - 1; i++ ) {
		fst_close(fst[i]);
	}

	return 0;
}

