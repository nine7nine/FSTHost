#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "jfst/node.h"
#include "log/log.h"

#define ACK "<OK>"
#define NAK "<FAIL>"

static Serv* serv = NULL;

/* fsthost.c */
extern void fsthost_quit();

/* cpuusage.c */
extern void CPUusage_init();
extern double CPUusage_getCurrentValue();

static void jfst_send_fmt ( JFST* jfst, ServClient* serv_client, const char* fmt, ... ) {
	va_list ap;
	char msg[512];
	char new_fmt[128];

	sprintf( new_fmt, "%s:%s", jfst->client_name, fmt );

	va_start (ap, fmt);
	vsnprintf (msg, sizeof msg, new_fmt, ap);
	serv_client_send_data ( serv_client, msg );
	va_end (ap);
}

static void list_plugins ( ServClient* serv_client ) {
	JFST_NODE* jn = jfst_node_get_first();
	for ( ; jn; jn = jn->next ) {
		JFST* jfst = jn->jfst;
		serv_client_send_data( serv_client, jfst->client_name );
	}
}

static void list_programs ( JFST* jfst, ServClient* serv_client ) {
	char progName[FST_MAX_PROG_NAME];

	FST* fst = jfst->fst;
	int32_t i;
	for ( i = 0; i < fst_num_presets(fst); i++ ) {
		fst_get_program_name(fst, i, progName, sizeof(progName));
		jfst_send_fmt(jfst, serv_client, "%d:%s", i, progName);
        }
}

static void list_params ( JFST* jfst, ServClient* serv_client ) {
	char paramName[FST_MAX_PARAM_NAME];

	FST* fst = jfst->fst;
	int32_t i;
	for ( i = 0; i < fst_num_params(fst); i++ ) {
		fst_call_dispatcher ( fst, effGetParamName, i, 0, paramName, 0 );
		jfst_send_fmt(jfst, serv_client, "%d:%s", i, paramName);
        }
}

static void list_midi_map ( JFST* jfst, ServClient* serv_client ) {
	char name[FST_MAX_PARAM_NAME];

	MidiLearn* ml = &jfst->midi_learn;
	FST* fst = jfst->fst;
	uint8_t cc;
	for (cc = 0; cc < 128; cc++) {
		int32_t paramIndex = ml->map[cc];
		if ( paramIndex < 0 || paramIndex >= fst_num_params(fst) )
			continue;

		fst_call_dispatcher( fst, effGetParamName, paramIndex, 0, name, 0 );
		jfst_send_fmt(jfst, serv_client, "%d:%s", cc, name);
	}
}

static void get_program ( JFST* jfst, ServClient* serv_client ) {
	jfst_send_fmt( jfst, serv_client, "PROGRAM:%d", jfst->fst->current_program );
}

static void get_channel ( JFST* jfst, ServClient* serv_client ) {
	jfst_send_fmt( jfst, serv_client, "CHANNEL:%d", midi_filter_one_channel_get(&jfst->channel) );
}

static void get_volume ( JFST* jfst, ServClient* serv_client ) {
	jfst_send_fmt( jfst, serv_client, "VOLUME:%d", jfst_get_volume(jfst) );
}

static void cpu_usage ( ServClient* serv_client ) {
	char msg[16];
	snprintf( msg, sizeof msg, "%g", CPUusage_getCurrentValue() );
	serv_client_send_data ( serv_client, msg );
}

static void jfst_news ( JFST* jfst, ServClient* serv_client, Changes* changes ) {
	if ( *changes & CHANGE_BYPASS )
		jfst_send_fmt( jfst, serv_client, "BYPASS:%d", jfst->bypassed );

	if ( *changes & CHANGE_EDITOR )
		jfst_send_fmt( jfst, serv_client, "EDITOR:%d", (bool) jfst->fst->window );

	if ( *changes & CHANGE_CHANNEL )
		get_channel(jfst, serv_client);

	if ( *changes & CHANGE_VOLUME )
		get_volume(jfst, serv_client);

	if ( *changes & CHANGE_MIDILE ) {
		MidiLearn* ml = &jfst->midi_learn;
		jfst_send_fmt( jfst, serv_client, "MIDI_LEARN:%d", ml->wait );
	}

	if ( *changes & CHANGE_PROGRAM )
		get_program( jfst, serv_client );

	*changes = 0;
}

static void news ( ServClient* serv_client, bool all ) {
	JFST_NODE* jn = jfst_node_get_first();
	for ( ; jn; jn = jn->next ) {
		/* Change for that client/jfst pair */
		Changes* jfst_change = &( jn->changes[serv_client->number] );

		if ( all ) {
			Changes all_changes = (unsigned int) -1;
			*jfst_change = all_changes;
		}

		jfst_news ( jn->jfst, serv_client, jfst_change );
	}
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

static void help( ServClient* serv_client ) {
	char msg[256] = "";
	short i;
	for ( i = 0; proto_string_map[i].key != CMD_UNKNOWN; i++ ) {
		strcat( msg, proto_string_map[i].name );
		strcat( msg, " " );
	}
	serv_client_send_data ( serv_client, msg );
}

void jfst_proto_dispatch( ServClient* serv_client, CMD* cmd ) {
	JFST* jfst = cmd->jfst;
	const char* value = cmd->value;
	cmd->done = cmd->ack = true;

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
		list_programs ( jfst, serv_client );
		break;
	case CMD_LIST_PARAMS:
		list_params ( jfst, serv_client );
		break;
	case CMD_LIST_MIDI_MAP:
		list_midi_map( jfst, serv_client );
		break;
	case CMD_GET_PROGRAM:
		get_program ( jfst, serv_client );
		break;
	case CMD_SET_PROGRAM:
		fst_program_change ( jfst->fst, strtol(value, NULL, 10) );
		break;
	case CMD_GET_CHANNEL:
		get_channel ( jfst, serv_client );
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
		get_volume(jfst, serv_client);
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
	case CMD_KILL:
		// TODO: close only this plugin
		fsthost_quit();
		break;
	default:
		cmd->ack = cmd->done = false;
	}
}

/******************** SERV ***********************************/

void fsthost_proto_dispatch ( ServClient* serv_client, CMD* cmd ) {
	cmd->ack = cmd->done = true;

	switch ( cmd->proto_cmd ) {
	case CMD_LIST_PLUGINS:
		list_plugins ( serv_client );
		break;
	case CMD_CPU:
		cpu_usage( serv_client );
		break;
	case CMD_QUIT:
		cmd->quit = true;
		break;
	case CMD_HELP:
		help( serv_client );
		break;
	case CMD_NEWS:
		if ( !strcasecmp(cmd->value,"all") ) {
			news( serv_client, true );
		} else {
			news( serv_client, false );
		}
		break;
	case CMD_UNKNOWN:
		log_error ( "GOT INVALID CMD: %s", cmd->proto_cmd );
		cmd->ack = false;
		break;
	default:
		cmd->ack = cmd->done = false;
	}
}

static JFST_NODE* jfst_node_get_by_name( const char* name ) {
	if ( name == '\0' ) return NULL;

	JFST_NODE* jn = jfst_node_get_first();
	for ( ; jn; jn = jn->next ) {
		JFST* jfst = jn->jfst;
		if ( !strcmp( jfst->client_name, name) )
			return jn;
	}
	return NULL;
}

static bool handle_client_callback ( ServClient* serv_client, char* msg ) {
	CMD cmd;
	msg2cmd( msg, &cmd );

	fsthost_proto_dispatch( serv_client, &cmd );
	if ( cmd.done ) goto quit;

	JFST_NODE* jn = jfst_node_get_by_name( cmd.plugin );
	if ( ! jn ) {
		log_error( "Invalid plugin name \"%s\"", cmd.plugin );
		goto quit;
	}

	cmd.jfst = jn->jfst;
	jfst_proto_dispatch( serv_client, &cmd );

quit:
	if ( cmd.done ) log_debug ( "GOT VALID MSG: %s", msg );

	// Send ACK / NAK
	serv_client_send_data ( serv_client, (cmd.ack) ? ACK : NAK );

	return ( ! cmd.quit );
}

/* Public functions */
bool fsthost_proto_init ( uint16_t ctrl_port_number ) {
	log_info ( "Starting JFST control server ..." );
	serv = serv_init ( ctrl_port_number, handle_client_callback );
	if ( ! serv ) {
		log_error ( "Cannot create CTRL socket :(" );
		return false;
	}
	return true;
}

void proto_poll() {
	if ( ! serv ) return;
	serv_poll(serv);
}

void proto_close() {
	if ( ! serv ) return;
	serv_close (serv);
}
