#include <stdio.h>
#include <libgen.h>
#include <windows.h>
#include <winnt.h>
#include <wine/exception.h>
#include <pthread.h>
#include <signal.h>

#include "fst.h"

#include <X11/X.h>
#include <X11/Xlib.h>

static FST* fst_first = NULL;

#define DELAYED_WINDOW 1

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
	unsigned short i;

	pthread_mutex_init (&fst->lock, NULL);
	pthread_mutex_init (&fst->event_call_lock, NULL);
	pthread_cond_init (&fst->event_called, NULL);
	fst->want_program = -1;
	fst->current_program = 0;
	fst->event_call = RESET;
	for (i=0; i<128; i++ )
		fst->midi_map[i] = -1;

	if (fst_first == NULL) {
		fst_first = fst;
	} else {
		FST* p = fst_first;
		while (p->next) p = p->next;
		p->next = fst;
	}

	return fst;
}

//DWORD WINAPI
int
fst_gui_event_loop (HMODULE hInst)
{
	MSG msg;
	FST* fst;
	struct AEffect* plugin;
	int newProg;
	char progName[24];
	HWND window;
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

	/* create a dummy window for timer events */
	if ((hInst = GetModuleHandleA (NULL)) == NULL) {
		fst_error ("can't get module handle");
		return 1;
	}
	
	if ((window = CreateWindowA ("FST", "dummy", WS_OVERLAPPEDWINDOW | WS_DISABLED,
		0, 0, 0, 0, NULL, NULL, hInst, NULL )) == NULL) {
		fst_error ("cannot create dummy timer window");
	}

	if (!SetTimer (window, 1000, 20, NULL)) {
		fst_error ("cannot set timer on dummy window");
		return 1;
	}

	while (GetMessageA (&msg, NULL, 0,0)) {
	   TranslateMessage( &msg );
	   DispatchMessageA (&msg);

	   /* handle window creation requests, destroy requests, 
	      and run idle callbacks 
	   */
	   if ( msg.message != WM_TIMER )
		continue;

again:
	   for (fst = fst_first; fst; fst = fst->next) {
	      plugin = fst->plugin;

	      switch (fst->event_call) {
	      case EDITOR_OPEN:		
	         pthread_mutex_lock (&fst->lock);
	         fst->event_call = RESET;
	         if (fst->window == NULL && ! fst_create_editor (fst))
	               fst_error ("cannot create editor for plugin %s", fst->handle->name);

	         pthread_cond_signal (&fst->event_called);
	         pthread_mutex_unlock (&fst->lock);
	         break;
	      case EDITOR_SHOW:
	         pthread_mutex_lock (&fst->lock);
	         fst->event_call = RESET;
	         if (fst->window != NULL) {
		         ShowWindow(fst->window, SW_RESTORE);
		         EnableWindow(fst->window, TRUE);
	        	 UpdateWindow(fst->window);
	         }

	         pthread_cond_signal (&fst->event_called);
	         pthread_mutex_unlock (&fst->lock);
	         break;
	      case EDITOR_CLOSE:
	         pthread_mutex_lock (&fst->lock);
	         fst->event_call = RESET;
		 if (fst->window != NULL)
		    plugin->dispatcher( plugin, effEditClose, 0, 0, NULL, 0.0f );

		 DestroyWindow(fst->window);
	         fst->window = NULL;
	         pthread_cond_signal (&fst->event_called);
	         pthread_mutex_unlock (&fst->lock);
	         break;
	      case PROGRAM_CHANGE:
	         pthread_mutex_lock (&fst->lock);
	         fst->event_call = RESET;
	         if (fst->want_program != -1) {
	             plugin->dispatcher (plugin, effBeginSetProgram, 0, 0, NULL, 0);
	             plugin->dispatcher (plugin, effSetProgram, 0, fst->want_program, NULL, 0);
	             plugin->dispatcher (plugin, effEndSetProgram, 0, 0, NULL, 0);
	             fst->want_program = -1; 
	         }
	         pthread_cond_signal (&fst->event_called);
	         pthread_mutex_unlock (&fst->lock);
	         break;
	      case CLOSE:
	         pthread_mutex_lock (&fst->lock);
	         fst->event_call = RESET;
	         plugin->dispatcher(plugin, effClose, 0, 0, NULL, 0.0f);
	         fst_event_loop_remove_plugin (fst);

	         if (fst->handle->plugincnt)
		    --fst->handle->plugincnt;

	         pthread_cond_signal (&fst->event_called);
	         pthread_mutex_unlock (&fst->lock);
	         goto again;
	      case DISPATCHER:
	         pthread_mutex_lock (&fst->lock);
	         fst->event_call = RESET;
	         fst->dispatcher_retval = fst->plugin->dispatcher( plugin, 
	              fst->dispatcher_opcode, fst->dispatcher_index, fst->dispatcher_val,
	              fst->dispatcher_ptr, fst->dispatcher_opt );
	         pthread_cond_signal (&fst->event_called);
	         pthread_mutex_unlock (&fst->lock);
	         break;
	      }

	      if (fst->wantIdle)
	         plugin->dispatcher (plugin, effIdle, 0, 0, NULL, 0);  

	      if (fst->window)
	         plugin->dispatcher (plugin, effEditIdle, 0, 0, NULL, 0);
		
	      newProg = plugin->dispatcher( plugin, effGetProgram, 0, 0, NULL, 0.0f );
	      if (fst->current_program != newProg) {
	         fst->current_program = newProg;
	         plugin->dispatcher (plugin, effGetProgramName, 0, 0, &progName, 0.0f);
	         printf("Program: %d : %s\n", newProg, progName);
	      }
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
		return -1;
	}

	return 0;
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

int
fst_program_change (FST *fst, int want_program)
{
        pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);

	if (fst->want_program != want_program) {
		fst->want_program = want_program;
		if (fst->mainThreadId != GetCurrentThreadId() ) {
			fst->event_call = PROGRAM_CHANGE;

			pthread_cond_wait (&fst->event_called, &fst->lock);
		} else {
			fst->plugin->dispatcher (fst->plugin, effBeginSetProgram, 0, 0, NULL, 0);
			fst->plugin->dispatcher (fst->plugin, effSetProgram, 0, fst->want_program, NULL, 0);
			fst->plugin->dispatcher (fst->plugin, effEndSetProgram, 0, 0, NULL, 0);
	         	fst->current_program = fst->want_program;
			fst->want_program = -1;
		}
	}

        pthread_mutex_unlock (&fst->lock);
        pthread_mutex_unlock (&fst->event_call_lock);
}

int 
fst_call_dispatcher(FST *fst, int opcode, int index, int val, void *ptr, float opt )
{

	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);
	if (fst->mainThreadId != GetCurrentThreadId() ) {
		fst->dispatcher_opcode = opcode;
		fst->dispatcher_index = index;
		fst->dispatcher_val = val;
		fst->dispatcher_ptr = ptr;
		fst->dispatcher_opt = opt;
		fst->event_call = DISPATCHER;

		pthread_cond_wait (&fst->event_called, &fst->lock);

	} else {
        	fst->dispatcher_retval =
			fst->plugin->dispatcher(fst->plugin, opcode, index, val, ptr, opt );
	}
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
		SWP_NOACTIVATE | SWP_NOMOVE);

	ShowWindow (fst->window, SW_SHOWMINNOACTIVE);

	fst->xid = (int) GetPropA (window, "__wine_x11_whole_window");
	printf( "And xid = %x\n", fst->xid );

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
	fst_call_dispatcher (fst, effStopProcess, 0, 0, NULL, 0.0f);
	fst_call_dispatcher (fst, effMainsChanged, 0, 0, NULL, 0.0f);
} 

void
fst_resume (FST *fst) {
	fst_call_dispatcher (fst, effMainsChanged, 0, 1, NULL, 0.0f);
	fst_call_dispatcher (fst, effStartProcess, 0, 0, NULL, 0.0f);
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

HMODULE
fst_load_vst_library(FSTHandle* fhandle, const char * path) {
	HMODULE dll;
	char * full_path;
	char * envdup;
	char * vst_path;
	size_t len1;
	size_t len2;

	if ((dll = LoadLibraryA (path)) != NULL) {
		fhandle->path = strdup(path);
		return dll;
	}

	envdup = getenv ("VST_PATH");
	if (envdup == NULL)
		return NULL;

	envdup = strdup (envdup);
	if (envdup == NULL) {
		fst_error ("strdup failed");
		return NULL;
	}

	len2 = strlen(path);

	vst_path = strtok (envdup, ":");
	while (vst_path != NULL) {
		fst_error ("\"%s\"", vst_path);
		len1 = strlen(vst_path);
		full_path = malloc (len1 + 1 + len2 + 1);
		memcpy(full_path, vst_path, len1);
		full_path[len1] = '/';
		memcpy(full_path + len1 + 1, path, len2);
		full_path[len1 + 1 + len2] = '\0';

		if ((dll = LoadLibraryA (full_path)) != NULL)
			break;

		free(full_path);
		vst_path = strtok (NULL, ":");
	}

	free(envdup);

	fhandle->path = full_path;
	return dll;
}

FSTHandle*
fst_load (const char * path) {
	char mypath[MAX_PATH];
	char * period;
	WCHAR* wbuf;
	FSTHandle* fhandle;
	LPWSTR dos_file_name;


	strcpy(mypath, path);
	if (strstr (path, ".dll") == NULL)
		strcat (mypath, ".dll");

	fhandle = (FSTHandle*) calloc (1, sizeof (FSTHandle));
	fhandle->name = basename (strdup(mypath));

	/* strip off .dll */
	if ((period = strrchr (fhandle->name, '.')) != NULL)
		*period = '\0';

	// Get wine path
	printf("UnixPath: %s\n", mypath);
	wbuf = wine_get_dos_file_name(mypath);
	WideCharToMultiByte(CP_UNIXCP, 0, wbuf, -1, (LPSTR) &mypath, MAX_PATH, NULL, NULL);
	printf("WinePath: %s\n", mypath);

	if ((fhandle->dll = fst_load_vst_library (fhandle, (const char *) &mypath)) == NULL) {
		fst_unload (fhandle);
		return NULL;
	}

	if ((fhandle->main_entry = (main_entry_t) GetProcAddress (fhandle->dll, "main")) == NULL) {
		fst_unload (fhandle);
		return NULL;
	}

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
fst_instantiate (FSTHandle* fhandle, audioMasterCallback amc, void* userptr)
{
	FST* fst = fst_new ();
	short i;
	char ParamName[32];

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
	fst->plugin->user = userptr;
		
	if (fst->plugin->magic != kEffectMagic) {
		fst_error ("%s is not a VST plugin\n", fhandle->name);
		free (fst);
		return NULL;
	}

	fst->plugin->dispatcher (fst->plugin, effOpen, 0, 0, NULL, 0.0f);

	fst->handle->plugincnt++;
	fst->event_call = RESET;
	fst->mainThreadId = GetCurrentThreadId();

	return fst;
}

void
fst_close (FST* fst)
{
	fst_suspend(fst);

	fst_destroy_editor (fst);

	pthread_mutex_lock (&fst->event_call_lock);
	pthread_mutex_lock (&fst->lock);
	fst->event_call = CLOSE;
	pthread_cond_wait (&fst->event_called, &fst->lock);
	pthread_mutex_unlock (&fst->lock);
	pthread_mutex_unlock (&fst->event_call_lock);

	free(fst);
}

int
fst_load_state (FST * fst, const char * filename)
{
	char * file_ext = strrchr(filename, '.');

	if ( (strcmp(file_ext, ".fxp") == 0) || 
	     (strcmp(file_ext, ".FXP") == 0) ||
	     (strcmp(file_ext, ".fxb") == 0) ||
	     (strcmp(file_ext, ".FXB") == 0)
	) {    
		fst_load_fxfile(fst, filename);
	} else if (strcmp(file_ext, ".fps") == 0) {
		fst_load_fps(fst, filename);
	} else {
		printf("Unkown file type\n");
		return 0;
	}

	printf("File %s loaded\n", filename);

	return 1;
}

int
fst_save_state (FST * fst, const char * filename)
{
	char * file_ext = strrchr(filename, '.');

	if ( (strcmp(file_ext, ".fxp") == 0) || (strcmp(file_ext, ".FXP") == 0) ) {
		fst_save_fxfile(fst, filename, 0);
	} else if ( (strcmp(file_ext, ".fxb") == 0) || (strcmp(file_ext, ".FXB") == 0)) {
		fst_save_fxfile(fst, filename, 1);
	} else if (strcmp(file_ext, ".fps") == 0) {
		fst_save_fps(fst, filename);
	} else {
		printf("Unkown file type\n");
		return 0;
	}
	
	return 1;
}
