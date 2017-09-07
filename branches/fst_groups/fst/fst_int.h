#ifndef __fst_int_h__
#define __fst_int_h__

#include <pthread.h>

#include "fst.h"
#include "vestige/aeffectx.h"
#include "amc.h"

/*
typedef struct FSTInfo 
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

struct FSTHandle {
	HMODULE		dll;
	char*		name;
	char*		path;
	main_entry_t	main_entry;
};

typedef struct {
	FSTEventTypes	type;

	/* Call Data */
	union {
		int32_t program;	/* PROGRAM_CHANGE */
		int32_t	opcode;		/* DISPATCHER */
		int32_t width;		/* EDITOR_RESIZE */
	};

	union {
		float opt;		/* DISPATCHER */
		float sample_rate;	/* CONFIGURE */
	};

	union {
		intptr_t val;		/* DISPATCHER */
		intptr_t block_size;	/* CONFIGURE */
	};

	union {
		int32_t	index;		/* DISPATCHER */
		int32_t	height;		/* EDITOR_RESIZE */
	};

	void*		ptr;		/* DISPATCHER */
	intptr_t	retval;		/* DISPATCHER */
} FSTCall;

typedef struct {
	pthread_mutex_t	lock;
	pthread_cond_t	called;
	FSTCall* call;
} FSTEventCall;

struct FST_THREAD {
	char			name[24];
	HANDLE			handle;
	DWORD			id;
	bool			fake;
	pthread_mutex_t		lock;
	FST*			first;

	/* Idle Callback */
	FSTIdleCallback		idle_cb;
	void*			idle_cb_data;
};

struct FST {
	FST*			next;

	AEffect*		plugin;
	FSTHandle*		handle;
	AMC			amc;
	FSTEventCall		event_call;
	FST_THREAD*		thread;

	char*			name;
	bool			initialized;
	bool			opened;
	pthread_mutex_t		lock;
	pthread_mutex_t		process_lock;

	/* GUI */
	bool			editor_popup;
	void*			window; /* win32 HWND */
	void*			xid;    /* X11 XWindow */
	int			width;
	int			height;

	/* Window Close Callback */
	FSTWindowCloseCallback	window_close_cb;
	void*			window_close_cb_data;

	int32_t			current_program;

	/* Info */
	int			vst_version;
	bool			isSynth;
	bool			canReceiveVstEvents;
	bool			canReceiveVstMidiEvent;
	bool			canSendVstEvents;
	bool			canSendVstMidiEvent;

	/* Audio buffers ptr */
	float**			inports;
	float**			outports;
};

intptr_t fst_call_dispatcher (FST *fst, int32_t opcode, int32_t index, intptr_t val, void *ptr, float opt );

#endif /* __fst_int_h__ */
