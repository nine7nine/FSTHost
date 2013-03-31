#include "fst.h"

// most simple one :) could be sufficient.... 
intptr_t
simple_master_callback( struct AEffect *fx, long opcode, long index, long value, void *ptr, float opt ) {
	if ( opcode == audioMasterVersion ) {
		return 2;
	} else {
		return 0;
	}
}

static void fst_showinfo(FST* fst) {
	char tmpstr[64];
	if ( fst_call_dispatcher( fst, effGetEffectName, 0, 0, tmpstr, 0 ) ) {
		printf("name: %s\n",tmpstr);
	} else {
		printf("name: %s\n",fst->handle->name);
	}

	printf("uniqueID: %d\n", fst->plugin->uniqueID);
	printf("version %d\n", fst->plugin->version);
	printf("vst_version: %d\n", fst->vst_version);
	printf("isSynth: %d\n", fst->isSynth);
	printf("canReceiveVstEvents: %d\n", fst->canReceiveVstEvents);
	printf("canReceiveVstMidiEvent: %d\n", fst->canReceiveVstMidiEvent);
	printf("canSendVstEvents: %d\n", fst->canSendVstEvents);
/*
	printf("canSendVstMidiEvent", bool2str(tmpstr,sizeof tmpstr,fst->canSendVstMidiEvent));
	printf("numInputs", int2str(tmpstr,sizeof tmpstr,fst->plugin->numInputs));
	printf("numOutputs", int2str(tmpstr,sizeof tmpstr,fst->plugin->numOutputs));
	printf("numParams", int2str(tmpstr,sizeof tmpstr,fst->plugin->numParams));
	printf("hasEditor", 
		bool2str(tmpstr,sizeof tmpstr, fst->plugin->flags & effFlagsHasEditor ? TRUE : FALSE));
*/
	/* TODO: Category need some changes in vestige (additional enum)
	if( (info->Category = read_string( fp )) == NULL ) goto error;
	*/
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	FST*		fst;
	FSTHandle*	handle;

	const char* path = cmdline;

	printf("Load plugin %s\n", path);
	handle = fst_load(path);
	if (! handle) {
		fst_error ("can't load plugin %s", path);
		return 1;
	}

	printf( "Revive plugin: %s\n", handle->name);
	fst = fst_open(handle, (audioMasterCallback) simple_master_callback, NULL);
	if (! fst) {
		fst_error ("can't instantiate plugin %s", handle->name);
		return 1;
	}

	fst_showinfo(fst);

	printf("Close plugin: %s\n", handle->name);
	fst_close(fst);

	printf("Unload plugin: %s\n", path);
	fst_unload(handle);

	return 0;
}
