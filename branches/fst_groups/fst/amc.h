#ifndef __amc_h__
#define __amc_h__

#include <stdbool.h>
#include "vestige/aeffectx.h"

typedef struct AMC {
	/* This is needed only audioMasterGetTime - but we don't know how long plugin want to use it */
	struct VstTimeInfo	timeInfo;
	float sample_rate;
	intptr_t block_size;

	void		(*Automate)	( struct AMC*, int32_t param );
	void		(*GetTime)	( struct AMC*, int32_t mask );
	bool		(*ProcessEvents)( struct AMC*, VstEvents* events );
	intptr_t	(*TempoAt)	( struct AMC*, int32_t location );
	void		(*NeedIdle)	( struct AMC* );
	void		(*SizeWindow)	( struct AMC*, int32_t width, int32_t height );
	bool		(*UpdateDisplay)( struct AMC* );
	bool		need_idle;
	void*		user_ptr;
} AMC;

intptr_t VSTCALLBACK amc_callback ( AEffect*, int32_t, int32_t, intptr_t, void*, float );
intptr_t VSTCALLBACK amc_simple_callback ( AEffect*, int32_t, int32_t, intptr_t, void*, float );

#endif /* __amc_h__ */
