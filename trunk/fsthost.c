/*
    Copyright (C) 2015 Pawel Piatek <xj@wp.pl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <windows.h>

#include "log/log.h"
#include "jfst/node.h"
#include "xmldb/info.h"

#ifndef NO_GTK
#include <glib.h>
#include "gtk/gjfst.h"
GMainLoop* g_main_loop;
#endif

#define VERSION "1.6.3"
#ifdef __x86_64__
#define ARCH "64"
#else
#define ARCH "32"
#endif
#define APPNAME "fsthost"
#define APPNAME_ARCH APPNAME ARCH

#define MAX_PLUGS 32
#define LOOP_INT 100 // 100ms

/* lash.c */
#ifdef HAVE_LASH
extern void jfst_lash_init(int* argc, char** argv[]);
extern bool jfst_lash_idle(JFST *jfst);
extern void jfst_lash_add (JFST* jfst);
#endif

/* fsthost_proto */
extern bool fsthost_proto_init( uint16_t port_number );
extern void proto_poll();
extern void proto_close();

volatile bool quit = false;
volatile bool open_editor = false;
volatile bool separate_threads = false;
volatile bool sigusr1_save_state = false;
volatile bool gtk_gui = true;

struct plugin {
	const char* path;
	const char* state;
	const char* uuid;
	const char* client_name;
};

void fsthost_quit() {
	quit = true;
#ifndef NO_GTK
	if (gtk_gui) {
		gjfst_quit();
	} else {
		g_main_loop_quit(g_main_loop);
	}
#endif
}

static void signal_handler (int signum) {
	switch(signum) {
	case SIGINT:
		log_info("Caught signal to terminate (SIGINT)");
		quit = true;
		break;
	case SIGTERM:
		log_info("Caught signal to terminate (SIGTERM)");
		quit = true;
		break;
	case SIGUSR1:
		log_info("Caught signal to save state (SIGUSR1)");
		JFST *jfst = jfst_node_get_first()->jfst;
		if (jfst->default_state_file)
			jfst_save_state(jfst, jfst->default_state_file);
		break;
	case SIGUSR2:
		log_info("Caught signal to open editor (SIGUSR2)");
		open_editor = true;
		break;
	}
}

bool fsthost_idle () {
	JFST_NODE* jnf = jfst_node_get_first();
	if ( ! jnf ) return true;

	/* TODO: jfst ask for quit suppoert */

#ifdef HAVE_LASH
	if ( ! jfst_lash_idle(jnf->jfst) )
		quit = true;
#endif

	if ( quit ) {
		fsthost_quit();
		return false;
	}

	proto_poll();

	if ( open_editor ) {
		open_editor = false;
		JFST_NODE* jn;
		for ( jn = jfst_node_get_first(); jn; jn = jn->next )
			fst_run_editor( jn->jfst->fst, false );
	}

	return true;
}

static void edit_close_handler ( void* arg ) {
	quit = true;
}

static void cmdline2arg(int *argc, char ***pargv, LPSTR cmdline) {
	LPWSTR* szArgList = CommandLineToArgvW(GetCommandLineW(), argc);
	if (!szArgList) {
		log_error("Unable to parse command line");
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
	argv[0] = (char*) APPNAME_ARCH; // Force APP name
	*pargv = argv;
}

static void usage(char* appname) {
	const char* fmt = "%-20s%s\n";

	fprintf(stderr, "\nUsage: %s [ options ] <plugin>\n", appname);
	fprintf(stderr, "  or\n");
	fprintf(stderr, "Usage: %s -L [ -d <xml_db_info> ]\n", appname);
	fprintf(stderr, "  or\n");
	fprintf(stderr, "Usage: %s -g [ -d <xml_db_info> ] <path_for_add_to_db>\n", appname);
	fprintf(stderr, "  or\n");
	fprintf(stderr, "Usage: %s -s <FPS state file>\n\n", appname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, fmt, "-g", "Create/Update XML info DB.");
	fprintf(stderr, fmt, "-L", "List plugins from XML info DB.");
	fprintf(stderr, fmt, "-d xml_db_path", "Custom path to XML DB");
	fprintf(stderr, fmt, "-b", "Start in bypass mode");
#ifndef NO_GTK
	fprintf(stderr, fmt, "-n", "Disable GTK GUI");
#endif
	fprintf(stderr, fmt, "-N", "Notify changes by SysEx");
	fprintf(stderr, fmt, "-e", "Hide Editor");
	fprintf(stderr, fmt, "-S <port>", "Start CTRL server on port <port>. Use 0 for random.");
	fprintf(stderr, fmt, "-s <state_file>", "Load <state_file>");
	fprintf(stderr, fmt, "-c <client_name>", "Jack Client name");
	fprintf(stderr, fmt, "-A", "Set plugin port names as aliases");
	fprintf(stderr, fmt, "-k channel", "MIDI Channel (0: all, 17: none)");
	fprintf(stderr, fmt, "-i num_in", "Jack number In ports");
	fprintf(stderr, fmt, "-j <connect_to>", "Connect Audio Out to <connect_to>. " JFST_STR_NO_CONNECT " for no connect");
	fprintf(stderr, fmt, "-l", "save state to state_file on SIGUSR1 (require -s)");
	fprintf(stderr, fmt, "-m mode_midi_cc", "Bypass/Resume MIDI CC (default: 122)");
	fprintf(stderr, fmt, "-p", "Plugin path ( same as <plugin>, - for skip )");
	fprintf(stderr, fmt, "-M", "Disable connecting MIDI In port to all physical");
	fprintf(stderr, fmt, "-P", "Self MIDI Program Change handling");
	fprintf(stderr, fmt, "-o num_out", "Jack number Out ports");
	fprintf(stderr, fmt, "-T", "Separate threads");
	fprintf(stderr, fmt, "-u uuid", "JackSession UUID");
	fprintf(stderr, fmt, "-U SysExID", "SysEx ID (1-127). 0 is default (do not use it)");
	fprintf(stderr, fmt, "-v", "Verbose");
	fprintf(stderr, fmt, "-V", "Disable Volume control / filtering CC7 messages");
}

static inline void
main_loop() {
	log_info("GUI Disabled - start MainLoop");
	while ( ! quit ) {
		if ( ! fsthost_idle() )
			break;

		usleep ( LOOP_INT * 1000 );
	}
}

static bool
new_plugin( struct plugin* plug, FST_THREAD* fst_one_th ) {
	JFST_NODE* jn = jfst_node_new(APPNAME);
	JFST* jfst = jn->jfst;
	jfst->default_state_file = plug->state;
	jfst->uuid = (char*) plug->uuid;
	jfst->client_name = (char*) plug->client_name;

	FST_THREAD* fst_th = (fst_one_th) ? fst_one_th : fst_thread_new("GUI/Event (Sep)", false);

	if ( plug->path != NULL && !strcmp(plug->path,"-") )
		plug->path = NULL;

	/* Load plugin - in this thread or dedicated */
	bool loaded = jfst_load ( jfst, plug->path, sigusr1_save_state, fst_th );

	/* Well .. Are we loaded plugin ? */
	if (! loaded) return false;

	// Init Jack
	if ( ! jfst_init(jfst) )
		return false;
	return true;
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	int		argc = -1;
	char**		argv = NULL;
	int		ret = 1;
	bool		opt_have_serv = false;
	bool		show_fst_editor = true;

	JFST_DEFAULTS* def = jfst_get_defaults();

{ /* Parse options | init JFST NODES | Limit scope of some initial variables */
	uint16_t	opt_port_number = 0;
	int		pc = 0; // plugins count
	int		sc = 0; // states count
	LogLevel	log_level = LOG_INFO;

	enum { NORMAL, LIST, GEN_DB } mode = NORMAL;

	struct plugin	plugins[MAX_PLUGS];
	memset( plugins, 0, sizeof(struct plugin) * MAX_PLUGS );

	// Handle FSTHOST_GUI environment
	char* menv = getenv("FSTHOST_GUI");
	if ( menv ) {
		int gui = strtol(menv, NULL, 10);
		switch ( gui ) {
			case 0: show_fst_editor = gtk_gui = false; break;
			case 1: show_fst_editor = false; break;
			/* 2 or more is normal mode */
		}
	}

	// Handle FSTHOST_THREADS environment
	menv = getenv("FSTHOST_THREADS");
	if ( menv ) separate_threads = true;

        // Parse command line options
	cmdline2arg(&argc, &argv, cmdline);
	short c;
	while ( (c = getopt (argc, argv, "Abd:egs:S:c:k:i:j:lLnNMm:p:Po:Tu:U:vV")) != -1) {
		switch (c) {
			case 'A': def->want_port_aliases = true; break;
			case 'b': def->bypassed = true; break;
			case 'd': def->dbinfo_file = optarg; break;
			case 'e': show_fst_editor = false; break;
			case 'g': mode = GEN_DB; break;
			case 'L': mode = LIST; break;
			case 's': plugins[sc++].state = optarg; break;
			case 'S': opt_have_serv=true; opt_port_number = strtol(optarg,NULL,10); break;
			case 'c': plugins[pc].client_name = optarg; break;
			case 'k': def->channel = strtol(optarg, NULL, 10); break;
			case 'i': def->maxIns = strtol(optarg, NULL, 10); break;
			case 'j': def->connect_to = optarg; break;
			case 'l': sigusr1_save_state = true; break;
			case 'p': plugins[pc++].path = optarg; break;
			case 'M': def->want_auto_midi_physical = false; break;
			case 'P': def->midi_pc = MIDI_PC_SELF; break; /* used but not enabled */
			case 'o': def->maxOuts = strtol(optarg, NULL, 10); break;
			case 'n': gtk_gui = false; break;
			case 'N': def->sysex_want_notify = true; break;
			case 'm': def->want_state_cc = strtol(optarg, NULL, 10); break;
			case 'T': separate_threads = true;
			case 'u': plugins[pc].uuid = optarg; break;
			case 'U': def->sysex_uuid = strtol(optarg, NULL, 10); break;
			case 'v': log_level = LOG_DEBUG; break;
			case 'V': def->no_volume = true; break;
			default: usage (argv[0]); return 1;
		}
	}

#ifdef NO_GTK
	gtk_gui = false;
#endif

	/* We have more arguments than getops options */
	for ( pc = 0; optind < argc; optind++, pc++ )
		plugins[pc].path = argv[optind];

	/* Under Jack Session Manager Control "-p -j !" is forced */
	if ( getenv("SESSION_DIR") ) {
		def->want_auto_midi_physical = false;
		def->connect_to = JFST_STR_NO_CONNECT;
	}

	log_init ( log_level, NULL, NULL );
	log_info( "FSTHost Version: %s (%s)", VERSION, ARCH "bit" );

	switch ( mode ) {
		case LIST: /* List plugins then abandon other tasks */
			return fst_info_list ( def->dbinfo_file, ARCH );
		case GEN_DB: /* If path is NULL then Generate using VST_PATH */
			ret = fst_info_update ( def->dbinfo_file, plugins[0].path );
			if (ret > 0) usage ( argv[0] );
			return ret;
		case NORMAL:
			break;
	}

	/* Create FST thread ( if not separate mode ) */
	FST_THREAD* fst_one_thread = NULL;
	if ( ! separate_threads ) {
		fst_one_thread = fst_thread_new("GUI/Event", false);
		if ( ! fst_one_thread ) goto game_over;
	}

	/* Init JFST Nodes aka plugins */
	for ( pc = 0; pc < MAX_PLUGS; pc++ ) {
		if ( ! plugins[pc].path && ! plugins[pc].state )
			break;

		if ( ! new_plugin(&plugins[pc],fst_one_thread) )
			goto game_over;
	}
	if ( pc == 0 ) goto game_over; // no any plugin loaded

#ifdef HAVE_LASH
	JFST_NODE* jnf = jfst_node_get_first();
	jfst_lash_init(&argc, &argv);
	jfst_lash_add (jnf->jfst);
#endif

	// Socket stuff
	if ( opt_have_serv ) {
		if ( ! fsthost_proto_init(opt_port_number) )
			return 1;
	}
} /* end of "parse" scope */

	// Set Thread policy - usefull only with WineRT/LPA patch
	//fst_set_thread_priority ( "Main", REALTIME_PRIORITY_CLASS, THREAD_PRIORITY_TIME_CRITICAL );
	fst_set_thread_priority ( "Main", ABOVE_NORMAL_PRIORITY_CLASS, THREAD_PRIORITY_ABOVE_NORMAL );

	// Handling signals
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &signal_handler;
	sigaction(SIGINT, &sa, NULL);  // SIGINT  - clean quit
	sigaction(SIGTERM, &sa, NULL); // SIGTERM - clean quit
	sigaction(SIGUSR2, &sa, NULL); // SIGUSR2 - open editor

	// SIGUSR1 - save state ( ladish support )
	// NOTE: seems that wine use this signal internally
	if ( sigusr1_save_state )
		sigaction(SIGUSR1, &sa, NULL);

	if ( show_fst_editor && ! gtk_gui ) {
		open_editor = true;

		JFST_NODE* jn;
		for ( jn = jfst_node_get_first() ; jn; jn = jn->next )
			fst_set_window_close_callback( jn->jfst->fst, edit_close_handler, jn->jfst );
	}

#ifdef NO_GTK
	main_loop();
#else
	g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, LOOP_INT, (GSourceFunc) fsthost_idle, NULL, NULL);
	if (gtk_gui) {
		log_info( "Start GUI" );
		gjfst_init(&argc, &argv);

		JFST_NODE* jn = jfst_node_get_first();
		for ( ; jn; jn = jn->next )
			gjfst_add( jn->jfst, show_fst_editor );

		gjfst_start();

		for ( jn = jfst_node_get_first(); jn; jn = jn->next )
			gjfst_free( jn->jfst );
	} else {
		g_main_loop = g_main_loop_new(NULL, TRUE);
		g_main_loop_run(g_main_loop);
	}
#endif
	ret = 0;
	log_info( "Game Over" );

game_over:
	if ( opt_have_serv )
		proto_close();

	if ( ret > 0 ) usage ( argv[0] );
	jfst_node_free_all();
	return ret;
}
