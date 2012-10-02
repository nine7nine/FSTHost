/*
   FSTHost Control by XJ / Pawel Piatek /

   This is part of FSTHost sources
*/

#include <stdint.h>

enum State {
	FST_STATE_BYPASS = 0,
	FST_STATE_ACTIVE = 1,
	FST_NA = 2 // NOT AVAILABLE
};

struct FSTState {
        enum State state;
        uint8_t program; /* 0 - 127 */
        uint8_t channel; /* 0 - 17 */
        uint8_t volume; /* 0 - 127 */
        char program_name[24];
};

struct FSTPlug {
        uint8_t id; /* 0 - 127 */
        char name[24];
        struct FSTState* state;
	bool change;
	bool dump_request;
};

struct Song {
        char name[24];
        struct FSTState* fst_state[127];
        struct Song* next;
};

void nLOG(char *fmt, ...);
