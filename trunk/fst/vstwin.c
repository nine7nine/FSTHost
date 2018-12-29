#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <windows.h>

#include "log/log.h"
#include "fst_int.h"

#define INF log_info
#define DEBUG log_debug
#define ERR log_error

static atomic_bool WindowClassRegistered = false;

static void fst_event_handler(FST* fst, FSTCall* call);

static FST* 
fst_new () {
	FST* fst = calloc (1, sizeof (FST));

	fst->initialized = false;
	fst->opened = false;
	pthread_mutex_init (&fst->lock, NULL);
	pthread_mutex_init (&fst->process_lock, NULL);
	//fst->current_program = 0; - calloc done this
	pthread_mutex_init (&(fst->event_call.lock), NULL);
	pthread_cond_init (&(fst->event_call.called), NULL);
//	fst->editor_popup = true;

	return fst;
}

/*************************** Auxiliary routines *****************************************/
/* Plugin "canDo" helper function to neaten up plugin feature detection calls */
static bool
fst_canDo(FST* fst, char* feature) {
	bool can;
	can = (fst->plugin->dispatcher(fst->plugin, effCanDo, 0, 0, (void*)feature, 0.0f) > 0);
	INF("Plugin can %-20s : %s", feature, ((can) ? "Yes" : "No"));
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

/*************************** FST HANDLE routines ********************************************/
static main_entry_t
fst_get_main_entry(HMODULE dll) {
	main_entry_t main_entry;

	main_entry = (main_entry_t) GetProcAddress (dll, "VSTPluginMain");
	if (main_entry) return main_entry;

	main_entry = (main_entry_t) GetProcAddress (dll, "main");
	if (main_entry) return main_entry;

	ERR("Can't found either main and VSTPluginMain entry");
	return NULL;
}

FSTHandle* fst_load (const char *path) {
	char mypath[PATH_MAX];
	size_t mypath_maxchars = sizeof(mypath) - 1;

	INF("Load library %s", path);

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

			INF("Load library %s", mypath);
			dll = LoadLibraryA(mypath);
			if (dll) goto have_dll;

			vpath = strtok (NULL, ":");
		}
	}

	ERR("Can't load library: %s", base);
	return NULL;

have_dll: ;
/* Wine path to library
	char buf[PATH_MAX];
	GetModuleFileName(dll, (LPSTR) &buf, sizeof(buf));
	INF("GetModuleFileName: %s", buf);
*/
	main_entry_t main_entry = fst_get_main_entry(dll);
	if (! main_entry) {	
		FreeLibrary (dll);
		return NULL;
	}

	char* fullpath = realpath(mypath,NULL);
	if (! fullpath) {
		ERR("Can't get realpath for %s", mypath);
		FreeLibrary (dll);
		return NULL;
	}

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
	INF("Unload library: %s", fhandle->path);
	FreeLibrary (fhandle->dll);
	free (fhandle->path);
	free (fhandle->name);
	free (fhandle);

	return true;
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

			if (fst->editor_popup) ERR("Receive WM_CLOSE - WTF ?");

			if ( fst->window_close_cb ) fst->window_close_cb ( fst->window_close_cb_data );
		}
		break;
	case WM_NCDESTROY:
	case WM_DESTROY:
//		DEBUG("Get destroy %d", w);
		break;
	default:
		break;
	}

	return DefWindowProcA (w, msg, wp, lp );
}

static bool
register_window_class() {
	if ( WindowClassRegistered ) return true;

	HMODULE hInst;
	if ((hInst = GetModuleHandleA (NULL)) == NULL) {
		ERR ("can't get module handle");
		return false;
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
		ERR( "Class register failed :(" );
		return false;
	}
	WindowClassRegistered = true;

	return true;
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
		ERR("no window to show");
		return false;
	}
	
	fst_resize_editor(fst);
	ShowWindowAsync(fst->window, SW_SHOWNORMAL);
//	UpdateWindow(fst->window);

	return true;
}

/*************************** Event call internal routines ********************************/
static void fst_event_call (FST *fst, FSTCall* call) {
	/* Call directly */
	if (GetCurrentThreadId() == fst->thread->id) {
		fst_event_handler ( fst, call );
	} else { /* Call from Event thread */
		FSTEventCall* ec = &( fst->event_call );
		pthread_mutex_lock (&ec->lock);
		fst_lock(fst);

		ec->call = call;

		/* hurry thread, but note that some plugins don't like fast PC */
		if ( call->type != PROGRAM_CHANGE )
			PostThreadMessage( fst->thread->id, WM_USER, 0, 0 );

		pthread_cond_wait (&ec->called, &fst->lock);
		fst_unlock(fst);
		pthread_mutex_unlock ( &ec->lock );
	}
}

static bool fst_init ( FST* fst ) {
	AEffect* plugin = fst->handle->main_entry ( amc_callback );
	if (plugin == NULL)  {
		ERR ("%s could not be instantiated", fst->handle->name);
		return false;
	}

	if (plugin->magic != kEffectMagic) {
		ERR ("%s is not a VST plugin", fst->handle->name);
		return false;
	}

	fst->plugin = plugin;
	fst->plugin->resvd1 = (intptr_t*) &( fst->amc );

	return true;
}

static void fst_open_int (FST* fst) {
	AEffect* plugin = fst->plugin;
	fst->plugin = plugin;
	fst->plugin->resvd1 = (intptr_t*) &( fst->amc );

	plugin->dispatcher(plugin, effOpen, 0, 0, NULL, 0.0f);

	fst->vst_version = (int) plugin->dispatcher (plugin, effGetVstVersion, 0, 0, NULL, 0.0f);
	if (fst->vst_version >= 2) {
		fst->canReceiveVstEvents = fst_canDo(fst, "receiveVstEvents");
		fst->canReceiveVstMidiEvent = fst_canDo(fst, "receiveVstMidiEvent");
		fst->canSendVstEvents = fst_canDo(fst, "sendVstEvents");
		fst->canSendVstMidiEvent = fst_canDo(fst, "sendVstMidiEvent");

		fst->isSynth = (plugin->flags & effFlagsIsSynth);
		INF("%-31s : %s", "Plugin isSynth", fst->isSynth ? "Yes" : "No");

		bool pr = (plugin->flags & effFlagsCanReplacing);
		INF("%-31s : %s", "Support processReplacing", pr ? "Yes" : "No");

		/* Get plugin name */
		char tmpstr[kVstMaxEffectNameLen];
		if ( plugin->dispatcher (plugin, effGetEffectName, 0, 0, tmpstr, 0 ) )
			fst->name = strdup ( tmpstr );
	}

	// Always need some name ;-)
	if (! fst->name) fst->name = strdup ( fst->handle->name );
}

/*************************** Public (thread-safe) routines *****************************************/
void fst_call (FST *fst, FSTEventTypes type) {
	FSTCall call = { .type = type };
	fst_event_call ( fst, &call );
}

void fst_editor_resize ( FST* fst, int32_t width, int32_t height ) {
	FSTCall call = {
		.type = EDITOR_RESIZE,
		.width = width,
		.height = height
	};

	fst_event_call ( fst, &call );
}

int32_t fst_get_program (FST *fst) {
	/* Wait for change */
	fst_lock(fst);
	int32_t program = fst->current_program;
	fst_unlock(fst);
	return program;
}

void fst_set_program (FST *fst, int32_t program) {
	if (fst_get_program(fst) == program) {
		ERR("Program (%d) already set by someone else. Skip.", program);
		return;	
	}

	FSTCall call = {
		.type = PROGRAM_CHANGE,
		.program = program
	};
	fst_event_call ( fst, &call );
}

void fst_configure (FST *fst, float sample_rate, intptr_t block_size) {
	// Don't call plugin for same values
	AMC* amc = &fst->amc;
	if ( amc->block_size == block_size
	  && amc->sample_rate == sample_rate
	) {
		DEBUG("FST: skip configure for same values BS: %d SR: %g", block_size, sample_rate);
		return;
	}

	FSTCall call = {
		.type = CONFIGURE,
		.block_size = block_size,
		.sample_rate = sample_rate
	};

	fst_event_call ( fst, &call );
}

intptr_t fst_call_dispatcher (FST *fst, int32_t opcode, int32_t index, 
			intptr_t val, void *ptr, float opt )
{
	FSTCall call = {
		.type = DISPATCHER,
		.opcode = opcode,
		.index = index,
		.val = val,
		.ptr = ptr,
		.opt = opt
	};
	fst_event_call ( fst, &call );

	return call.retval;
}

bool fst_run_editor (FST* fst, bool popup) {
	if (fst->window) return false;

	fst->editor_popup = popup;
	fst_call (fst, EDITOR_OPEN);

	// Check is we really created some window ;-)
	if (!fst->window) {
		ERR ("no window created for VST plugin editor");
		return false;
	} else {
		return true;
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

void fst_get_program_name (FST *fst, int32_t program, char* name, size_t size) {
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
}

bool fst_set_program_name (FST *fst, const char* name) {
	char nname[kVstMaxProgNameLen + 1];
	strncpy ( nname, name, sizeof(nname) - 1 );
	valid_program_name ( nname, sizeof nname );

	fst_call_dispatcher(fst, effSetProgramName, 0, 0, nname, 0.0f);

	return true;
}

static bool fst_thread_add (FST_THREAD* th, FST* fst);
static void fst_thread_remove (FST* fst);

FST* fst_open (FSTHandle* fhandle, FST_THREAD* th) {
	INF("Revive plugin: %s", fhandle->name);

	FST* fst = fst_new ();
	fst->handle = fhandle;
	fst_thread_add( th, fst );
	fst_call(fst, INIT);

	if ( ! fst->initialized ) {
		fst_thread_remove(fst);
		fst_unload(fst->handle);
		free(fst);
		return NULL;
	}

	fst_call( fst, OPEN );

	return fst;
}

FST* fst_load_open ( const char* path, FST_THREAD* th ) {
	if ( ! path ) {
		ERR ( "empty plugin path ?" );
		return NULL;
	}

	if ( ! th ) {
		/* Adopt this thread as "fake" thread */
		th = fst_thread_new( "Fake", true );
		if ( ! th ) return NULL;
	}

	// Load plugin library
	FSTHandle* handle = fst_load(path);
	if (! handle) return NULL;

	// Revive plugin
	FST* fst = fst_open(handle, th);
	if (! fst) return NULL;

	return fst;
}

void fst_close (FST* fst) {
	fst_call ( fst, CLOSE );	
	fst_thread_remove(fst);

	fst_unload(fst->handle);
	free(fst->name);
	free(fst);
}

/*************************** Event handler routines *****************************************/
static bool fst_create_editor (FST* fst) {
	AEffect* plugin = fst->plugin;

	/* "guard point" to trap errors that occur during plugin loading */
	if (!(plugin->flags & effFlagsHasEditor)) {
		ERR ("Plugin \"%s\" has no editor", fst->handle->name);
		return false;
	}

	HMODULE hInst;
	if ((hInst = GetModuleHandleA (NULL)) == NULL) {
		ERR ("can't get module handle");
		return false;
	}

	if ( ! register_window_class() )
		return false;

	HWND window;
	if ((window = CreateWindowA ("FST", fst->handle->name, (fst->editor_popup) ? 
		(WS_POPUP & ~WS_TABSTOP) :
//		(WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_CAPTION) :
		(WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hInst, NULL)) == NULL)
	{
		ERR ("cannot create editor window");
		return false;
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
		ERR ("cannot set GUI window property");

	if (fst->editor_popup) {
		SetWindowPos (window, 0, 0, 0, 0, 0, SWP_SHOWWINDOW|SWP_NOMOVE|SWP_NOOWNERZORDER|
			SWP_NOREDRAW|SWP_NOCOPYBITS|SWP_DEFERERASE|SWP_NOZORDER|SWP_STATECHANGED);
		UpdateWindow(window);
	} else {
		fst_show_editor(fst);
	}

	fst->xid = GetPropA (window, "__wine_x11_whole_window");
//	ERR("And xid = %p", fst->xid );

	return true;
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
	INF("Suspend plugin");
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
		INF("Program: %d : %s", newProg, progName);
	}
}

static inline void fst_plugin_idle ( FST* fst ) {
	if ( fst->amc.need_idle )
		fst->plugin->dispatcher (fst->plugin, effIdle, 0, 0, NULL, 0);  

	if ( fst_has_window(fst) )
		fst->plugin->dispatcher (fst->plugin, effEditIdle, 0, 0, NULL, 0);
}

static void fst_event_handler (FST* fst, FSTCall* call) {
	DEBUG("FST Event: %d", call->type);

	AEffect* plugin = fst->plugin;
	AMC* amc = &fst->amc;

	if ( fst->initialized ) {
		if ( ! fst->opened && call->type != OPEN ) {
			ERR("Not opened yet !");
			return;
		}
	} else if ( call->type != INIT ) {
		ERR("Not initialized yet !");
		return;
	}

	fst_lock(fst);
	switch ( call->type ) {
	case INIT:
		call->retval = fst_init(fst);
		fst->initialized = true;
		break;
	case OPEN:
		fst_open_int(fst);
		fst->opened = true;
		break;
	case CLOSE:
		INF("Closing plugin: %s", fst->name);
		fst_suspend(fst);
		fst_destroy_editor (fst);
		plugin->dispatcher(plugin, effClose, 0, 0, NULL, 0.0f);
		fst->opened = false;
		INF("Plugin closed");
		break;
	case SUSPEND:
		fst_suspend (fst);
		break;
	case RESUME:
		INF("Resume plugin");
		fst_process_lock ( fst );
		plugin->dispatcher (plugin, effMainsChanged, 0, 1, NULL, 0.0f);
		plugin->dispatcher (plugin, effStartProcess, 0, 0, NULL, 0.0f);
		fst_process_unlock ( fst );
		break;
	case CONFIGURE:
		amc->block_size = call->block_size;
		amc->sample_rate = call->sample_rate;
		INF("Sample Rate: %g | Block Size: %d", amc->sample_rate, amc->block_size);
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
		fst->width = call->width;
		fst->height = call->height;
		fst_resize_editor(fst);
		break;
	case PROGRAM_CHANGE:
		plugin->dispatcher (plugin, effBeginSetProgram, 0, 0, NULL, 0);
		fst_process_lock ( fst );
		plugin->dispatcher (plugin, effSetProgram, 0, call->program, NULL, 0);

		// NOTE: some plugins needs effIdle call for update program
		fst_plugin_idle ( fst );

		fst_process_unlock ( fst );
		plugin->dispatcher (plugin, effEndSetProgram, 0, 0, NULL, 0);

		fst_update_current_program ( fst );
		break;
	case DISPATCHER:
		DEBUG("Dispatcher %d",  call->opcode );
		call->retval = plugin->dispatcher( plugin, call->opcode, call->index, call->val, call->ptr, call->opt );
		break;
	}
	fst_unlock(fst);
}

/*************************** Thread support routines *****************************************/
void fst_show_thread_info ( const char* th_name ) {
	HANDLE* h_thread = GetCurrentThread();
        INF("%s Thread W32ID: %d | LWP: %d | W32 Class: %d | W32 Priority: %d",
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

void fst_set_idle_callback ( FST* fst, FSTIdleCallback f, void* ptr ) {
	// Lock thread for sure
	pthread_mutex_lock (&fst->thread->lock);
	fst->idle_cb = f;
	fst->idle_cb_data = ptr;
	pthread_mutex_unlock (&fst->thread->lock);
}

static void fst_event_dispatcher(FST_THREAD* th) {
	pthread_mutex_lock (&th->lock);
	FST* fst;
	for (fst = th->first; fst; fst = fst->next) {
		if ( fst->opened ) {
			if ( fst->idle_cb )
				fst->idle_cb( fst->idle_cb_data );

			fst_plugin_idle ( fst );
			fst_update_current_program ( fst );
		}

		FSTEventCall* ec = &( fst->event_call );
		if ( ec->call != NULL ) { // We got call
			fst_event_handler(fst, ec->call);
			ec->call = NULL;
			pthread_cond_signal (&ec->called);
		}
	}
	pthread_mutex_unlock (&th->lock);
}

static DWORD WINAPI
fst_event_thread ( LPVOID lpParam ) {
	FST_THREAD* th = (FST_THREAD*) lpParam;

	fst_set_thread_priority ( th->name, ABOVE_NORMAL_PRIORITY_CLASS, THREAD_PRIORITY_ABOVE_NORMAL );

	register_window_class();

	if (!SetTimer (NULL, 1000, 30, NULL)) {
		ERR ("cannot set timer on dummy window");
		return 1;
	}

 	MSG msg;
	while (GetMessageA (&msg, NULL, 0,0) != 0) {
 		TranslateMessage(&msg);
 		DispatchMessageA(&msg);
		if ( msg.message != WM_TIMER
		  && msg.message != WM_USER
		) continue;

		fst_event_dispatcher(th);
	}

	INF( "FST THREAD: THE END" );

	return 0;
}

FST_THREAD* fst_thread_new( const char* name, bool fake ) {
	FST_THREAD* th = malloc( sizeof(FST_THREAD) );
	if ( ! th ) {
		ERR ( "Can't create thread (1)" );
		return NULL;
	}

	pthread_mutex_init (&th->lock, NULL);
	th->fake = fake;
	th->first = NULL;
	snprintf( th->name, sizeof(th->name), "%s", name );

	if ( fake ) {
		th->handle = GetCurrentThread();
		th->id = GetCurrentThreadId();
	} else {
		th->handle = CreateThread( NULL, 0, fst_event_thread, th, 0, &(th->id) );
	}
	
	if ( ! th->handle ) {
		free(th);
		ERR ( "Can't create thread (2)" );
		return NULL;
	}

	return th;
}

static bool
fst_thread_add (FST_THREAD* th, FST* fst) {
	pthread_mutex_lock (&th->lock);
	fst->thread = th;
	if ( th->first == NULL ) {
		th->first = fst;
	} else {
		FST* p = th->first;
		while (p->next) p = p->next;
		p->next = fst;
	}
	pthread_mutex_unlock (&th->lock);
	return true;
}

static void
fst_thread_remove (FST* fst) {
	FST_THREAD* th = fst->thread;
	FST* p;
	FST* prev;

	pthread_mutex_lock (&th->lock);
	fst->thread = NULL;
	for (p = th->first, prev = NULL; p->next; prev = p, p = p->next) {
		if (p == fst && prev)
			prev->next = p->next;
	}

	if (th->first == fst) {
		if (th->first->next) {
			th->first = th->first->next;
		} else {
			th->first = NULL;
			if ( ! th->fake ) {
				PostQuitMessage(0);
				INF ( "Waiting for end of thread" );
				WaitForSingleObject( th->handle, 0 );
			}
			pthread_mutex_unlock (&th->lock);
			return;
		}
	}
	pthread_mutex_unlock (&th->lock);
}

/************************** Unused ***************************************/
#if 0
bool fst_event_callback() {
	MSG msg;
	while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE) != 0) {
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
		if (msg.message == WM_QUIT)
			return false;
	}
	
	fst_event_dispatcher(); /* FIXME: thread ?? */

	return true;
}
#endif
