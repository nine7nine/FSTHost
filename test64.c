#include <unistd.h>
#include "fst.h"

static void fst_showinfo(FST* fst) {
	printf("name: %s\n",fst->name);
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
	const char* path = cmdline;

	fst = fst_open_load(path, &simple_master_callback, NULL);
	if (! fst) return 1;

	fst_resume(fst);

	fst_run_editor(fst, false);

	fst_error("Wait 5s ..");
	int t;
	for (t=0; t < 10; t++) {
		fst_event_callback();
		sleep(1);
	}

	fst_showinfo(fst);

	fst_close(fst);

	return 0;
}
