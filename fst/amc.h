#ifndef __amc_h__
#define __amc_h__

#include <stdbool.h>
#include "vestige/aeffectx.h"

typedef struct _AMC {
	/* This is needed only audioMasterGetTime - but we don't know how long plugin want to use it */
	struct VstTimeInfo	timeInfo;

	void		(*Automate)	( struct _AMC*, int32_t param );
	void		(*GetTime)	( struct _AMC*, int32_t mask );
	bool		(*ProcessEvents)( struct _AMC*, VstEvents* events );
	intptr_t	(*TempoAt)	( struct _AMC*, int32_t location );
	void		(*NeedIdle)	( struct _AMC* );
	void		(*SizeWindow)	( struct _AMC*, int32_t width, int32_t height );
	intptr_t	(*GetSampleRate)( struct _AMC* );
	intptr_t	(*GetBlockSize)	( struct _AMC* );
	bool		(*UpdateDisplay)( struct _AMC* );
	void*		user_ptr;
} AMC;

intptr_t VSTCALLBACK amc_callback ( AEffect*, int32_t, int32_t, intptr_t, void*, float );
intptr_t VSTCALLBACK amc_simple_callback ( AEffect*, int32_t, int32_t, intptr_t, void*, float );

#endif /* __amc_h__ */
