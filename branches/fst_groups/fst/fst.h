#ifndef __fst_h__
#define __fst_h__

#include <stdbool.h>
#include <stdint.h>

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
//extern void (*fst_error_callback)(const char *msg);

/**
 * Set the @ref fst_error_callback for error message display.
 *
 * The FST library provides two built-in callbacks for this purpose:
 * default_fst_error_callback() and silent_fst_error_callback().
 */
//void fst_set_error_function (void (*func)(const char *));

typedef struct FST FST;
typedef struct FSTHandle FSTHandle;
typedef struct FST_THREAD FST_THREAD;
typedef struct AMC AMC;

typedef void (*FSTWindowCloseCallback)(void* arg);
typedef void (*FSTIdleCallback)(void* arg);

typedef enum {
	INIT,
	OPEN,
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

typedef enum {
	FST_PORT_IN,
	FST_PORT_OUT
} FSTPortType;

enum FxFileType {
	FXBANK		= 0,
	FXPROGRAM	= 1
};

bool fst_want_midi_in ( FST* fst );
bool fst_want_midi_out ( FST* fst );

void fst_set_window_close_callback ( FST* fst, FSTWindowCloseCallback f, void* ptr );

void fst_process ( FST* fst, float** ins, float** outs, int32_t frames );

//void fst_process_events ( FST* fst, VstEvents* events );
void fst_process_events ( FST* fst, void* events );

float fst_get_param ( FST* fst, int32_t param );

void fst_set_param ( FST* fst, int32_t param, float value );

bool fst_process_trylock ( FST* fst );
void fst_process_lock ( FST* fst );
void fst_process_unlock ( FST* fst );

int32_t	fst_num_params		(FST* fst);
int32_t	fst_num_presets		(FST* fst);
int32_t	fst_num_ins		(FST* fst);
int32_t	fst_num_outs		(FST* fst);
int32_t	fst_uid			(FST* fst);
int32_t	fst_version		(FST* fst);
int32_t	fst_max_port_name	(FST* fst);
bool	fst_has_chunks		(FST* fst);
bool	fst_has_window		(FST* fst);
bool	fst_has_editor		(FST* fst);
bool	fst_has_popup_editor	(FST* fst);
int	fst_width		(FST* fst);
int	fst_height		(FST* fst);
AMC*	fst_amc			(FST* fst);
void*	fst_xid			(FST* fst);

const char* fst_name (FST* fst);
const char* fst_path (FST* fst);

void fst_get_param_name ( FST* fst, int32_t param, char* name );

void fst_error (const char *fmt, ...);

void fst_lock ( FST* fst );
void fst_unlock ( FST* fst );

void fst_set_thread_priority ( const char* th_name, int class, int priority );
void fst_show_thread_info ( const char* th_name );
FST_THREAD* fst_thread_new( const char* name, bool fake );

FSTHandle* fst_load (const char * path );
bool fst_unload (FSTHandle*);
FST* fst_open (FSTHandle* fhandle, FST_THREAD* th);
FST* fst_load_open ( const char* path, FST_THREAD* th );
void fst_close (FST*);

void fst_event_loop();
bool fst_event_callback();
void fst_set_idle_callback ( FST* fst, FSTIdleCallback f, void* ptr );

void fst_call (FST *fst, FSTEventTypes type);
void fst_set_program (FST *fst, int32_t program);
int32_t fst_get_program (FST *fst);
void fst_configure (FST *fst, float sample_rate, intptr_t block_size);
void fst_get_program_name (FST *fst, int32_t program, char* name, size_t size);
bool fst_set_program_name (FST *fst, const char* name);
bool fst_get_port_name ( FST* fst, int32_t port_number, FSTPortType type, char* name );
void fst_editor_resize ( FST* fst, int32_t width, int32_t height );

bool fst_run_editor (FST*, bool popup);
bool fst_show_editor (FST *fst);

void fst_set_chunk(FST* fst, enum FxFileType type, int size, void* chunk);
int32_t fst_get_chunk(FST* fst, enum FxFileType type, void* chunk);

/* Support for FXB/FXP files (fxb.c) */
int fst_load_fxfile (FST *fst, const char *filename);
int32_t fst_get_fxfile_uuid ( const char* filename );
int fst_save_fxfile (FST *fst, const char *filename, enum FxFileType fileType);

#endif /* __fst_h__ */
