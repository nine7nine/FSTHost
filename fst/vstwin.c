#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "fst.h"

static FST* fst_first = NULL;
static bool WindowClassRegistered = FALSE;

static void fst_event_handler(FST* fst);

static FST* 
fst_new () {
	FST* fst = calloc (1, sizeof (FST));

	pthread_mutex_init (&fst->lock, NULL);
	pthread_mutex_init (&fst->process_lock, NULL);
	//fst->current_program = 0; - calloc done this
	pthread_mutex_init (&(fst->event_call.lock), NULL);
	pthread_cond_init (&(fst->event_call.called), NULL);
//	fst->editor_popup = TRUE;

	return fst;
}

/*************************** Auxiliary routines *****************************************/
/* Plugin "canDo" helper function to neaten up plugin feature detection calls */
static bool
fst_canDo(FST* fst, char* feature) {
	bool can;
	can = (fst->plugin->dispatcher(fst->plugin, effCanDo, 0, 0, (void*)feature, 0.0f) > 0);
	fst_error("Plugin can %-20s : %s", feature, ((can) ? "Yes" : "No"));
	return can;
}

/* Valid program name helper function */
static void valid_program_name ( char* text, size_t size ) {
	char *m = NULL, *c;

	for (c = text; (*c != 0) && (c - text) < size; c++) {
		if ( isprint(*c)) {
			if (m) {
				*m = *c;
				m = c;
			}
		} else if (!m) m = c;
	}

	// make sure of string terminator
	if (m) *m = 0; else *c = 0;
}

/*************************** Multi-plugin support routines *****************************************/
static void
fst_event_loop_remove_plugin (FST* fst) {
	FST* p;
	FST* prev;

	for (p = fst_first, prev = NULL; p->next; prev = p, p = p->next) {
		if (p == fst && prev)
			prev->next = p->next;
	}

	if (fst_first == fst) {
		if (fst_first->next) {
			fst_first = fst_first->next;
		} else {
			fst_first = NULL;
			PostQuitMessage(0);
		}
	}
}

static void
fst_event_loop_add_plugin (FST* fst) {
	if (!fst_first) {
		fst_first = fst;
	} else {
		FST* p = fst_first;
		while (p->next) p = p->next;
		p->next = fst;
	}
}

/*************************** Editor window routines *****************************************/
static LRESULT WINAPI 
my_window_proc (HWND w, UINT msg, WPARAM wp, LPARAM lp) {
	FST* fst = GetPropA(w, "FST");

	switch (msg) {
	case WM_KEYUP:
	case WM_KEYDOWN:
		break;

	case WM_CLOSE:
		if (fst) {
			fst->window = NULL;
			AEffect* plugin = fst->plugin;
			plugin->dispatcher(plugin, effEditClose, 0, 0, NULL, 0.0f);

			if (fst->editor_popup) fst_error("Receive WM_CLOSE - WTF ?");

			if ( fst->window_close ) fst->window_close ( fst->window_close_user_ptr );
		}
		break;
	case WM_NCDESTROY:
	case WM_DESTROY:
//		fst_error("Get destroy %d", w);
		break;
	default:
		break;
	}

	return DefWindowProcA (w, msg, wp, lp );
}

static bool
register_window_class() {
	HMODULE hInst;
	if ((hInst = GetModuleHandleA (NULL)) == NULL) {
		fst_error ("can't get module handle");
		return FALSE;
	}

	WNDCLASSEX wclass;
	wclass.cbSize = sizeof(WNDCLASSEX);
	wclass.style = 0;
//	wclass.style = (CS_HREDRAW | CS_VREDRAW);
	wclass.lpfnWndProc = my_window_proc;
	wclass.cbClsExtra = 0;
	wclass.cbWndExtra = 0;
	wclass.hInstance = hInst;
	wclass.hIcon = LoadIcon(hInst, "FST");
	wclass.hCursor = LoadCursor(0, IDI_APPLICATION);
//	wclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wclass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
	wclass.lpszMenuName = "MENU_FST";
	wclass.lpszClassName = "FST";
	wclass.hIconSm = 0;

	if (!RegisterClassExA(&wclass)){
		fst_error( "Class register failed :(" );
		return FALSE;
	}
	WindowClassRegistered = TRUE;

	return TRUE;
}

static void fst_resize_editor (FST *fst) {
	int height = fst->height;
	int width = fst->width;

	if (! fst->editor_popup) {
		// Add space window title height and borders
		height += 24;
		width += 6;
	}

	SetWindowPos(fst->window, HWND_BOTTOM, 0, 0, width, height, SWP_STATECHANGED|
		SWP_ASYNCWINDOWPOS|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_DEFERERASE);
}

bool fst_show_editor (FST *fst) {
	if (!fst->window) {
		fst_error("no window to show");
		return FALSE;
	}
	
	fst_resize_editor(fst);
	ShowWindowAsync(fst->window, SW_SHOWNORMAL);
//	UpdateWindow(fst->window);

	return TRUE;
}

/*************************** Event call routines *****************************************/
/* Assume that fst->event_call_lock is held */
static void fst_event_call (FST *fst, FSTEventTypes type) {
	FSTEventCall* ec = &( fst->event_call );
	ec->type = type;
	if (GetCurrentThreadId() == fst->MainThreadId) {
		fst_event_handler ( fst );
	} else {
		pthread_mutex_lock (&fst->lock);
		pthread_cond_wait (&ec->called, &fst->lock);
		pthread_mutex_unlock (&fst->lock);
	}
}

void fst_call (FST *fst, FSTEventTypes type) {
	FSTEventCall* ec = &( fst->event_call );
	pthread_mutex_lock ( &ec->lock );
	fst_event_call ( fst, type );
	pthread_mutex_unlock ( &ec->lock );
}

void fst_program_change (FST *fst, int32_t program) {
	FSTEventCall* ec = &( fst->event_call );
	pthread_mutex_lock (&ec->lock);
	if (fst->current_program != program) {
		ec->program = program;
		fst_event_call ( fst, PROGRAM_CHANGE );
	}
	pthread_mutex_unlock (&ec->lock);
}

void fst_configure (FST *fst, float sample_rate, intptr_t block_size) {
	FSTEventCall* ec = &( fst->event_call );

	/* Usa dispatcher as a store for data */
	FSTDispatcher dp;
	dp.val = block_size;
	dp.opt = sample_rate;

	pthread_mutex_lock ( &ec->lock );
	ec->dispatcher = &dp;
	fst_event_call ( fst, CONFIGURE );
	pthread_mutex_unlock ( &ec->lock );
}

intptr_t
fst_call_dispatcher (FST *fst, int32_t opcode, int32_t index, 
			intptr_t val, void *ptr, float opt )
{
	FSTEventCall* ec = &( fst->event_call );

	FSTDispatcher dp;
	dp.opcode = opcode;
	dp.index = index;
	dp.val = val;
	dp.ptr = ptr;
	dp.opt = opt;

	pthread_mutex_lock ( &ec->lock );
	ec->dispatcher = &dp;
	fst_event_call ( fst, DISPATCHER );
	pthread_mutex_unlock ( &ec->lock );

	return dp.retval;
}

/*************************** Public (thread-safe) routines *****************************************/
bool fst_run_editor (FST* fst, bool popup) {
	if (fst->window) return FALSE;

	fst->editor_popup = popup;
	fst_call (fst, EDITOR_OPEN);

	// Check is we really created some window ;-)
	if (!fst->window) {
		fst_error ("no window created for VST plugin editor");
		return FALSE;
	} else {
		return TRUE;
	}
}

bool fst_get_port_name ( FST* fst, int32_t port_number, FSTPortType type, char* name ) {
	int32_t opcode;
	switch ( type ) {
	case FST_PORT_IN:
		opcode = effGetInputProperties;
		break;
	case FST_PORT_OUT:
		opcode = effGetOutputProperties;
		break;
	default: return false;
	}

	VstPinProperties vpp;
	intptr_t success = fst_call_dispatcher(fst, opcode, port_number, 0, &vpp, 0);
	if ( success != 1 ) return false;

	/* Some plugs return empty label */
	if ( strlen(vpp.label) == 0 ) return false;

	return strcpy( name, vpp.label );
	return true;
}

bool fst_get_program_name (FST *fst, int32_t program, char* name, size_t size) {
	char progName[FST_MAX_PROG_NAME];
	if (program == fst->current_program) {
		fst_call_dispatcher(fst, effGetProgramName, 0, 0, progName, 0);
	} else {
		bool success = false;
		if ( fst->vst_version >= 2 )
			success = fst_call_dispatcher(fst, effGetProgramNameIndexed, program, 0, progName, 0);

		if ( ! success ) {
			/* FIXME:
			So what ? nasty plugin want that we iterate around all presets ?
			no way ! We don't have time for this
			*/
			sprintf ( progName, "preset %d", program );
		}
	}
	strncpy ( name, progName, size - 1 );
	valid_program_name ( name, size );

	return TRUE; 
}

bool fst_set_program_name (FST *fst, const char* name) {
	char nname[kVstMaxProgNameLen];
	strncpy ( nname, name, sizeof ( nname ) );
	valid_program_name ( nname, sizeof nname );

	fst_call_dispatcher(fst, effSetProgramName, 0, 0, nname, 0.0f);

	return TRUE;
}

static main_entry_t
fst_get_main_entry(HMODULE dll) {
	main_entry_t main_entry;

	main_entry = (main_entry_t) GetProcAddress (dll, "VSTPluginMain");
	if (main_entry) return main_entry;

	main_entry = (main_entry_t) GetProcAddress (dll, "main");
	if (main_entry) return main_entry;

	fst_error("Can't found either main and VSTPluginMain entry");
	return NULL;
}

FSTHandle* fst_load (const char *path) {
	char mypath[PATH_MAX];
	size_t mypath_maxchars = sizeof(mypath) - 1;

	fst_error("Load library %s", path);

	/* Copy path for later juggling */
	strncpy(mypath, path, mypath_maxchars);

	/* Get basename */
	char* base = basename( (char*) path );

	// Just try load plugin
	HMODULE dll = LoadLibraryA(mypath);
	if ( dll ) goto have_dll;
	
	// Try find plugin in VST_PATH
	char* env = getenv("VST_PATH");
	if ( env ) {
		char* vpath = strtok (env, ":");
		while (vpath) {
			char* last = vpath + strlen(vpath) - 1;
			if (*last == '/') {
				snprintf(mypath, sizeof(mypath), "%s%s", vpath, base);
			} else {
				snprintf(mypath, sizeof(mypath), "%s/%s", vpath, base);
			}

			fst_error("Load library %s", mypath);
			dll = LoadLibraryA(mypath);
			if (dll) goto have_dll;

			vpath = strtok (NULL, ":");
		}
	}

	fst_error("Can't load library: %s", base);
	return NULL;

have_dll: ;
/* Wine path to library
	char buf[PATH_MAX];
	GetModuleFileName(dll, (LPSTR) &buf, sizeof(buf));
	fst_error("GetModuleFileName: %s", buf);
*/

	main_entry_t main_entry = fst_get_main_entry(dll);
	if (! main_entry) {	
		FreeLibrary (dll);
		return NULL;
	}

	char* fullpath = realpath(mypath,NULL);
	if (! fullpath) fullpath = strdup(mypath);

	char* ext = strstr(base, ".dll");
	if (!ext) ext = strstr(base, ".DLL");
	char* name = (ext) ? strndup(base, ext - base) : strdup(base);
	
	FSTHandle* fhandle;
	fhandle = malloc(sizeof(FSTHandle));
	fhandle->dll = dll;
	fhandle->main_entry = main_entry;
	fhandle->path = fullpath;
	fhandle->name = name;

	return fhandle;
}

bool fst_unload (FSTHandle* fhandle) {
	fst_error("Unload library: %s", fhandle->path);
	FreeLibrary (fhandle->dll);
	free (fhandle->path);
	free (fhandle->name);
	free (fhandle);

	return TRUE;
}

FST* fst_open (FSTHandle* fhandle) {
	if (fhandle == NULL) {
	    fst_error( "the handle was NULL" );
	    return NULL;
	}
	fst_error("Revive plugin: %s", fhandle->name);

	AEffect* plugin = fhandle->main_entry ( amc_callback );
	if (plugin == NULL)  {
		fst_error ("%s could not be instantiated", fhandle->name);
		return NULL;
	}

	if (plugin->magic != kEffectMagic) {
		fst_error ("%s is not a VST plugin", fhandle->name);
		return NULL;
	}

	FST* fst = fst_new ();
	fst->plugin = plugin;
	fst->handle = fhandle;
	fst->plugin->resvd1 = (intptr_t*) &( fst->amc );

	// Open Plugin
	plugin->dispatcher (plugin, effOpen, 0, 0, NULL, 0.0f);
	fst->vst_version = (int) plugin->dispatcher (plugin, effGetVstVersion, 0, 0, NULL, 0.0f);

	if (fst->vst_version >= 2) {
		fst->canReceiveVstEvents = fst_canDo(fst, "receiveVstEvents");
		fst->canReceiveVstMidiEvent = fst_canDo(fst, "receiveVstMidiEvent");
		fst->canSendVstEvents = fst_canDo(fst, "sendVstEvents");
		fst->canSendVstMidiEvent = fst_canDo(fst, "sendVstMidiEvent");

		fst->isSynth = (plugin->flags & effFlagsIsSynth);
		fst_error("%-31s : %s", "Plugin isSynth", fst->isSynth ? "Yes" : "No");

		bool pr = (plugin->flags & effFlagsCanReplacing);
		fst_error("%-31s : %s", "Support processReplacing", pr ? "Yes" : "No");

		/* Get plugin name */
		char tmpstr[kVstMaxEffectNameLen];
		if ( plugin->dispatcher (plugin, effGetEffectName, 0, 0, tmpstr, 0 ) )
			fst->name = strdup ( tmpstr );
	}

	// We always need some name ;-)
	if (! fst->name) fst->name = strdup ( fst->handle->name );

	// Bind to plugin list
	fst_event_loop_add_plugin(fst);

	fst->MainThreadId = GetCurrentThreadId();

	return fst;
}

FST* fst_load_open ( const char* path ) {
	if ( ! path ) {
		fst_error ( "empty plugin path ?" );
		return NULL;
	}

	// Load plugin library
	FSTHandle* handle = fst_load(path);
	if (! handle) return NULL;

	// Revive plugin
	FST* fst = fst_open(handle);
	if (! fst) return NULL;

	return fst;
}

void fst_close (FST* fst) {
	fst_call ( fst, CLOSE );	

	fst_unload(fst->handle);
	free(fst->name);
	free(fst);
}

/*************************** Event handler routines *****************************************/
static bool fst_create_editor (FST* fst) {
	AEffect* plugin = fst->plugin;

	/* "guard point" to trap errors that occur during plugin loading */
	if (!(plugin->flags & effFlagsHasEditor)) {
		fst_error ("Plugin \"%s\" has no editor", fst->handle->name);
		return FALSE;
	}

	HMODULE hInst;
	if ((hInst = GetModuleHandleA (NULL)) == NULL) {
		fst_error ("can't get module handle");
		return FALSE;
	}

	if ( ! WindowClassRegistered && ! register_window_class() )
		return FALSE;

	HWND window;
	if ((window = CreateWindowA ("FST", fst->handle->name, (fst->editor_popup) ? 
		(WS_POPUP & ~WS_TABSTOP) :
//		(WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_CAPTION) :
		(WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hInst, NULL)) == NULL)
	{
		fst_error ("cannot create editor window");
		return FALSE;
	}
	fst->window = window;

	/* Request plugin to open editor */
	plugin->dispatcher (plugin, effEditOpen, 0, 0, window, 0);

	/* Get editor window size */
	struct ERect* er;
	plugin->dispatcher (plugin, effEditGetRect, 0, 0, &er, 0 );
	fst->width  = er->right - er->left;
	fst->height = er->bottom - er->top;

	// Bind FST to window
	if (! SetPropA(window, "FST", fst))
		fst_error ("cannot set GUI window property");

	if (fst->editor_popup) {
		SetWindowPos (window, 0, 0, 0, 0, 0, SWP_SHOWWINDOW|SWP_NOMOVE|SWP_NOOWNERZORDER|
			SWP_NOREDRAW|SWP_NOCOPYBITS|SWP_DEFERERASE|SWP_NOZORDER|SWP_STATECHANGED);
		UpdateWindow(window);
	} else {
		fst_show_editor(fst);
	}

	fst->xid = GetPropA (window, "__wine_x11_whole_window");
//	fst_error("And xid = %p", fst->xid );

	return TRUE;
}

static inline void fst_destroy_editor ( FST* fst ) {
	if ( fst->window ) {
		AEffect* plugin = fst->plugin;
		plugin->dispatcher (plugin, effEditClose, 0, 0, NULL, 0.0f);
		DestroyWindow ( fst->window );
		fst->window = NULL;
	}
}

static inline void fst_suspend ( FST* fst ) {
	AEffect* plugin = fst->plugin;
	fst_error("Suspend plugin");
	fst_process_lock ( fst );
	plugin->dispatcher (plugin, effStopProcess, 0, 0, NULL, 0.0f);
	plugin->dispatcher (plugin, effMainsChanged, 0, 0, NULL, 0.0f);
	fst_process_unlock ( fst );
}

static inline void
fst_update_current_program(FST* fst) {
	AEffect* plugin = fst->plugin;
	int32_t newProg = plugin->dispatcher ( plugin, effGetProgram, 0, 0, NULL, 0 );
	if ( newProg != fst->current_program ) {
		fst->current_program = newProg;
		char progName[FST_MAX_PROG_NAME];
		plugin->dispatcher ( plugin, effGetProgramName, 0, 0, progName, 0 );
		fst_error ("Program: %d : %s", newProg, progName);
	}
}

static inline void fst_plugin_idle ( FST* fst ) {
	if (fst->wantIdle)
		fst->plugin->dispatcher (fst->plugin, effIdle, 0, 0, NULL, 0);  

	if (fst->window)
		fst->plugin->dispatcher (fst->plugin, effEditIdle, 0, 0, NULL, 0);
}



static inline void fst_event_handler (FST* fst) {
	FSTEventCall* ec = &( fst->event_call );
	if ( ec->type == RESET ) return;

	AEffect* plugin = fst->plugin;
	AMC* amc = &fst->amc;
	FSTDispatcher* dp = ec->dispatcher;

	pthread_mutex_lock (&fst->lock);
	switch ( ec->type ) {
	case CLOSE:
		fst_error("Closing plugin: %s", fst->name);
		fst_suspend(fst);
		fst_destroy_editor (fst);
		plugin->dispatcher(plugin, effClose, 0, 0, NULL, 0.0f);
		fst_event_loop_remove_plugin(fst);
		fst_error("Plugin closed");
		break;
	case SUSPEND:
		fst_suspend (fst);
		break;
	case RESUME:
		fst_error("Resume plugin");
		fst_process_lock ( fst );
		plugin->dispatcher (plugin, effMainsChanged, 0, 1, NULL, 0.0f);
		plugin->dispatcher (plugin, effStartProcess, 0, 0, NULL, 0.0f);
		fst_process_unlock ( fst );
		break;
	case CONFIGURE:
		amc->block_size = dp->val;
		amc->sample_rate = dp->opt;
		fst_error("Sample Rate: %g | Block Size: %d", amc->sample_rate, amc->block_size);
		plugin->dispatcher( plugin, effSetSampleRate, 0, 0, NULL, amc->sample_rate );
		plugin->dispatcher( plugin, effSetBlockSize, 0, amc->block_size, NULL, 0.0f );
		break;
	case EDITOR_OPEN:
		fst_create_editor(fst);
		break;
	case EDITOR_CLOSE:
		fst_destroy_editor (fst);
		break;
	case EDITOR_RESIZE:
		fst_resize_editor(fst);
		break;
	case PROGRAM_CHANGE:
		plugin->dispatcher (plugin, effBeginSetProgram, 0, 0, NULL, 0);
		fst_process_lock ( fst );
		plugin->dispatcher (plugin, effSetProgram, 0, ec->program, NULL, 0);

		// NOTE: some plugins needs effIdle call for update program
		fst_plugin_idle ( fst );

		fst_process_unlock ( fst );
		plugin->dispatcher (plugin, effEndSetProgram, 0, 0, NULL, 0);

		fst_update_current_program ( fst );
		break;
	case DISPATCHER:
		dp->retval = plugin->dispatcher( plugin, dp->opcode, dp->index, dp->val, dp->ptr, dp->opt );
		break;
	case RESET:
		break;
	}
	ec->type = RESET;
	pthread_mutex_unlock (&fst->lock);

	pthread_cond_signal (&ec->called);
}

static void fst_event_dispatcher() {
	FST* fst;
	for (fst = fst_first; fst; fst = fst->next) {
		fst_plugin_idle ( fst );

		fst_update_current_program ( fst );

		fst_event_handler(fst);
	}
}

bool fst_event_callback() {
	MSG msg;
	while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE) != 0) {
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
		if (msg.message == WM_QUIT)
			return FALSE;
	}
	
	fst_event_dispatcher();

	return TRUE;
}

void fst_show_thread_info ( const char* th_name ) {
	HANDLE* h_thread = GetCurrentThread();
        printf("%s Thread W32ID: %d | LWP: %d | W32 Class: %d | W32 Priority: %d\n",
		th_name,
		GetCurrentThreadId(),
		(int) syscall (SYS_gettid),
		GetPriorityClass(h_thread),
		GetThreadPriority(h_thread)
	);
}

void fst_set_thread_priority ( const char* th_name, int class, int priority ) {
	HANDLE* h_thread = GetCurrentThread ();
	SetPriorityClass ( h_thread, class );
	SetThreadPriority ( h_thread, priority );
	fst_show_thread_info ( th_name );
}

void fst_event_loop () {

	register_window_class();

	if (!SetTimer (NULL, 1000, 100, NULL)) {
		fst_error ("cannot set timer on dummy window");
		return;
	}

 	MSG msg;
	while (GetMessageA (&msg, NULL, 0,0) != 0) {
 		TranslateMessage(&msg);
 		DispatchMessageA(&msg);
		if ( msg.message != WM_TIMER )
			continue;

		fst_event_dispatcher();
	}

	fst_error( "GUI EVENT LOOP: THE END" );
}
