#include <libgen.h>
#include <signal.h>

#include "fst.h"

static FST* fst_first = NULL;
int MainThreadId;

static LRESULT WINAPI 
my_window_proc (HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	FST* fst=NULL;
	LRESULT result;

	switch (msg) {
	case WM_KEYUP:
	case WM_KEYDOWN:
		break;

	case WM_CLOSE:
		printf("Receive WM_CLOSE - WTF ?\n");
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

static FST* 
fst_new ()
{
	FST* fst = (FST*) calloc (1, sizeof (FST));

	pthread_mutex_init (&fst->lock, NULL);
	pthread_mutex_init (&fst->event_call_lock, NULL);
	pthread_cond_init (&fst->event_called, NULL);
	fst->want_program = -1;
	//fst->current_program = 0; - calloc done this
	fst->event_call = RESET;

	return fst;
}

/* Plugin "canDo" helper function to neaten up plugin feature detection calls */
static bool
fst_canDo(FST* fst, char* feature)
{
	bool can;
	can = (fst->plugin->dispatcher(fst->plugin, effCanDo, 0, 0, (void*)feature, 0.0f) > 0);
	printf("Plugin can %s : %s\n", feature, ((can) ? "Yes" : "No"));
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

bool
fst_run_editor (FST* fst)
{
	/* wait for the plugin editor window to be created (or not) */
	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);

	if (!fst->window) {
		fst->event_call = EDITOR_OPEN;
		pthread_cond_wait (&fst->event_called, &fst->lock);
	}

	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);

	if (!fst->window) {
		fst_error ("no window created for VST plugin editor");
		return FALSE;
	}

	return TRUE;
}

bool
fst_show_editor (FST *fst) {
	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);

	if (fst->window) {
		fst->event_call = EDITOR_SHOW;
		pthread_cond_wait (&fst->event_called, &fst->lock);
	}

	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);

	return TRUE;
}

bool
fst_get_program_name (FST *fst, short program, char* name, size_t size)
{
	char *m = NULL, *c;
	struct AEffect* plugin = fst->plugin;

	if (program == fst->current_program) {
		plugin->dispatcher(plugin, effGetProgramName, 0, 0, name, 0.0f);
	} else {
		plugin->dispatcher(plugin, effGetProgramNameIndexed, program, 0, name, 0.0 );
	}

	// remove all non ascii signs
	for (c = name; (*c != 0) && (c - name) < size; c++) {
		if ( isprint(*c)) {
			if (m != NULL) {
				*m = *c;
				m = c;
			}
		} else if (m == NULL) m = c;
	}
	// make sure of string terminator
	if (m != NULL) *m = 0; else *c = 0;

	return TRUE; 
}

void
fst_program_change (FST *fst, short want_program)
{
	// if we in main thread then call directly
	if (GetCurrentThreadId() == MainThreadId) {
		char progName[24];

		fst->plugin->dispatcher (fst->plugin, effBeginSetProgram, 0, 0, NULL, 0);
		fst->plugin->dispatcher (fst->plugin, effSetProgram, 0,(int32_t) want_program, NULL, 0);
		fst->plugin->dispatcher (fst->plugin, effEndSetProgram, 0, 0, NULL, 0);
		fst->current_program = want_program;
		fst->want_program = -1;

		fst_get_program_name(fst, fst->current_program, progName, sizeof(progName));
		printf("Program: %d : %s\n", fst->current_program, progName);

		return;
	}

	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);

	if (fst->current_program != want_program) {
		fst->want_program = want_program;
		fst->event_call = PROGRAM_CHANGE;

		pthread_cond_wait (&fst->event_called, &fst->lock);
	}

	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);
}

int 
fst_call_dispatcher(FST *fst, int opcode, int index, int val, void *ptr, float opt )
{
	int retval;

	// if we in main thread then call directly
	if (GetCurrentThreadId() == MainThreadId) {
		retval = fst->plugin->dispatcher( fst->plugin, opcode, index, val, ptr, opt );
	} else {
		struct FSTDispatcher dp;

		pthread_mutex_lock (&fst->event_call_lock);
		pthread_mutex_lock (&fst->lock);

		dp.opcode = opcode;
		dp.index = index;
		dp.val = val;
		dp.ptr = ptr;
		dp.opt = opt;
		fst->dispatcher = &dp;
		fst->event_call = DISPATCHER;

		pthread_cond_wait (&fst->event_called, &fst->lock);

		pthread_mutex_unlock (&fst->lock);
		pthread_mutex_unlock (&fst->event_call_lock);
		retval = dp.retval;
		fst->dispatcher = NULL;
	}

	return retval;
}

static bool
fst_create_editor (FST* fst)
{
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
	
	if ((window = CreateWindowA ("FST", fst->handle->name,
//		       (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_CAPTION),
			WS_POPUPWINDOW | WS_DISABLED | WS_MINIMIZE & ~WS_BORDER & ~WS_SYSMENU,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL, hInst, NULL)) == NULL)
	{
		fst_error ("cannot create editor window");
		return FALSE;
	}
	fst->window = window;

	fst->plugin->dispatcher (fst->plugin, effEditOpen, 0, 0, fst->window, 0);
	fst->plugin->dispatcher (fst->plugin, effEditGetRect, 0, 0, &er, 0 );

	fst->width  = er->right - er->left;
	fst->height = er->bottom - er->top;

	SetWindowPos (fst->window, HWND_BOTTOM, 0, 0, fst->width, fst->height, 
		SWP_NOACTIVATE | SWP_NOMOVE | SWP_DEFERERASE | SWP_NOCOPYBITS | SWP_NOSENDCHANGING);

	ShowWindow (fst->window, SW_SHOWMINNOACTIVE);
//	ShowWindow (fst->window, SW_SHOWNA);

	fst->xid = (int) GetPropA (window, "__wine_x11_whole_window");
	printf( "And xid = %x\n", fst->xid );
	
	EnableWindow(fst->window, TRUE);
	UpdateWindow(fst->window);
	ShowWindow (fst->window, SW_RESTORE);

	return TRUE;
}

void
fst_destroy_editor (FST* fst)
{
	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);
	if (fst->window) {
		fst->event_call = EDITOR_CLOSE;
		pthread_cond_wait (&fst->event_called, &fst->lock);
	}
	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);
}

void
fst_suspend (FST *fst) {
	fst_error("Suspend plugin");
	fst_call_dispatcher (fst, effStopProcess, 0, 0, NULL, 0.0f);
	fst_call_dispatcher (fst, effMainsChanged, 0, 0, NULL, 0.0f);
} 

void
fst_resume (FST *fst) {
	fst_error("Resume plugin");
	fst_call_dispatcher (fst, effMainsChanged, 0, 1, NULL, 0.0f);
	fst_call_dispatcher (fst, effStartProcess, 0, 0, NULL, 0.0f);
} 

static void
fst_event_loop_remove_plugin (FST* fst)
{
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
fst_event_loop_add_plugin (FST* fst)
{
	if (fst_first == NULL) {
		fst_first = fst;
	} else {
		FST* p = fst_first;
		while (p->next) p = p->next;
		p->next = fst;
	}
}

FSTHandle*
fst_load (const char * path)
{
	char mypath[MAX_PATH];
	char *base, *envdup, *vst_path, *last, *name;
	size_t len;
	HMODULE dll = NULL;
	main_entry_t main_entry;
	FSTHandle* fhandle;

	strcpy(mypath, path);
	if (strstr (path, ".dll") == NULL)
		strcat (mypath, ".dll");

	/* Get name */
	base = strdup(basename (mypath));
	len =  strrchr(base, '.') - base;
	name = strndup(base, len);

	// If we got full path
	dll = LoadLibraryA(mypath);

	// Try find plugin in VST_PATH
	if ( (dll == NULL) && (envdup = getenv("VST_PATH")) != NULL ) {
		envdup = strdup (envdup);
		vst_path = strtok (envdup, ":");
		while (vst_path != NULL) {
			last = vst_path + strlen(vst_path) - 1;
			if (*last == '/') *last='\0';

			sprintf(mypath, "%s/%s", vst_path, base);
			printf("Search in %s\n", mypath);

			if ( (dll = LoadLibraryA(mypath)) != NULL)
				break;
			vst_path = strtok (NULL, ":");
		}
		free(envdup);
		free(base);
	}

	if (dll == NULL) {
		free(name);
		fst_error("Can't load plugin\n");
		return NULL;
	}

	if ((main_entry = (main_entry_t) GetProcAddress (dll, "main")) == NULL) {
		free(name);
		FreeLibrary (dll);
		fst_error("Wrong main_entry\n");
		return NULL;
	}
	
	fhandle = calloc (1, sizeof (FSTHandle));
	fhandle->dll = dll;
	fhandle->main_entry = main_entry;
	fhandle->path = strdup(mypath);
	fhandle->name = name;
	fhandle->plugincnt = 0;
	
	return fhandle;
}

bool
fst_unload (FSTHandle* fhandle)
{
	// Some plugin use this library ?
	if (fhandle->plugincnt) {
		fst_error("Can't unload library %s because %d plugins still using it\n",
			fhandle->name, fhandle->plugincnt);
		return FALSE;
	}

	FreeLibrary (fhandle->dll);
	free (fhandle->path);
	free (fhandle->name);
	
	free (fhandle);

	return TRUE;
}

FST*
fst_open (FSTHandle* fhandle, audioMasterCallback amc, void* userptr)
{
	FST* fst = fst_new ();

	if( fhandle == NULL ) {
	    fst_error( "the handle was NULL\n" );
	    return NULL;
	}

	if ((fst->plugin = fhandle->main_entry (amc)) == NULL)  {
		fst_error ("%s could not be instantiated\n", fhandle->name);
		free (fst);
		return NULL;
	}

	fst->handle = fhandle;
	fst->plugin->resvd1 = userptr;

	if (fst->plugin->magic != kEffectMagic) {
		fst_error ("%s is not a VST plugin\n", fhandle->name);
		free (fst);
		return NULL;
	}

	// Open Plugin
	fst->plugin->dispatcher (fst->plugin, effOpen, 0, 0, NULL, 0.0f);
	fst->vst_version = fst->plugin->dispatcher (fst->plugin, effGetVstVersion, 0, 0, NULL, 0.0f);

	if (fst->vst_version >= 2) {
		fst->canReceiveVstEvents = fst_canDo(fst, "receiveVstEvents");
		fst->canReceiveVstMidiEvent = fst_canDo(fst, "receiveVstMidiEvent");
		fst->canSendVstEvents = fst_canDo(fst, "sendVstEvents");
		fst->canSendVstMidiEvent = fst_canDo(fst, "sendVstMidiEvent");

		fst->isSynth = (fst->plugin->flags & effFlagsIsSynth) > 0;
		printf("Plugin isSynth : %s\n", fst->isSynth ? "Yes" : "No");
	}

	++fst->handle->plugincnt;
	fst_event_loop_add_plugin(fst);

	MainThreadId = GetCurrentThreadId();

	return fst;
}

void
fst_close (FST* fst)
{
	fst_suspend(fst);
	fst_destroy_editor (fst);

	// It's matter from which thread we calling it
	if (GetCurrentThreadId() == MainThreadId) {
		fst->plugin->dispatcher(fst->plugin, effClose, 0, 0, NULL, 0.0f);
		--fst->handle->plugincnt;
	} else {
		// Try call from even_loop thread
		pthread_mutex_lock (&fst->event_call_lock);
		pthread_mutex_lock (&fst->lock);
		fst->event_call = CLOSE;
		pthread_cond_wait (&fst->event_called, &fst->lock);
		pthread_mutex_unlock (&fst->lock);
		pthread_mutex_unlock (&fst->event_call_lock);
	}
	free(fst);
	
	printf("Plugin closed\n");
}

static inline void
fst_event_handler(FST* fst) {
	pthread_mutex_lock (&fst->lock);
	struct AEffect* plugin = fst->plugin;
	struct FSTDispatcher* dp = fst->dispatcher;

	switch (fst->event_call) {
	case CLOSE:
		fst->plugin->dispatcher(fst->plugin, effClose, 0, 0, NULL, 0.0f);
		fst_event_loop_remove_plugin (fst);
		--fst->handle->plugincnt;
		break;
	case EDITOR_OPEN:		
		if (fst->window == NULL)
			fst_create_editor(fst);
		break;
	case EDITOR_SHOW:
		if (fst->window != NULL) {
			ShowWindow(fst->window, SW_RESTORE);
			EnableWindow(fst->window, TRUE);
			UpdateWindow(fst->window);
		}
		break;
	case EDITOR_CLOSE:
		if (fst->window != NULL) {
			EnableWindow(fst->window, FALSE);
			ShowWindow(fst->window, SW_HIDE);
			UpdateWindow(fst->window);
			plugin->dispatcher(plugin, effEditClose, 0, 0, NULL, 0.0f);
			DestroyWindow(fst->window);
			fst->window = FALSE;
		}
		break;
	case PROGRAM_CHANGE:
		plugin->dispatcher (plugin, effBeginSetProgram, 0, 0, NULL, 0);
		plugin->dispatcher (plugin, effSetProgram, 0,(int32_t) fst->want_program, NULL, 0);
		plugin->dispatcher (plugin, effEndSetProgram, 0, 0, NULL, 0);
		fst->current_program = fst->want_program;
		fst->program_changed = TRUE;
		fst->want_program = -1;
		break;
	case DISPATCHER:
		dp->retval = plugin->dispatcher( plugin, dp->opcode, dp->index, dp->val, dp->ptr, dp->opt );
		break;
	}
	fst->event_call = RESET;
	pthread_cond_signal (&fst->event_called);
	pthread_mutex_unlock (&fst->lock);
}

static bool
register_window_class (HMODULE hInst)
{
	WNDCLASSEX wclass;
	HANDLE t_thread; 

	wclass.cbSize = sizeof(WNDCLASSEX);
	wclass.style = 0;
//	wclass.style = (CS_HREDRAW | CS_VREDRAW);
	wclass.lpfnWndProc = my_window_proc;
	wclass.cbClsExtra = 0;
	wclass.cbWndExtra = 0;
	wclass.hInstance = hInst;
	wclass.hIcon = LoadIcon(hInst, "FST");
	wclass.hCursor = LoadCursor(0, IDI_APPLICATION);
//    wclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wclass.lpszMenuName = "MENU_FST";
	wclass.lpszClassName = "FST";
	wclass.hIconSm = 0;

	if (!RegisterClassExA(&wclass)){
		printf( "Class register failed :(\n" );
		return FALSE;
	}

	return TRUE;
}

void
fst_event_loop (HMODULE hInst)
{
	MSG msg;
	FST* fst;
	HWND dummy_window;
	//DWORD gui_thread_id;
	HANDLE* h_thread;

	register_window_class(hInst);
	//gui_thread_id = GetCurrentThreadId ();
	h_thread = GetCurrentThread ();

	//SetPriorityClass ( h_thread, REALTIME_PRIORITY_CLASS);
	SetPriorityClass ( h_thread, ABOVE_NORMAL_PRIORITY_CLASS);
	//SetThreadPriority ( h_thread, THREAD_PRIORITY_TIME_CRITICAL);
	SetThreadPriority ( h_thread, THREAD_PRIORITY_ABOVE_NORMAL);
	printf ("W32 GUI EVENT Thread Class: %d\n", GetPriorityClass (h_thread));
	printf ("W32 GUI EVENT Thread Priority: %d\n", GetThreadPriority(h_thread));

	if ((dummy_window = CreateWindowA ("FST", "dummy", WS_OVERLAPPEDWINDOW | WS_DISABLED,
		0, 0, 0, 0, NULL, NULL, hInst, NULL )) == NULL) {
		fst_error ("cannot create dummy timer window");
	}

	if (!SetTimer (dummy_window, 1000, 50, NULL)) {
		fst_error ("cannot set timer on dummy window");
		return;
	}

	while (GetMessageA (&msg, NULL, 0,0) != 0) {
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
		if ( msg.message != WM_TIMER || msg.hwnd != dummy_window )
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
