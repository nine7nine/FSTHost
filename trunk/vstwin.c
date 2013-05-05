#include <string.h>
#include <libgen.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>

#include "fst.h"

static FST* fst_first = NULL;
static int MainThreadId;
static bool WindowClassRegistered = FALSE;

static FST* 
fst_new () {
	FST* fst = calloc (1, sizeof (FST));

	pthread_mutex_init (&fst->lock, NULL);
	pthread_mutex_init (&fst->event_call_lock, NULL);
	pthread_cond_init (&fst->event_called, NULL);
	fst->want_program = -1;
	//fst->current_program = 0; - calloc done this
	fst->event_call = RESET;
//	fst->editor_popup = TRUE;

	return fst;
}

/* Plugin "canDo" helper function to neaten up plugin feature detection calls */
static bool
fst_canDo(FST* fst, char* feature) {
	bool can;
	can = (fst->plugin->dispatcher(fst->plugin, effCanDo, 0, 0, (void*)feature, 0.0f) > 0);
	printf("Plugin can %-20s : %s\n", feature, ((can) ? "Yes" : "No"));
	return can;
}

static inline void
fst_update_current_program(FST* fst) {
	short newProg;
	char progName[24];

	newProg = fst->plugin->dispatcher( fst->plugin, effGetProgram, 0, 0, NULL, 0.0f );
	if (newProg != fst->current_program || fst->program_changed) {
		fst->program_changed = FALSE;
		fst->current_program = newProg;
		fst_get_program_name(fst, fst->current_program, progName, sizeof(progName));
		printf("Program: %d : %s\n", newProg, progName);
	}
}

static LRESULT WINAPI 
my_window_proc (HWND w, UINT msg, WPARAM wp, LPARAM lp) {
	FST* fst = GetPropA(w, "FST");

	switch (msg) {
	case WM_KEYUP:
	case WM_KEYDOWN:
		break;

	case WM_CLOSE:
		if (fst && ! fst->editor_popup) {
			fst->plugin->dispatcher(fst->plugin, effEditClose, 0, 0, NULL, 0.0f);
			fst->window = NULL;
		} else {
			printf("Receive WM_CLOSE - WTF ?\n");
		}
		break;
	case WM_NCDESTROY:
	case WM_DESTROY:
//		printf("Get destroy %d\n", w);
		break;
	default:
		break;
	}

	return DefWindowProcA (w, msg, wp, lp );
}

static bool
register_window_class() {
	WNDCLASSEX wclass;
	HMODULE hInst;

	if ((hInst = GetModuleHandleA (NULL)) == NULL) {
		fst_error ("can't get module handle");
		return FALSE;
	}


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
		printf( "Class register failed :(\n" );
		return FALSE;
	}
	WindowClassRegistered = TRUE;

	return TRUE;
}

static void fst_resize_editor (FST *fst) {
	SetWindowPos(fst->window, HWND_BOTTOM, 0, 0, fst->width, fst->height, SWP_STATECHANGED|
		SWP_ASYNCWINDOWPOS|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_DEFERERASE);
}

bool
fst_show_editor (FST *fst) {
	if (!fst->window) {
		fst_error("no window to show");
		return FALSE;
	}
	
	fst_resize_editor(fst);
	ShowWindowAsync(fst->window, SW_SHOWNORMAL);
//	UpdateWindow(fst->window);

	return TRUE;
}

static bool
fst_create_editor (FST* fst) {
	HMODULE hInst;
	HWND window;
	struct ERect* er;

	/* "guard point" to trap errors that occur during plugin loading */
	if (!(fst->plugin->flags & effFlagsHasEditor)) {
		fst_error ("Plugin \"%s\" has no editor", fst->handle->name);
		return FALSE;
	}

	if ((hInst = GetModuleHandleA (NULL)) == NULL) {
		fst_error ("can't get module handle");
		return FALSE;
	}
	
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

	fst->plugin->dispatcher (fst->plugin, effEditOpen, 0, 0, window, 0);
	fst->plugin->dispatcher (fst->plugin, effEditGetRect, 0, 0, &er, 0 );

	fst->width  = er->right - er->left;
	fst->height = er->bottom - er->top;

	if (! fst->editor_popup) {
		// Add window title height and borders
		fst->height += 24;
		fst->width += 6;

		// Bind FST to window
		if (! SetPropA(window, "FST", fst))
                	fst_error ("cannot set GUI window property");
	}

	if (fst->editor_popup) {
		SetWindowPos (window, 0, 0, 0, 0, 0, SWP_SHOWWINDOW|SWP_NOMOVE|SWP_NOOWNERZORDER|
			SWP_NOREDRAW|SWP_NOCOPYBITS|SWP_DEFERERASE|SWP_NOZORDER|SWP_STATECHANGED);
		UpdateWindow(window);
	} else {
		fst_show_editor(fst);
	}
	

	fst->xid = GetPropA (window, "__wine_x11_whole_window");
	printf("And xid = %p\n", fst->xid );

	return TRUE;
}

static void
fst_event_call(FST *fst, enum EventCall type) {
	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);
		
	fst->event_call = type;
	pthread_cond_wait (&fst->event_called, &fst->lock);

	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);
}

void
fst_suspend (FST *fst) {
	if (GetCurrentThreadId() == MainThreadId) {
		fst_error("Suspend plugin");
		fst_call_dispatcher (fst, effStopProcess, 0, 0, NULL, 0.0f);
		fst_call_dispatcher (fst, effMainsChanged, 0, 0, NULL, 0.0f);
	} else {
		fst_event_call(fst, SUSPEND);
	}
} 

void
fst_resume (FST *fst) {
	if (GetCurrentThreadId() == MainThreadId) {
		fst_error("Resume plugin");
		fst_call_dispatcher (fst, effMainsChanged, 0, 1, NULL, 0.0f);
		fst_call_dispatcher (fst, effStartProcess, 0, 0, NULL, 0.0f);
	} else {
		fst_event_call(fst, RESUME);
	}
} 

bool fst_run_editor (FST* fst, bool popup) {
	if (fst->window) return FALSE;

	if ( ! WindowClassRegistered && ! register_window_class() )
		return FALSE;

	fst->editor_popup = popup;
	if (GetCurrentThreadId() == MainThreadId) {
		pthread_mutex_lock (&fst->lock);

		fst_create_editor(fst);

		pthread_mutex_unlock (&fst->lock);
	} else {
		fst_event_call(fst, EDITOR_OPEN);
	}

	// Check is we really create some window ;-)
	if (!fst->window) {
		fst_error ("no window created for VST plugin editor");
		return FALSE;
	} else {
		return TRUE;
	}
}

bool
fst_get_program_name (FST *fst, short program, char* name, size_t size) {
	char *m = NULL, *c;
	AEffect* plugin = fst->plugin;

	if (program == fst->current_program) {
		plugin->dispatcher(plugin, effGetProgramName, 0, 0, name, 0.0f);
	} else {
		plugin->dispatcher(plugin, effGetProgramNameIndexed, program, 0, name, 0.0 );
	}

	// remove all non ascii signs
	for (c = name; (*c != 0) && (c - name) < size; c++) {
		if ( isprint(*c)) {
			if (m) {
				*m = *c;
				m = c;
			}
		} else if (!m) m = c;
	}
	// make sure of string terminator
	if (m) *m = 0; else *c = 0;

	return TRUE; 
}

void
fst_program_change (FST *fst, short want_program) {
	// if we in main thread then call directly
	if (GetCurrentThreadId() == MainThreadId) {
		char progName[24];

		pthread_mutex_lock (&fst->lock);

		fst->plugin->dispatcher (fst->plugin, effBeginSetProgram, 0, 0, NULL, 0);
		fst->plugin->dispatcher (fst->plugin, effSetProgram, 0,(int32_t) want_program, NULL, 0);
		fst->plugin->dispatcher (fst->plugin, effEndSetProgram, 0, 0, NULL, 0);
		fst->current_program = want_program;

		fst_get_program_name(fst, fst->current_program, progName, sizeof(progName));
		printf("Program: %d : %s\n", fst->current_program, progName);

		pthread_mutex_unlock (&fst->lock);
	} else {
		pthread_mutex_lock (&fst->event_call_lock);
		if (fst->current_program != want_program) {
			pthread_mutex_lock (&fst->lock);

			fst->want_program = want_program;
			fst->event_call = PROGRAM_CHANGE;
			pthread_cond_wait (&fst->event_called, &fst->lock);
			fst->want_program = -1;
			fst->program_changed = TRUE;

			pthread_mutex_lock (&fst->lock);
		}
		pthread_mutex_unlock (&fst->event_call_lock);
	}
}

int 
fst_call_dispatcher(FST *fst, int opcode, int index, int val, void *ptr, float opt ) {
	int retval;

	// if we in main thread then call directly
	if (GetCurrentThreadId() == MainThreadId) {
		retval = fst->plugin->dispatcher( fst->plugin, opcode, index, val, ptr, opt );
	} else {
		struct FSTDispatcher dp;
	
		dp.opcode = opcode;
		dp.index = index;
		dp.val = val;
		dp.ptr = ptr;
		dp.opt = opt;

		pthread_mutex_lock (&fst->event_call_lock);
		pthread_mutex_lock (&fst->lock);

		fst->dispatcher = &dp;
		fst->event_call = DISPATCHER;

		pthread_cond_wait (&fst->event_called, &fst->lock);
		fst->dispatcher = NULL;

		pthread_mutex_unlock (&fst->lock);
		pthread_mutex_unlock (&fst->event_call_lock);

		retval = dp.retval;
	}

	return retval;
}

void fst_destroy_editor (FST* fst) {
	if (!fst->window) return;

	if (GetCurrentThreadId() == MainThreadId) {
		fst->plugin->dispatcher(fst->plugin, effEditClose, 0, 0, NULL, 0.0f);
		DestroyWindow(fst->window);
		fst->window = NULL;
	} else {
		fst_event_call(fst, EDITOR_CLOSE);
	}
}

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

FSTHandle*
fst_load (const char *path) {
	char mypath[PATH_MAX];
	size_t mypath_maxchars = sizeof(mypath) - 1;

	printf("Load library %s\n", path);

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

			printf("Load library %s\n", mypath);
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
	printf("GetModuleFileName: %s\n", buf);
*/

	main_entry_t main_entry = fst_get_main_entry(dll);
	if (! main_entry) {	
		FreeLibrary (dll);
		return NULL;
	}

	char* fullpath = realpath(mypath,NULL);
	if (! fullpath) fullpath = strndup(mypath, sizeof(mypath));

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

bool
fst_unload (FSTHandle* fhandle) {
	printf("Unload library: %s\n", fhandle->path);
	FreeLibrary (fhandle->dll);
	free (fhandle->path);
	free (fhandle->name);
	free (fhandle);

	return TRUE;
}

FST*
fst_open (FSTHandle* fhandle, audioMasterCallback amc, void* userptr) {
	if (fhandle == NULL) {
	    fst_error( "the handle was NULL" );
	    return NULL;
	}
	printf("Revive plugin: %s\n", fhandle->name);

	AEffect* plugin = fhandle->main_entry (amc);
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
	fst->plugin->resvd1 = userptr;

	// Open Plugin
	plugin->dispatcher (plugin, effOpen, 0, 0, NULL, 0.0f);
	fst->vst_version = plugin->dispatcher (plugin, effGetVstVersion, 0, 0, NULL, 0.0f);

	if (fst->vst_version >= 2) {
		fst->canReceiveVstEvents = fst_canDo(fst, "receiveVstEvents");
		fst->canReceiveVstMidiEvent = fst_canDo(fst, "receiveVstMidiEvent");
		fst->canSendVstEvents = fst_canDo(fst, "sendVstEvents");
		fst->canSendVstMidiEvent = fst_canDo(fst, "sendVstMidiEvent");

		fst->isSynth = (plugin->flags & effFlagsIsSynth) > 0;
		printf("%-31s : %s\n", "Plugin isSynth", fst->isSynth ? "Yes" : "No");

		/* Get plugin name */
		char tmpstr[32];
		if ( plugin->dispatcher (plugin, effGetEffectName, 0, 0, tmpstr, 0 ) )
			fst->name = strndup ( tmpstr, sizeof(tmpstr) );
	}

	// We always need some name ;-)
	if (! fst->name) fst->name = strdup ( fst->handle->name );

	// Bind to plugin list
	fst_event_loop_add_plugin(fst);

	MainThreadId = GetCurrentThreadId();

	return fst;
}

FST*
fst_load_open (const char* path, audioMasterCallback amc, void* userptr) {
	FSTHandle* handle = fst_load(path);
	if (! handle) return NULL;

	// Revive plugin
	FST* fst = fst_open(handle, amc, userptr);
	if (! fst) return NULL;

	return fst;
}

void
fst_close (FST* fst) {
	printf("Close plugin: %s\n", fst->name);

	// It's matter from which thread we calling it
	if (GetCurrentThreadId() == MainThreadId) {
		pthread_mutex_lock (&fst->lock);

		fst_suspend(fst);
		fst_destroy_editor (fst);

		fst->plugin->dispatcher(fst->plugin, effClose, 0, 0, NULL, 0.0f);
		fst_event_loop_remove_plugin(fst);

		pthread_mutex_unlock (&fst->lock);
		fst_unload(fst->handle);
		free(fst->name);
		free(fst);

		printf("Plugin closed\n");
	} else {
		// Try call from event_loop thread
		pthread_mutex_lock (&fst->event_call_lock);
		pthread_mutex_lock (&fst->lock);
		fst->event_call = CLOSE;
		pthread_cond_wait (&fst->event_called, &fst->lock);
		pthread_mutex_unlock (&fst->lock);
		pthread_mutex_unlock (&fst->event_call_lock);
	}
}

static inline void
fst_event_handler(FST* fst) {
	AEffect* plugin = fst->plugin;
	struct FSTDispatcher* dp = fst->dispatcher;

	switch (fst->event_call) {
	case CLOSE:
		fst_close(fst);
		break;
	case SUSPEND:
		fst_suspend(fst);
		break;
	case RESUME:
		fst_resume(fst);
		break;
	case EDITOR_OPEN:
		fst_run_editor(fst, fst->editor_popup);
		break;
	case EDITOR_CLOSE:
		fst_destroy_editor(fst);
		break;
	case PROGRAM_CHANGE:
		fst_program_change(fst, fst->want_program);
		break;
	case DISPATCHER:
		dp->retval = plugin->dispatcher( plugin, dp->opcode, dp->index, dp->val, dp->ptr, dp->opt );
		break;
	case RESET:
		break;
	}
	fst->event_call = RESET;
	pthread_cond_signal (&fst->event_called);
}

static void fst_event_dispatcher() {
	FST* fst;
	for (fst = fst_first; fst; fst = fst->next) {
		if (fst->wantIdle)
			fst->plugin->dispatcher (fst->plugin, effIdle, 0, 0, NULL, 0);  

		if (fst->window) {
			fst->plugin->dispatcher (fst->plugin, effEditIdle, 0, 0, NULL, 0);
			if (fst->wantResize) {
				fst->wantResize = FALSE;
				fst_resize_editor(fst);
			}
		}

		fst_update_current_program(fst);

		if (fst->event_call != RESET)
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

// ----------- NOT USED ----------------
void fst_event_loop (HMODULE hInst) {
 	MSG msg;
	FST* fst;
	//DWORD gui_thread_id;
	HANDLE* h_thread = GetCurrentThread ();

	register_window_class(hInst);
	//gui_thread_id = GetCurrentThreadId ();

	//SetPriorityClass ( h_thread, REALTIME_PRIORITY_CLASS);
	SetPriorityClass ( h_thread, ABOVE_NORMAL_PRIORITY_CLASS);
	//SetThreadPriority ( h_thread, THREAD_PRIORITY_TIME_CRITICAL);
	SetThreadPriority ( h_thread, THREAD_PRIORITY_ABOVE_NORMAL);
	printf ("W32 GUI EVENT Thread Class: %d\n", GetPriorityClass (h_thread));
	printf ("W32 GUI EVENT Thread Priority: %d\n", GetThreadPriority(h_thread));

	if (!SetTimer (NULL, 1000, 100, NULL)) {
		fst_error ("cannot set timer on dummy window");
		return;
	}

	while (GetMessageA (&msg, NULL, 0,0) != 0) {
 		TranslateMessage(&msg);
 		DispatchMessageA(&msg);
		if ( msg.message != WM_TIMER )
			continue;
		for (fst = fst_first; fst; fst = fst->next) {
			if (fst->wantIdle)
				fst->plugin->dispatcher (fst->plugin, effIdle, 0, 0, NULL, 0);  

			if (fst->window)
				fst->plugin->dispatcher (fst->plugin, effEditIdle, 0, 0, NULL, 0);

			fst_update_current_program(fst);

			if (fst->event_call != RESET)
				fst_event_handler(fst);
		}
	}
 
	printf( "GUI EVENT LOOP: THE END\n" );
}
