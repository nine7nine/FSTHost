#include <unistd.h>
#include <signal.h>
#include <stdio.h>

#include "../fst/fst.h"
#include "../xmldb/info.h"

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

void process( FST** fst, int num, int32_t nframes ) {
	int i;
	for ( i=0; i < num; i++ ) {
		/* Process AUDIO */
		int num_ins = fst_num_ins(fst[i]);
		int num_outs = fst_num_outs(fst[i]);

		float ins[nframes];
		float outs[nframes];
		float* pi[num_ins];
		float* po[num_outs];

		memset ( ins, 0, nframes * sizeof(float) );
		memset ( outs, 0, nframes * sizeof(float) );

		int g;
		for( g=0; g < num_ins; g++ ) {
			pi[g] = ins;
		}

		for( g=0; g < num_outs; g++ ) {
			po[g] = outs;
		}

		printf( "INS: %d | OUTS: %d\n", num_ins, num_outs );

		// Deal with plugin
		fst_process( fst[i], pi, po, nframes );
	}
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	int i;
	int	argc = -1;
	char**	argv = NULL;
	cmdline2arg(&argc, &argv, cmdline);

	if ( argc < 2 ) {
		printf ( "Usage: %s plugin ... ...", argv[0] );
		return 1;
	}

	int fst_count = argc - 1;
	int loaded = 0;

	// Handling signals
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &signal_handler;
	sigaction(SIGINT, &sa, NULL); // SIGINT for clean quit

	FST* fst[argc];

	puts ( "...... LOADING ....." );
	for ( i=0; i < fst_count; i++ ) {
		fst[i] = fst_info_load_open ( NULL, argv[i+1] );
		if (! fst[i]) goto exit;
//		fst_run_editor(fst[i], false);
		loaded++;
	}


        // Set block size / sample rate
	puts ( "...... CONFIGURE ....." );
	float sample_rate = 48000;
	intptr_t buffer_size = 1024;
	for ( i=0; i < loaded; i++ ) {
		fst_call_dispatcher (fst[i], effSetSampleRate, 0, 0, NULL, sample_rate);
		fst_call_dispatcher (fst[i], effSetBlockSize, 0, buffer_size, NULL, 0.0f);
		printf("Sample Rate: %g | Block Size: %d\n", sample_rate, buffer_size);
	}

	puts ( "...... RESUMING ....." );
	for ( i=0; i < loaded; i++ )
		fst_call ( fst[i], RESUME );

	puts ("CTRL+C to cancel");
	while ( ! quit ) {
		fst_event_callback();

		puts ( "...... PROCESSING ....." );
		process( fst, loaded, buffer_size );
		
		usleep ( 300000 );
	}

exit:
	for ( i=0; i < loaded; i++ )
		fst_close(fst[i]);

	puts ( "DONE" );

	return 0;
}

