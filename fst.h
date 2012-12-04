#ifndef __fst_fst_h__
#define __fst_fst_h__

#include <stdio.h>
#include <setjmp.h>
#include <pthread.h>
#include <stdbool.h>

#include <windows.h>
#include "vestige/aeffectx.h"

/**
 * Display FST error message.
 *
 * Set via fst_set_error_function(), otherwise a FST-provided
 * default will print @a msg (plus a newline) to stderr.
 *
 * @param msg error message text (no newline at end).
 */
extern void (*fst_error_callback)(const char *msg);

/**
 * Set the @ref fst_error_callback for error message display.
 *
 * The FST library provides two built-in callbacks for this purpose:
 * default_fst_error_callback() and silent_fst_error_callback().
 */
void fst_set_error_function (void (*func)(const char *));

typedef struct _FST FST;
typedef struct _FSTHandle FSTHandle;
//typedef struct _FSTInfo FSTInfo;
typedef struct _FXHeader FXHeader;

/*
struct _FSTInfo 
{
    char *name;
    int UniqueID;
    char *Category;
    
    int numInputs;
    int numOutputs;
    int numParams;

    int wantMidi;
    int wantEvents;
    int hasEditor;
    int canProcessReplacing; // what do we need this for ?

    // i think we should save the parameter Info Stuff soon.
    // struct VstParameterInfo *infos;
    char **ParamNames;
    char **ParamLabels;
};
*/

typedef struct AEffect * (*main_entry_t)(audioMasterCallback);

struct _FSTHandle
{
    void*		dll;
    char*		name;
    char*		path;
    main_entry_t	main_entry;

    int plugincnt;
};

struct ERect {
    short top;
    short left;
    short bottom;
    short right;
};

struct FSTDispatcher {
	int	opcode;
	int	index;
	int	val;
	void*	ptr;
	float	opt;
	int	retval;
};

enum EventCall {
	RESET,
	CLOSE,
	DISPATCHER,
	EDITOR_OPEN,
	EDITOR_CLOSE,
	PROGRAM_CHANGE
};

struct _FST 
{
	struct AEffect*		plugin;
	FSTHandle*		handle;
	struct _FST*		next;

	enum EventCall		event_call;
	struct FSTDispatcher*	dispatcher;

	bool			editor_popup;
	void*			window; /* win32 HWND */
	int			xid;    /* X11 XWindow */
	int			width;
	int			height;
	bool			wantIdle;

	bool			program_changed;
	short			want_program;
	short			current_program;

	unsigned short		vst_version;
	bool			isSynth;
	bool			canReceiveVstEvents;
	bool			canReceiveVstMidiEvent;
	bool			canSendVstEvents;
	bool			canSendVstMidiEvent;

	pthread_mutex_t		lock;
	pthread_mutex_t		event_call_lock;
	pthread_cond_t		event_called;
};

enum FxFileType {
	FXBANK		= 0,
	FXPROGRAM	= 1
};

struct _FXHeader {
        unsigned int chunkMagic;
        unsigned int byteSize;
        unsigned int fxMagic;
        unsigned int version;
        unsigned int fxID;
        unsigned int fxVersion;
        unsigned int numPrograms;
};

extern FSTHandle* fst_load (const char * );
extern bool fst_unload (FSTHandle*);

void fst_event_loop (HMODULE hInst);

extern FST* fst_open (FSTHandle*, audioMasterCallback amc, void* userptr);
extern void fst_close (FST*);
extern void fst_loop_quit();

extern void fst_program_change (FST *fst, short want_program);
extern bool fst_get_program_name (FST *fst, short program, char* name, size_t size);

extern bool fst_run_editor (FST*, bool popup);
extern void fst_destroy_editor (FST*);

//extern FSTInfo *fst_get_info (char *dllpathname);
//extern void fst_free_info (FSTInfo *info);
extern int fst_call_dispatcher(FST *fst, int opcode, int index, int val, void *ptr, float opt );

/**
 * Save a plugin state to a file.
 */
extern int fst_save_state (FST * fst, const char * filename);
extern int fst_save_fps (FST * fst, const char * filename);
extern int fst_save_fxfile (FST * fst, const char * filename, enum FxFileType fileType);

#endif /* __fst_fst_h__ */
