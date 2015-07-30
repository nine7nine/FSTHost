#ifndef __fst_h__
#define __fst_h__

#include <pthread.h>
#include <stdbool.h>

#include "vestige/aeffectx.h"
#include "amc.h"

/* VST standard (kVstMaxProgNameLen) says it is 24 bytes but some plugs use more characters */
#define FST_MAX_PROG_NAME 32

/* VST standard (kVstMaxParamStrLen) says it is 8 bytes but some plugs use more characters */
#define FST_MAX_PARAM_NAME 32

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
	CONFIGURE,
	DISPATCHER,
	EDITOR_OPEN,
	EDITOR_CLOSE,
	EDITOR_RESIZE,
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

typedef void (*FSTWindowCloseCallback)(void* arg);

typedef struct _FST {
	struct _FST*		next;

	AEffect*		plugin;
	FSTHandle*		handle;
	AMC			amc;
	FSTEventCall		event_call;

	char*			name;
	pthread_mutex_t		lock;
	pthread_mutex_t		process_lock;
	bool			wantIdle;
	int			MainThreadId;

	/* GUI */
	bool			editor_popup;
	void*			window; /* win32 HWND */
	void*			xid;    /* X11 XWindow */
	int			width;
	int			height;

	/* Window Close Callback */
	void			(*window_close)(void* arg);
	void*			window_close_user_ptr;

	int32_t			current_program;

	/* Info */
	int			vst_version;
	bool			isSynth;
	bool			canReceiveVstEvents;
	bool			canReceiveVstMidiEvent;
	bool			canSendVstEvents;
	bool			canSendVstMidiEvent;
} FST;

typedef enum {
	FST_PORT_IN,
	FST_PORT_OUT
} FSTPortType;

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

static inline bool fst_want_midi_in ( FST* fst ) {
	/* No MIDI at all - very old/rare v1 plugins */
	if (fst->vst_version < 2) return false;
	return (fst->isSynth || fst->canReceiveVstEvents || fst->canReceiveVstMidiEvent);
}

static inline bool fst_want_midi_out ( FST* fst ) {
	/* No MIDI at all - very old/rare v1 plugins */
	if (fst->vst_version < 2) return false;
	return (fst->canSendVstEvents || fst->canSendVstMidiEvent);
}

static inline void fst_set_window_close_callback ( FST* fst, void(*f), void* ptr ) {
	fst->window_close = f;
	fst->window_close_user_ptr = ptr;
}

static inline void fst_process ( FST* fst, float** ins, float** outs, int32_t frames ) {
	if ( fst->plugin->flags & effFlagsCanReplacing ) {
		fst->plugin->processReplacing (fst->plugin, ins, outs, frames);
	} else {
		fst->plugin->process (fst->plugin, ins, outs, frames);
	}
}

static inline void fst_process_events ( FST* fst, VstEvents* events ) {
	fst->plugin->dispatcher (fst->plugin, effProcessEvents, 0, 0, events, 0.0f);
}

static inline float fst_get_param ( FST* fst, int32_t param ) {
	return fst->plugin->getParameter(fst->plugin,param);
}

static inline void fst_set_param ( FST* fst, int32_t param, float value ) {
	fst->plugin->setParameter( fst->plugin, param, value );
}

static inline bool fst_process_trylock ( FST* fst ) { return ( pthread_mutex_trylock ( &(fst->process_lock) ) == 0 ); }
static inline void fst_process_lock ( FST* fst ) { pthread_mutex_lock ( &(fst->process_lock) ); }
static inline void fst_process_unlock ( FST* fst ) { pthread_mutex_unlock ( &(fst->process_lock) ); }

static inline int32_t	fst_num_params	( FST* fst ) { return fst->plugin->numParams; }
static inline int32_t	fst_num_presets	( FST* fst ) { return fst->plugin->numPrograms; }
static inline int32_t	fst_num_ins	( FST* fst ) { return fst->plugin->numInputs; }
static inline int32_t	fst_num_outs	( FST* fst ) { return fst->plugin->numOutputs; }
static inline int32_t	fst_uid 	( FST* fst ) { return fst->plugin->uniqueID; }
static inline int32_t	fst_version 	( FST* fst ) { return fst->plugin->version; }
static inline bool	fst_has_chunks	( FST* fst ) { return fst->plugin->flags & effFlagsProgramChunks; }

void fst_error (const char *fmt, ...);

void fst_set_thread_priority ( const char* th_name, int class, int priority );
void fst_show_thread_info ( const char* th_name );

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
void fst_configure (FST *fst, float sample_rate, intptr_t block_size);
void fst_get_program_name (FST *fst, int32_t program, char* name, size_t size);
bool fst_set_program_name (FST *fst, const char* name);
bool fst_get_port_name ( FST* fst, int32_t port_number, FSTPortType type, char* name );

bool fst_run_editor (FST*, bool popup);
bool fst_show_editor (FST *fst);

/* Support for FXB/FXP files (fxb.c) */
int fst_load_fxfile ( FST *fst, const char *filename );
int fst_save_fxfile (FST * fst, const char * filename, enum FxFileType fileType);

#endif /* __fst_h__ */
