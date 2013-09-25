#ifndef __amc_h__
#define __amc_h__

#include "fst.h"

struct _AMC {
	void		(*Automate) ( struct _AMC* amc, int32_t param );
	VstTimeInfo*	(*GetTime) ( struct _AMC* amc, int32_t mask );
	bool		(*ProcessEvents) ( struct _AMC* amc, VstEvents* events );
	intptr_t	(*TempoAt) ( struct _AMC* amc, int32_t location );
	void		(*NeedIdle) ( struct _AMC* amc );
	void		(*SizeWindow) ( struct _AMC* amc, int32_t width, int32_t height );
	intptr_t	(*GetSampleRate) ( struct _AMC* amc );
	intptr_t	(*GetBlockSize) ( struct _AMC* amc );
	bool		(*UpdateDisplay) ( struct _AMC* amc );
	void* user_ptr;
};
typedef struct _AMC AMC;

#endif /* __amc_h__ */
