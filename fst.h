#ifndef __fst_fst_h__
#define __fst_fst_h__

#include <stdio.h>
#include <setjmp.h>
#include <pthread.h>
#include <stdbool.h>

#include "vestige/aeffectx.h"
#include "amc.h"

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

/*
typedef struct _FSTInfo 
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
} FSTInfo;
*/

typedef AEffect* (VSTCALLBACK *main_entry_t)(audioMasterCallback);

typedef struct {
    void*		dll;
    char*		name;
    char*		path;
    main_entry_t	main_entry;
} FSTHandle;

typedef struct {
	int32_t		opcode;
	int32_t		index;
	intptr_t	val;
	void*		ptr;
	float		opt;
	intptr_t	retval;
} FSTDispatcher;

typedef enum {
	RESET,
	CLOSE,
	SUSPEND,
	RESUME,
	DISPATCHER,
	EDITOR_OPEN,
	EDITOR_CLOSE,
	PROGRAM_CHANGE
} FSTEventTypes;

typedef struct {
	FSTEventTypes	type;
	pthread_mutex_t	lock;
	pthread_cond_t	called;
	union { /* Data */
		FSTDispatcher* dispatcher;	/* DISPATCHER */
		int32_t program;		/* PROGRAM_CHANGE */
	};
} FSTEventCall;

typedef struct _FST {
	AEffect*		plugin;
	FSTHandle*		handle;
	AMC*			amc;
	FSTEventCall*		event_call;
	struct _FST*		next;

	char*			name;
	pthread_mutex_t		lock;
	bool			wantIdle;
	int			MainThreadId;

	/* GUI */
	bool			editor_popup;
	void*			window; /* win32 HWND */
	void*			xid;    /* X11 XWindow */
	int			width;
	int			height;
	bool			wantResize;

	int32_t			current_program;
	bool			program_changed;

	/* Info */
	intptr_t		vst_version;
	bool			isSynth;
	bool			canReceiveVstEvents;
	bool			canReceiveVstMidiEvent;
	bool			canSendVstEvents;
	bool			canSendVstMidiEvent;

	/* This is needed only audioMasterGetTime - but we don't know how long plugin want to use it */
	struct VstTimeInfo	timeInfo;
} FST;

enum FxFileType {
	FXBANK		= 0,
	FXPROGRAM	= 1
};

typedef struct {
	int32_t chunkMagic;
	int32_t byteSize;
	int32_t fxMagic;
	int32_t version;
	int32_t fxID;
	int32_t fxVersion;
	int32_t numPrograms;
} FXHeader;

void fst_error (const char *fmt, ...);

FSTHandle* fst_load (const char * path );
bool fst_unload (FSTHandle*);
FST* fst_open (FSTHandle*);
FST* fst_load_open (const char* path);
void fst_close (FST*);

void fst_event_loop();
bool fst_event_callback();

void fst_call (FST *fst, FSTEventTypes type);
intptr_t fst_call_dispatcher (FST *fst, int32_t opcode, int32_t index, intptr_t val, void *ptr, float opt );
void fst_program_change (FST *fst, int32_t program);
bool fst_get_program_name (FST *fst, short program, char* name, size_t size);
bool fst_set_program_name (FST *fst, const char* name);

bool fst_run_editor (FST*, bool popup);
bool fst_show_editor (FST *fst);

/* Support for FXB/FXP files (fxb.c) */
int fst_load_fxfile ( FST *fst, const char *filename );
int fst_save_fxfile (FST * fst, const char * filename, enum FxFileType fileType);

/* Support for XML Database (info.c) */
int fst_info_update(const char *dbpath, const char *fst_path);

#endif /* __fst_fst_h__ */
