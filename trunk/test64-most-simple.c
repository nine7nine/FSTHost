#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "vestige/aeffectx.h"

typedef struct AEffect * (*main_entry_t)(audioMasterCallback);

// most simple one :) could be sufficient.... 
intptr_t VSTCALLBACK
simple_master_callback( struct AEffect *fx, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt ) {
        if ( opcode == audioMasterVersion ) return 2;
        return 0;
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow) {
	const char* path = cmdline;

	printf("Load plugin %s\n", path);
	void* dll = LoadLibraryA(path);
	if (! dll) {
		printf ("can't load plugin %s\n", path);
		return 1;
	}

	main_entry_t main_entry = (main_entry_t) GetProcAddress (dll, "VSTPluginMain");
	printf("Main entry: %p\n", main_entry);

	printf( "Revive plugin\n");
	struct AEffect* plugin = main_entry((audioMasterCallback) simple_master_callback);

	if (! plugin) {
		printf ("can't instantiate plugin\n");
		return 1;
	}

	printf( "V: %d U: %d NI: %d NO: %d NPr: %d NPa: %d\n", plugin->version, plugin->uniqueID, plugin->numInputs,
                plugin->numOutputs, plugin->numPrograms, plugin->numParams );

	// Open Plugin
	printf("Open\n");
        plugin->dispatcher (plugin, effOpen, 0, 0, NULL, 0.0f);
	sleep(3);
	printf("Close\n");
        plugin->dispatcher (plugin, effOpen, 0, 0, NULL, 0.0f);

	FreeLibrary (dll);

	return 0;
}
