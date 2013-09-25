#ifndef __amc_h__
#define __amc_h__

#include <stdbool.h>
#include "vestige/aeffectx.h"

typedef struct _AMC {
	void		(*Automate) ( struct _AMC* amc, int32_t param );
	VstTimeInfo*	(*GetTime) ( struct _AMC* amc, int32_t mask );
	bool		(*ProcessEvents) ( struct _AMC* amc, VstEvents* events );
	intptr_t	(*TempoAt) ( struct _AMC* amc, int32_t location );
	void		(*NeedIdle) ( struct _AMC* amc );
	void		(*SizeWindow) ( struct _AMC* amc, int32_t width, int32_t height );
	intptr_t	(*GetSampleRate) ( struct _AMC* amc );
	intptr_t	(*GetBlockSize) ( struct _AMC* amc );
	bool		(*UpdateDisplay) ( struct _AMC* amc );
	void*		user_ptr;
} AMC;

intptr_t VSTCALLBACK amc_callback ( AEffect*, int32_t, int32_t, intptr_t, void*, float );
intptr_t VSTCALLBACK amc_simple_callback ( AEffect*, int32_t, int32_t, intptr_t, void*, float );

#endif /* __amc_h__ */
