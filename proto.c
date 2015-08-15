#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "serv/serv.h"
#include "jfst/jfst.h"
#include "log/log.h"

#define ACK "<OK>"
#define NAK "<FAIL>"

/* fsthost.c */
extern void fsthost_quit();

/* cpuusage.c */
extern void CPUusage_init();
extern double CPUusage_getCurrentValue();

static void send_fmt ( int client_sock, const char* fmt, ... ) {
	va_list ap;
	char msg[512];

	va_start (ap, fmt);
	vsnprintf (msg, sizeof msg, fmt, ap);
	serv_send_client_data ( client_sock, msg );
	va_end (ap);
}

static void list_programs ( JFST* jfst, int sock ) {
	char progName[FST_MAX_PROG_NAME];

	FST* fst = jfst->fst;
	int32_t i;
	for ( i = 0; i < fst_num_presets(fst); i++ ) {
		fst_get_program_name(fst, i, progName, sizeof(progName));
		send_fmt(sock, "%d:%s", i, progName);
        }
}

static void list_params ( JFST* jfst, int sock ) {
	char paramName[FST_MAX_PARAM_NAME];

	FST* fst = jfst->fst;
	int32_t i;
	for ( i = 0; i < fst_num_params(fst); i++ ) {
		fst_call_dispatcher ( fst, effGetParamName, i, 0, paramName, 0 );
		send_fmt(sock, "%d:%s", i, paramName);
        }
}

static void list_midi_map ( JFST* jfst, int sock ) {
	char name[FST_MAX_PARAM_NAME];

	MidiLearn* ml = &jfst->midi_learn;
	FST* fst = jfst->fst;
	uint8_t cc;
	for (cc = 0; cc < 128; cc++) {
		int32_t paramIndex = ml->map[cc];
		if ( paramIndex < 0 || paramIndex >= fst_num_params(fst) )
			continue;

		fst_call_dispatcher( fst, effGetParamName, paramIndex, 0, name, 0 );
		send_fmt(sock, "%d:%s", cc, name);
	}
}

static void get_program ( JFST* jfst, int sock ) {
	send_fmt( sock, "PROGRAM:%d", jfst->fst->current_program );
}

static void get_channel ( JFST* jfst, int sock ) {
	send_fmt( sock, "CHANNEL:%d", midi_filter_one_channel_get(&jfst->channel) );
}

static void get_volume ( JFST* jfst, int sock ) {
	send_fmt( sock, "VOLUME:%d", jfst_get_volume(jfst) );
}

static void cpu_usage ( int sock ) {
	send_fmt( sock, "%g", CPUusage_getCurrentValue() );
}

static void news ( JFST* jfst, int sock, uint8_t* changes ) {

	if ( *changes & CHANGE_BYPASS )
		send_fmt( sock, "BYPASS:%d", jfst->bypassed );

	if ( *changes & CHANGE_EDITOR )
		send_fmt( sock, "EDITOR:%d", (bool) jfst->fst->window );

	if ( *changes & CHANGE_CHANNEL )
		get_channel(jfst, sock);

	if ( *changes & CHANGE_VOLUME )
		get_volume(jfst, sock);

	if ( *changes & CHANGE_MIDILE ) {
		MidiLearn* ml = &jfst->midi_learn;
		send_fmt( sock, "MIDI_LEARN:%d", ml->wait );
	}

	if ( *changes & CHANGE_PROGRAM )
		get_program( jfst, sock );

	*changes = 0;
}

typedef enum {
	CMD_UNKNOWN,
	CMD_EDITOR,
	CMD_LIST_PLUGINS,
	CMD_LIST_PROGRAMS,
	CMD_LIST_PARAMS,
	CMD_LIST_MIDI_MAP,
	CMD_GET_PROGRAM,
	CMD_SET_PROGRAM,
	CMD_GET_CHANNEL,
	CMD_SET_CHANNEL,
	CMD_MIDI_LEARN,
	CMD_SET_VOLUME,
	CMD_GET_VOLUME,
	CMD_SUSPEND,
	CMD_RESUME,
	CMD_LOAD,
	CMD_SAVE,
	CMD_NEWS,
	CMD_CPU,
	CMD_HELP,
	CMD_QUIT,
	CMD_KILL
} PROTO_CMD;

struct PROTO_MAP {
	PROTO_CMD key;
	const char* name;
};

static struct PROTO_MAP proto_string_map[] = {
	{ CMD_EDITOR, "editor" },
	{ CMD_LIST_PLUGINS, "list_plugins" },
	{ CMD_LIST_PROGRAMS, "list_programs" },
	{ CMD_LIST_PARAMS, "list_params" },
	{ CMD_LIST_MIDI_MAP, "list_midi_map" },
	{ CMD_GET_PROGRAM, "get_program" },
	{ CMD_SET_PROGRAM, "set_program" },
	{ CMD_GET_CHANNEL, "get_channel" },
	{ CMD_SET_CHANNEL, "set_channel" },
	{ CMD_MIDI_LEARN, "midi_learn" },
	{ CMD_SET_VOLUME, "set_volume" },
	{ CMD_GET_VOLUME, "get_volume" },
	{ CMD_SUSPEND, "suspend" },
	{ CMD_RESUME, "resume" },
	{ CMD_LOAD, "load" },
	{ CMD_SAVE, "save" },
	{ CMD_NEWS, "news" },
	{ CMD_CPU, "cpu" },
	{ CMD_HELP, "help" },
	{ CMD_QUIT, "quit" },
	{ CMD_KILL, "kill" },
	{ CMD_UNKNOWN, NULL }
};

static PROTO_CMD proto_lookup ( const char* name ) {
	short i;
	for ( i = 0; proto_string_map[i].key != CMD_UNKNOWN; i++ ) {
		if ( ! strcasecmp( proto_string_map[i].name, name ) )
			return proto_string_map[i].key;
	}
	return CMD_UNKNOWN;
}

typedef struct {
	const char* cmd;
	const char* plugin;
	const char* value;
	PROTO_CMD proto_cmd;
	JFST* jfst;
	/* return */
	bool ack;
	bool quit;
	bool done;
} CMD;

void msg2cmd( char* msg, CMD* cmd ) {
	char* p = msg;
	short i;
	for ( i=0; i < 3; i++ ) {
		switch ( i ) {
			case 0: cmd->cmd = p; break;
			case 1: cmd->plugin = p; break;
			case 2: cmd->value = p; break;
		}

		// Find and replace next token
		while ( *p != '\0' ) {
			p++; // assume at least one character token
			if ( *p == ':' || *p == ' ' ) {
				*p = '\0';
				p++;
				break;
			}
		}
	}

	cmd->proto_cmd = proto_lookup( cmd->cmd );
	cmd->ack = false;
	cmd->quit = false;
	cmd->done = false;
	cmd->jfst = NULL;
}

static void help( int sock ) {
	char msg[256] = "";
	short i;
	for ( i = 0; proto_string_map[i].key != CMD_UNKNOWN; i++ ) {
		strcat( msg, proto_string_map[i].name );
		strcat( msg, " " );
	}
	serv_send_client_data ( sock, msg );
}

void jfst_proto_dispatch( CMD* cmd, uint8_t* changes, int client_sock ) {
	JFST* jfst = cmd->jfst;
	const char* value = cmd->value;
	cmd->done = cmd->ack = true;

	uint8_t all_changes = -1;
	switch ( cmd->proto_cmd ) {
	case CMD_EDITOR:
		if ( !strcasecmp(value, "open") ) {
			fst_run_editor ( jfst->fst, false );
		} else if ( !strcasecmp(value,"close") ) {
			fst_call ( jfst->fst, EDITOR_CLOSE );
		} else {
			puts ( "Need value: open|close" );
			cmd->ack = false;
		}
		break;
	case CMD_LIST_PROGRAMS:
		list_programs ( jfst, client_sock );
		break;
	case CMD_LIST_PARAMS:
		list_params ( jfst, client_sock );
		break;
	case CMD_LIST_MIDI_MAP:
		list_midi_map( jfst, client_sock );
		break;
	case CMD_GET_PROGRAM:
		get_program ( jfst, client_sock );
		break;
	case CMD_SET_PROGRAM:
		fst_program_change ( jfst->fst, strtol(value, NULL, 10) );
		break;
	case CMD_GET_CHANNEL:
		get_channel ( jfst, client_sock );
		break;
	case CMD_SET_CHANNEL:
		midi_filter_one_channel_set( &jfst->channel, strtol(value, NULL, 10) );
		break;
	case CMD_MIDI_LEARN:
		if ( !strcasecmp(value, "start") ) {
			jfst_midi_learn(jfst, true );
		} else if ( !strcasecmp(value,"stop") ) {
			jfst_midi_learn(jfst, false);
		} else {
			puts ( "Need value: start|stop" );
			cmd->ack = false;
		}
		break;
	case CMD_SET_VOLUME:
		jfst_set_volume(jfst, strtol(value, NULL, 10));
		break;
	case CMD_GET_VOLUME:
		get_volume(jfst, client_sock);
		break;
	case CMD_SUSPEND:
		jfst_bypass ( jfst, true );
		break;
	case CMD_RESUME:
		jfst_bypass ( jfst, false );
		break;
	case CMD_LOAD:
		cmd->ack = jfst_load_state ( jfst, value );
		break;
	case CMD_SAVE:
		cmd->ack = jfst_save_state ( jfst, value );
		break;
	case CMD_NEWS:
		if ( !strcasecmp(value,"all") ) {
			news( jfst, client_sock, &all_changes );
		} else {
			news( jfst, client_sock, changes );
		}
		break;
	case CMD_KILL:
		// TODO: close only this plugin
		fsthost_quit();
		break;
	default:
		cmd->ack = cmd->done = false;
	}
}

/******************** SERV ***********************************/

void fsthost_proto_dispatch ( CMD* cmd, int client_sock ) {
	cmd->ack = cmd->done = true;

	switch ( cmd->proto_cmd ) {
	case CMD_CPU:
		cpu_usage( client_sock );
		break;
	case CMD_QUIT:
		cmd->quit = true;
		break;
	case CMD_HELP:
		help( client_sock );
		break;
	case CMD_UNKNOWN:
		log_error ( "GOT INVALID CMD: %s", cmd );
		cmd->ack = false;
		break;
	default:
		cmd->ack = cmd->done = false;
	}
}

static JFST* jfst_get_by_name( const char* name ) {
	if ( name == '\0' ) return NULL;

	JFST_NODE* jn = jfst_node_get_first();
	for ( ; jn; jn = jn->next ) {
		JFST* jfst = jn->jfst;
		if ( !strcmp( jfst->client_name, name) )
			return jfst;
	}
	return NULL;
}

static bool handle_client_callback ( char* msg, int client_sock, uint8_t* changes, void* data ) {
	CMD cmd;
	msg2cmd( msg, &cmd );

	fsthost_proto_dispatch( &cmd, client_sock );
	if ( cmd.done ) goto quit;

	cmd.jfst = jfst_get_by_name( cmd.plugin );
	if ( ! cmd.jfst ) {
		log_error( "Invalid plugin name \"%s\"", cmd.plugin );
		goto quit;
	}

	jfst_proto_dispatch( &cmd, changes, client_sock );

quit:
	if ( cmd.done ) log_debug ( "GOT VALID MSG: %s", msg );

	// Send ACK / NAK
	serv_send_client_data ( client_sock, (cmd.ack) ? ACK : NAK );

	return ( ! cmd.quit );
}

/* Public functions */
bool fsthost_proto_init ( uint16_t ctrl_port_number ) {
	log_info ( "Starting JFST control server ..." );
	bool ok = serv_init ( ctrl_port_number, handle_client_callback, NULL );
	if ( ! ok ) log_error ( "Cannot create CTRL socket :(" );

	return ok;
}
