#include <libgen.h>
#include <signal.h>
#include <windows.h>
#include <commctrl.h>

#include "fst.h"

#define TITLEBAR_HEIGHT 26
#define IDC_EDITOR_BUTTON 101
#define IDC_PROGRAM_COMBO 102
#define IDC_CHANNEL_COMBO 103

static FST* fst_first = NULL;
int MainThreadId;
HWND dummy_window;

bool fst_editor_open (FST* fst);
void fst_editor_close (FST* fst);
void fst_suspend (FST *fst);
static void fst_event_loop_remove_plugin (FST* fst);

static LRESULT WINAPI 
my_window_proc (HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	LRESULT result;
	
	FST* fst = GetPropA(w, "FST");

	switch (msg) {
//	case WM_KEYUP:
//	case WM_KEYDOWN:
//		break;
	case WM_CLOSE:
		if (fst && w == fst->gui->window) {
			fst_gui_close(fst);
			fst->quit = true;
		}
		break;
//	case WM_NCDESTROY:
//		break;
//	case WM_DESTROY:
//		break;
	case WM_COMMAND:
		switch(LOWORD(wp)) {
		case IDC_EDITOR_BUTTON: ;
			if (SendMessageA((HWND) lp,BM_GETSTATE,0,0) & BST_CHECKED) {
				fst_editor_open(fst);
			} else {
				fst_editor_close(fst);

				// Resize main window
				SetWindowPos(fst->gui->window,HWND_TOP, 0, 0, fst->gui->tb_width + 3, fst->gui->tb_height + TITLEBAR_HEIGHT,
					SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_SHOWWINDOW);
			}
			break;
		case IDC_PROGRAM_COMBO:
			if (HIWORD(wp) == CBN_SELCHANGE) {
				int program = SendMessageA((HWND) lp, CB_GETCURSEL, 0,0);
				fst_program_change(fst, program);
			}
			break;
		case IDC_CHANNEL_COMBO:
			if (HIWORD(wp) == CBN_SELCHANGE)
				fst->channel = SendMessageA((HWND) lp, CB_GETCURSEL, 0,0);
		}
		break;
	}
	return DefWindowProcA (w, msg, wp, lp );
}

static FST* fst_new() {
	FST* fst = (FST*) calloc (1, sizeof (FST));

	pthread_mutex_init (&fst->lock, NULL);
	pthread_mutex_init (&fst->event_call_lock, NULL);
	pthread_cond_init (&fst->event_called, NULL);
	fst->want_program = -1;
	//fst->current_program = 0; - calloc done this
	fst->event_call = FST_RESET;

	return fst;
}

/* Plugin "canDo" helper function to neaten up plugin feature detection calls */
static bool fst_canDo(FST* fst, char* feature) {
	bool can;
	can = (fst->plugin->dispatcher(fst->plugin, effCanDo, 0, 0, (void*)feature, 0.0f) > 0);
	printf("Plugin can %s : %s\n", feature, ((can) ? "Yes" : "No"));
	return can;
}

static inline void fst_update_current_program(FST* fst) {
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

bool fst_get_program_name (FST *fst, short program, char* name, size_t size) {
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

void fst_program_change (FST *fst, short want_program) {
	if (fst->current_program == want_program)
		return;

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

	fst->want_program = want_program;
	fst->event_call = FST_PROGRAM_CHANGE;

	pthread_cond_wait (&fst->event_called, &fst->lock);

	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);
}

int fst_call_dispatcher(FST *fst, int opcode, int index, int val, void *ptr, float opt ) {
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
		fst->event_call = FST_DISPATCHER;

		pthread_cond_wait (&fst->event_called, &fst->lock);

		pthread_mutex_unlock (&fst->lock);
		pthread_mutex_unlock (&fst->event_call_lock);
		retval = dp.retval;
		fst->dispatcher = NULL;
	}

	return retval;
}

bool fst_editor_open (FST* fst) {
	HWND window;
	HMODULE hInst;
	struct ERect* er;
	RECT rect;

	hInst = GetModuleHandleA (NULL);
	window = CreateWindowA ("FST", fst->handle->name,
		       (WS_CHILD),
//			WS_POPUPWINDOW | WS_DISABLED | WS_MINIMIZE & ~WS_BORDER & ~WS_SYSMENU,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			fst->gui->window, NULL, hInst, NULL
	);


	fst->plugin->dispatcher (fst->plugin, effEditOpen, 0, 0, window, 0);
	fst->plugin->dispatcher (fst->plugin, effEditGetRect, 0, 0, &er, 0 );

	fst->gui->width  = er->right - er->left;
	fst->gui->height = er->bottom - er->top;

	// Fit child window to editor
	SetWindowPos (window, HWND_TOP, 0, fst->gui->tb_height + 1, fst->gui->width, fst->gui->height, 0);
	UpdateWindow(window);

	fst->gui->editor_window = window;

	ShowWindow (window, SW_NORMAL);

	// Resize main window
	SetWindowPos(fst->gui->window,HWND_TOP, 0, 0,
		fst->gui->width,
		fst->gui->height + fst->gui->tb_height + TITLEBAR_HEIGHT,
		SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_SHOWWINDOW
	);

	return TRUE;
}

void fst_editor_close (FST* fst) {
	fst->plugin->dispatcher(fst->plugin, effEditClose, 0, 0, NULL, 0.0f);
	DestroyWindow(fst->gui->editor_window);
	fst->gui->editor_window = NULL;
}

bool fst_gui_open_main (FST* fst, bool WithEditor) {
	HMODULE hInst;
	HWND window, program_combo, channel_combo, editor_button, midi_button;
	RECT rect;
	FST_GUI* gui;
	char progName[24];
	short i;
        char buf[10];

	/* "guard point" to trap errors that occur during plugin loading */
	if (!(fst->plugin->flags & effFlagsHasEditor)) {
		fst_error ("Plugin \"%s\" has no editor", fst->handle->name);
		return FALSE;
	}

	hInst = GetModuleHandleA (NULL);

	// Create GUI struct
	gui = malloc(sizeof(FST_GUI));

	// Create Main Window
	if ((gui->window = CreateWindowA ("FST", fst->handle->name,
		(WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_CAPTION),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hInst, NULL)) == NULL)
	{
		fst_error ("cannot create GUI window");
	}

	// Bind FST to window
	if (! SetPropA(gui->window, "FST", fst))
		fst_error ("cannot set GUI window property");

	// Create ComboBox with channels
	channel_combo = CreateWindowA(WC_COMBOBOX,0,WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,0,0,65,
		500,gui->window,(HMENU)IDC_CHANNEL_COMBO,hInst,0);
	GetWindowRect(channel_combo, &rect);

	for( i=0; i <= 17; i++ ) {
		if (i == 0) strcpy( buf, "Omni");
		else if (i == 17) strcpy( buf, "None");
		else sprintf( buf, "Ch %d", i);

		SendMessageA(channel_combo, CB_ADDSTRING, 0, (LPARAM) TEXT(buf));
	}
	SendMessageA(channel_combo, CB_SETCURSEL, 0, 0); // Select first entry

	gui->tb_height = rect.bottom - rect.top;

	// Create ComboBox with presets
	program_combo = CreateWindowA(WC_COMBOBOX,0,WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL
		,rect.right - rect.left + 1,0,200,500,gui->window,(HMENU)IDC_PROGRAM_COMBO,hInst,0);
	GetWindowRect(program_combo, &rect);

	for( i = 0; i < fst->plugin->numPrograms; i++ ) {	
		if ( fst->vst_version >= 2 ) {
			fst_get_program_name(fst, i, progName, sizeof(progName));
		} else {
			/* FIXME:
			So what ? nasty plugin want that we iterate around all presets ?
			no way - we don't have time for this
			*/
			sprintf ( progName, "preset %d", i );
		}

		SendMessageA(program_combo, CB_ADDSTRING, 0, (LPARAM) TEXT(progName));
	}
	SendMessageA(program_combo, CB_SETCURSEL, 0, 0); // Select first entry

	// Create Button for Show/Hide Editor
	editor_button = CreateWindowA(WC_BUTTON,"&EDITOR",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE|BS_TEXT,
		rect.right,0,100,gui->tb_height,gui->window,(HMENU)IDC_EDITOR_BUTTON,hInst,0);
	GetWindowRect(editor_button, &rect);

	gui->tb_width = rect.right + 3;
	fst->gui = gui;

	// Show Main Window
	SetWindowPos (gui->window, HWND_TOP, 0, 0, gui->tb_width, gui->tb_height + TITLEBAR_HEIGHT,
		SWP_NOMOVE | SWP_NOACTIVATE | SWP_DEFERERASE | SWP_NOCOPYBITS | SWP_NOSENDCHANGING);

	// Open editor (if we need this)
	if (WithEditor) {
		SendMessageA(editor_button, BM_SETCHECK, BST_CHECKED, 0);
		fst_editor_open(fst);
	}

	// Finally show the GUI window
	ShowWindow (gui->window, SW_SHOWNORMAL);

	// Get X server handle of GUI window
	fst->gui->xid = (int) GetPropA (gui->window, "__wine_x11_whole_window");
//	printf( "And xid = %x\n", fst->xid );
}

bool fst_gui_open (FST* fst) {
	/* wait for the plugin editor window to be created (or not) */
	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);

	if (! fst->gui) {
		fst->event_call = FST_GUI_OPEN;
		pthread_cond_wait (&fst->event_called, &fst->lock);
	}

	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);

	if (! fst->gui->window) {
		fst_error ("no window created for VST plugin editor");
		return FALSE;
	}

	return TRUE;
}

void fst_gui_close_main (FST* fst) {
	if (fst->gui->editor_window)
		fst_editor_close(fst);

	DestroyWindow(fst->gui->window);
	free(fst->gui);
	fst->gui = NULL;
}

void fst_gui_close (FST* fst) {
	if (! fst->gui) return;

	if (GetCurrentThreadId() == MainThreadId) {
		fst_gui_close_main(fst);
	} else {
		pthread_mutex_lock (&fst->event_call_lock);
		pthread_mutex_lock (&fst->lock);
		if (! fst->gui) {
			fst->event_call = FST_GUI_CLOSE;
			pthread_cond_wait (&fst->event_called, &fst->lock);
		}
		pthread_mutex_unlock (&fst->lock);
		pthread_mutex_unlock (&fst->event_call_lock);
	}
}

void fst_suspend (FST *fst) {
	fst_error("Suspend plugin");
	fst_call_dispatcher (fst, effStopProcess, 0, 0, NULL, 0.0f);
	fst_call_dispatcher (fst, effMainsChanged, 0, 0, NULL, 0.0f);
} 

void fst_resume (FST *fst) {
	fst_error("Resume plugin");
	fst_call_dispatcher (fst, effMainsChanged, 0, 1, NULL, 0.0f);
	fst_call_dispatcher (fst, effStartProcess, 0, 0, NULL, 0.0f);
} 

static void fst_event_loop_remove_plugin (FST* fst) {
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

static void fst_event_loop_add_plugin (FST* fst) {
	if (fst_first) {
		FST* p = fst_first;
		while (p->next) p = p->next;
		p->next = fst;
	} else {
		fst_first = fst;
	}
}

FSTHandle* fst_load (const char * path) {
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

	if ( 
	  (main_entry = (main_entry_t) GetProcAddress (dll, "VSTPluginMain")) == NULL &&
	  (main_entry = (main_entry_t) GetProcAddress (dll, "main")) == NULL
	) {
		free(name);
		FreeLibrary (dll);
		fst_error("Can't found either main and VSTPluginMain entry\n");
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

bool fst_unload (FSTHandle* fhandle) {
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

FST* fst_open (FSTHandle* fhandle, audioMasterCallback amc, void* userptr) {
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

static void fst_close_main (FST* fst) {
	fst->plugin->dispatcher(fst->plugin, effClose, 0, 0, NULL, 0.0f);
	--fst->handle->plugincnt;
}

void fst_close (FST* fst) {
	fst_suspend(fst);
	fst_gui_close(fst);

	// It's matter from which thread we calling it
	if (GetCurrentThreadId() == MainThreadId) {
		fst_close_main(fst);
	} else {
		// Try call from event_loop thread
		pthread_mutex_lock (&fst->event_call_lock);
		pthread_mutex_lock (&fst->lock);
		fst->event_call = FST_CLOSE;
		pthread_cond_wait (&fst->event_called, &fst->lock);
		pthread_mutex_unlock (&fst->lock);
		pthread_mutex_unlock (&fst->event_call_lock);
	}
	free(fst);
	
	printf("Plugin closed\n");
}

static inline void fst_event_handler(FST* fst) {
	pthread_mutex_lock (&fst->lock);

	switch (fst->event_call) {
	case FST_CLOSE:
		fst->plugin->dispatcher(fst->plugin, effClose, 0, 0, NULL, 0.0f);
		fst_event_loop_remove_plugin (fst);
		--fst->handle->plugincnt;
		break;
	case FST_GUI_OPEN:
		fst_gui_open_main(fst,true);
		LPOPENFILENAME lpofn;
		GetOpenFileName(lpofn);
		break;
	case FST_GUI_CLOSE:
		fst_gui_close_main(fst);
		break;
	case FST_PROGRAM_CHANGE:
		fst->plugin->dispatcher (fst->plugin, effBeginSetProgram, 0, 0, NULL, 0);
		fst->plugin->dispatcher (fst->plugin, effSetProgram, 0,(int32_t) fst->want_program, NULL, 0);
		fst->plugin->dispatcher (fst->plugin, effEndSetProgram, 0, 0, NULL, 0);
		fst->current_program = fst->want_program;
		fst->program_changed = TRUE;
		fst->want_program = -1;
		break;
	case FST_DISPATCHER: ;
		struct FSTDispatcher* dp = fst->dispatcher;
		dp->retval = fst->plugin->dispatcher( fst->plugin, dp->opcode, dp->index, dp->val, dp->ptr, dp->opt );
		break;
	}
	fst->event_call = FST_RESET;
	pthread_cond_signal (&fst->event_called);
	pthread_mutex_unlock (&fst->lock);
}

bool fst_init (HMODULE hInst) {
	WNDCLASSEX wclass;
	HANDLE this_thread;

	wclass.cbSize = sizeof(WNDCLASSEX);
	wclass.style = 0;
//	wclass.style = (CS_HREDRAW | CS_VREDRAW);
	wclass.lpfnWndProc = my_window_proc;
	wclass.cbClsExtra = 0;
	wclass.cbWndExtra = 0;
	wclass.hInstance = hInst;
	wclass.hIcon = LoadIcon(hInst, "FST");
	wclass.hCursor = LoadCursor(0, IDI_APPLICATION);
	wclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wclass.lpszMenuName = "MENU_FST";
	wclass.lpszClassName = "FST";
	wclass.hIconSm = 0;

	if (!RegisterClassExA(&wclass)){
		printf( "Class register failed :(\n" );
		return FALSE;
	}

	this_thread = GetCurrentThread ();

	//SetPriorityClass ( h_thread, REALTIME_PRIORITY_CLASS);
	SetPriorityClass ( this_thread, ABOVE_NORMAL_PRIORITY_CLASS);
	//SetThreadPriority ( h_thread, THREAD_PRIORITY_TIME_CRITICAL);
	SetThreadPriority ( this_thread, THREAD_PRIORITY_ABOVE_NORMAL);
	printf ("W32 GUI EVENT Thread Class: %d\n", GetPriorityClass (this_thread));
	printf ("W32 GUI EVENT Thread Priority: %d\n", GetThreadPriority(this_thread));

/*
	if ((dummy_window = CreateWindowA ("FST", "dummy",
		WS_OVERLAPPEDWINDOW | WS_DISABLED,
		0, 0, 0, 0, NULL, NULL, hInst, NULL )) == NULL)
	{
		fst_error ("cannot create dummy timer window");
	}

	if (!SetTimer (dummy_window, 1000, 50, NULL)) {
		fst_error ("cannot set timer on dummy window");
		return;
	}
*/

	return TRUE;
}

bool fst_event_loop () {
	MSG msg;
	FST* fst;

	if (PeekMessageA (&msg, NULL, 0, 0, PM_REMOVE) != 0) {
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

//	if ( msg.message != WM_TIMER || msg.hwnd != dummy_window )
//		return TRUE;

	for (fst = fst_first; fst; fst = fst->next) {
		if (fst->wantIdle)
			fst->plugin->dispatcher (fst->plugin, effIdle, 0, 0, NULL, 0);  

		if (fst->gui && fst->gui->editor_window)
			fst->plugin->dispatcher (fst->plugin, effEditIdle, 0, 0, NULL, 0);

		fst_update_current_program(fst);

		if (fst->event_call != FST_RESET)
			fst_event_handler(fst);
	}

	return TRUE;
}
