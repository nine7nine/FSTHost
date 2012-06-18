#include <libgen.h>
#include <windows.h>
#include <winnt.h>
#include <wine/exception.h>
#include <signal.h>

#include "fst.h"

#include <X11/X.h>
#include <X11/Xlib.h>

static FST* fst_first = NULL;

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
	if (newProg != fst->current_program) {
		fst->current_program = newProg;
		fst->plugin->dispatcher (fst->plugin, effGetProgramName, 0, 0, &progName, 0.0f);
		printf("Program: %d : %s\n", newProg, progName);
	}
}

static inline void
fst_event_handler(FST* fst) {
	pthread_mutex_lock (&fst->lock);
	struct AEffect* plugin = fst->plugin;

	switch (fst->event_call) {
	case EDITOR_OPEN:		
		if (fst->window == NULL && ! fst_create_editor (fst))
			fst_error ("cannot create editor for plugin %s", fst->handle->name);
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
			plugin->dispatcher( plugin, effEditClose, 0, 0, NULL, 0.0f );
			DestroyWindow(fst->window);
			fst->window = FALSE;
		}
		break;
	case PROGRAM_CHANGE:
		plugin->dispatcher (plugin, effBeginSetProgram, 0, 0, NULL, 0);
		plugin->dispatcher (plugin, effSetProgram, 0,(int32_t) fst->want_program, NULL, 0);
		plugin->dispatcher (plugin, effEndSetProgram, 0, 0, NULL, 0);
		
		fst->want_program = -1;
		break;
	case DISPATCHER:
		fst->dispatcher_retval = fst->plugin->dispatcher( plugin, 
			fst->dispatcher_opcode, fst->dispatcher_index, fst->dispatcher_val,
			fst->dispatcher_ptr, fst->dispatcher_opt );
		break;
	}

	fst->event_call = RESET;
	pthread_cond_signal (&fst->event_called);
	pthread_mutex_unlock (&fst->lock);
}

DWORD WINAPI
fst_event_loop (HMODULE hInst)
{
	MSG msg;
	FST* fst;
	HWND dummy_window;
	DWORD gui_thread_id;
	HANDLE* h_thread;

	register_window_class(hInst);
	gui_thread_id = GetCurrentThreadId ();
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

	if (!SetTimer (dummy_window, 1000, 100, NULL)) {
		fst_error ("cannot set timer on dummy window");
		return 1;
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

int
register_window_class (HMODULE hInst)
{
	WNDCLASSEX wclass;
	HANDLE t_thread; 
	//HMODULE hInst;

	//if ((hInst = GetModuleHandleA (NULL)) == NULL) {
	//	fst_error ("can't get module handle");
	//	return -1;
	//}
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
		return 0;
	}

	return 1;
}

int 
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

int
fst_show_editor (FST *fst) {
	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);

	if (fst->window) {
		fst->event_call = EDITOR_SHOW;
		pthread_cond_wait (&fst->event_called, &fst->lock);
	}

	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);

	return 0;
}

void
fst_program_change (FST *fst, short want_program)
{
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
	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);

	fst->dispatcher_opcode = opcode;
	fst->dispatcher_index = index;
	fst->dispatcher_val = val;
	fst->dispatcher_ptr = ptr;
	fst->dispatcher_opt = opt;
	fst->event_call = DISPATCHER;

	pthread_cond_wait (&fst->event_called, &fst->lock);

	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);

	return fst->dispatcher_retval;
}

int
fst_create_editor (FST* fst)
{
	HMODULE hInst;
	HWND window;
	struct ERect* er;

	/* "guard point" to trap errors that occur during plugin loading */

	/* Note: fst->lock is held while this function is called */

	if (!(fst->plugin->flags & effFlagsHasEditor)) {
		fst_error ("Plugin \"%s\" has no editor", fst->handle->name);
		return 0;
	}

	if ((hInst = GetModuleHandleA (NULL)) == NULL) {
		fst_error ("can't get module handle");
		return 0;
	}
	
	if ((window = CreateWindowA ("FST", fst->handle->name,
//		       (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_CAPTION),
			WS_POPUPWINDOW | WS_DISABLED | WS_MINIMIZE & ~WS_BORDER & ~WS_SYSMENU,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL, hInst, NULL)) == NULL)
	{
		fst_error ("cannot create editor window");
		return 0;
	}
	fst->window = window;

	fst->plugin->dispatcher (fst->plugin, effEditOpen, 0, 0, fst->window, 0);
	fst->plugin->dispatcher (fst->plugin, effEditGetRect, 0, 0, &er, 0 );

	fst->width  =  er->right - er->left;
	fst->height =  er->bottom - er->top;

	SetWindowPos (fst->window, HWND_BOTTOM, 0, 0, fst->width, fst->height, 
		SWP_NOACTIVATE | SWP_NOMOVE | SWP_DEFERERASE | SWP_NOCOPYBITS | SWP_NOSENDCHANGING);

	ShowWindow (fst->window, SW_SHOWMINNOACTIVE);
//	ShowWindow (fst->window, SW_SHOWNA);

	fst->xid = (int) GetPropA (window, "__wine_x11_whole_window");
	printf( "And xid = %x\n", fst->xid );
	
	EnableWindow(fst->window, TRUE);
	UpdateWindow(fst->window);
	ShowWindow (fst->window, SW_RESTORE);

	return 1;
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
	printf("Suspend plugin\n");
	fst->plugin->dispatcher (fst->plugin, effStopProcess, 0, 0, NULL, 0.0f);
	fst->plugin->dispatcher (fst->plugin, effMainsChanged, 0, 0, NULL, 0.0f);
} 

void
fst_resume (FST *fst) {
	printf("Resume plugin\n");
	fst->plugin->dispatcher (fst->plugin, effMainsChanged, 0, 1, NULL, 0.0f);
	fst->plugin->dispatcher (fst->plugin, effStartProcess, 0, 0, NULL, 0.0f);
} 

void
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

void
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
fst_load (const char * path) {
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
	
	fhandle = (FSTHandle*) calloc (1, sizeof (FSTHandle));
	fhandle->dll = dll;
	fhandle->main_entry = main_entry;
	fhandle->path = strdup(mypath);
	fhandle->name = name;
	
	return fhandle;
}

int
fst_unload (FSTHandle* fhandle)
{
	if (fhandle->plugincnt)
		return -1;

	if (fhandle->dll) {
		FreeLibrary (fhandle->dll);
		fhandle->dll = NULL;
	}

	if (fhandle->path) {
		free (fhandle->path);
		fhandle->name = NULL;
	}
	
	free (fhandle);
	return 0;
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
		fst->isSynth = (fst->plugin->flags & effFlagsIsSynth) > 0;
		fst->canReceiveVstEvents = fst_canDo(fst, "receiveVstEvents");
		fst->canReceiveVstMidiEvent = fst_canDo(fst, "receiveVstMidiEvent");
		fst->canSendVstEvents = fst_canDo(fst, "sendVstEvents");
		fst->canSendVstMidiEvent = fst_canDo(fst, "sendVstMidiEvent");
	}

	fst->handle->plugincnt++;
	fst_event_loop_add_plugin(fst);

	return fst;
}

int
fst_init ()
{
	if (CreateThread (NULL, 0, (LPTHREAD_START_ROUTINE) fst_event_loop, NULL, 0, NULL) == NULL) {
		fst_error ("could not create new thread proxy");
		return FALSE;
	}

	return TRUE;
}

void
fst_close (FST* fst)
{
	fst_suspend(fst);

	fst_destroy_editor (fst);

	fst->plugin->dispatcher(fst->plugin, effClose, 0, 0, NULL, 0.0f);

	fst_event_loop_remove_plugin (fst);

	if (fst->handle->plugincnt)
		--fst->handle->plugincnt;

	free(fst);
}
