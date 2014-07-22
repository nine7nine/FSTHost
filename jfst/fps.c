#include <glib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "jfst.h"

// Concept from: http://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
// With small modifications
static char *
trim(char * s) {
    char * p = s;
    int l = strlen(p);

    while(! isgraph(p[l - 1])) p[--l] = 0;
    while(* p && ! isgraph(* p)) ++p, --l;

    memmove(s, p, l + 1);

    return s;
}
// -----------------------------------

static xmlChar *
int2str(xmlChar *str, int buf_len, int integer) {
   xmlStrPrintf(str, buf_len, BAD_CAST "%d", integer);
   return str;
}

static xmlChar *
float2str(xmlChar *str, int buf_len, float floating) {
   xmlStrPrintf(str, buf_len, BAD_CAST "%f", floating);
   return str;
}

char*
fps_get_plugin_file( xmlNode *psn ) {
   xmlNode *n;

   for ( n = psn->children; n; n = n->next) {
      if (xmlStrcmp(n->name, BAD_CAST "file")) continue;
      return (char*) xmlGetProp(n, BAD_CAST "path");
   }
   return NULL;
}

static int
fps_check_this(FST *fst, char *field, char *value) {
   bool success = FALSE;
   char testString[64];

   printf("Check %s : %s == ", field, value);
   if ( strcmp( field, "uniqueID" ) == 0 ) {
      int32_t ival = strtol( value, NULL, 10 );
      if ( ival == fst->plugin->uniqueID ) {
         printf("%d [PASS]\n", ival);
         return TRUE;
      } else {
         printf("%d [FAIL]\nUniqueID mismatch!\n", ival);
         return FALSE;
      }
   // NOTE: All below is for compatibility and shall be removed someday
   } else if ( strcmp(  field, "productString" ) == 0 ) {
      success = fst_call_dispatcher( fst, effGetProductString, 0, 0, testString, 0 );
   } else if ( strcmp( field, "effectName" ) == 0 ) {
      success = fst_call_dispatcher( fst, effGetEffectName, 0, 0, testString, 0 );
   } else if ( strcmp( field, "vendorString" ) == 0 ) {
      success = fst_call_dispatcher( fst, effGetVendorString, 0, 0, testString, 0 );
   }

   if (success) {
      if (strcmp (testString, value) == 0) {
         printf("%s [PASS]\n", testString);
         return TRUE;
      }

      printf("%s [FAIL]\nstring mismatch!\n", testString);
   } else {
      printf("empty [FAIL]\nCan't get plugin string\n");
   }

   return FALSE;
}

static int
fps_process_node(JackVST* jvst, xmlNode *a_node) {
    xmlNode *cur_node;
    FST* fst = jvst->fst;

    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
       if (cur_node->type != XML_ELEMENT_NODE) continue;

       // Check
       if (xmlStrcmp(cur_node->name, BAD_CAST "check") == 0) {
          char *field = (char*) xmlGetProp(cur_node, BAD_CAST "field");
          char *value = (char*) xmlGetProp(cur_node, BAD_CAST "value");

          if (! fps_check_this(fst, field, value))
		return FALSE;
       // Map
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "map") == 0) {
          unsigned short cc = strtol((const char*) xmlGetProp(cur_node, BAD_CAST "cc"), NULL, 10);
          int  index = strtol((const char*) xmlGetProp(cur_node, BAD_CAST "index"), NULL, 10);
          char *name = (char *) xmlGetProp(cur_node, BAD_CAST "name");

          printf( "Got map %d = %d (%s)\n", cc, index, name );
          if ( cc < 0 || cc >= 128 || index < 0 || index >= fst->plugin->numParams )
             continue;

          jvst->midi_learn.map[cc] = index;
       // Param
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "param") == 0) {
          if (fst->plugin->flags & effFlagsProgramChunks) {
             printf("FPS: skip param - plugin do expect chunk\n");
             continue;
          }

          int index = strtol((const char*) xmlGetProp(cur_node, BAD_CAST "index"), NULL, 10);
	  float val = strtof((const char*) xmlGetProp(cur_node, BAD_CAST "value"), NULL);

	  pthread_mutex_lock( &fst->lock );
	  fst->plugin->setParameter( fst->plugin, index, val );
	  pthread_mutex_unlock( &fst->lock );
       // MIDI Channel
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "channel") == 0) {
	  int channel = strtol((const char*) xmlGetProp(cur_node, BAD_CAST "number"), NULL, 10);
          midi_filter_one_channel_set( &jvst->channel, channel );
       // MIDI Transposition
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "transposition") == 0) {
	  int value = strtol((const char*) xmlGetProp(cur_node, BAD_CAST "value"), NULL, 10);
          midi_filter_transposition_set( jvst->transposition, value );
       // MIDI Filter
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "filter") == 0 &&
                  midi_filter_one_channel_get(&jvst->channel) < 1)
       {
         const char* prop = NULL;
         MIDIFILTER mf = {0};
         mf.enabled = ( xmlStrcmp(xmlGetProp(cur_node, BAD_CAST "enabled"), BAD_CAST "yes") == 0 ) ? true : false;
	 const xmlChar* type = xmlGetProp(cur_node, BAD_CAST "rule");
         mf.type =  midi_filter_name2key ( (const char*) type );
         if ( mf.type == -1 ) { printf("Wrong filter type\n"); continue; }

         prop = (const char*) xmlGetProp(cur_node, BAD_CAST "channel");
	 mf.channel = (prop) ? strtol(prop, NULL, 10) : 0;

         prop = (const char*) xmlGetProp(cur_node, BAD_CAST "value1");
	 mf.value1 = (prop) ? strtol(prop, NULL, 10) : 0;

         prop = (const char*) xmlGetProp(cur_node, BAD_CAST "value2");
	 mf.value2 = (prop) ? strtol(prop, NULL, 10) : 0;
	 const xmlChar* rule = xmlGetProp(cur_node, BAD_CAST "rule");
         mf.rule =  midi_filter_name2key ( (const char*) rule );
         if ( mf.rule == -1 ) { printf("Wrong filter rule\n"); continue; }

         prop = (const char*) xmlGetProp(cur_node, BAD_CAST "rvalue");
	 mf.rvalue = (prop) ? strtol(prop, NULL, 10) : 0;

         midi_filter_add( &jvst->filters, &mf );
       // MIDI Program Change handling type
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "midi_pc") == 0) {
          if ( xmlStrcmp(xmlGetProp(cur_node, BAD_CAST "type"), BAD_CAST "plugin") == 0 ) {
              jvst->midi_pc = MIDI_PC_PLUG;
          } else if ( xmlStrcmp(xmlGetProp(cur_node, BAD_CAST "type"), BAD_CAST "self") == 0 ) {
              jvst->midi_pc = MIDI_PC_SELF;
          } else {
              printf("FPS: midi_pc : wrong value - allowed: \"plugin\" or \"self\"\n");
          }
       // Volume
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "volume") == 0) {
          jvst_set_volume(jvst, strtol((const char*) xmlGetProp(cur_node, BAD_CAST "level"), NULL, 10));
       // Bypass/Resume MIDI CC
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "mode") == 0) {
	  short cc = (short) strtol((const char*) xmlGetProp(cur_node, BAD_CAST "cc"), NULL, 10);
          if (cc >= 0 && cc <= 127)
             jvst->want_state_cc = cc;
       // SysExDump UUID
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "sysex") == 0) {
	  uint8_t uuid = (uint8_t) strtol((const char*) xmlGetProp(cur_node, BAD_CAST "uuid"), NULL, 10);
          jvst_sysex_set_uuid(jvst, uuid);
       // Current Program
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "program") == 0) {
          short currentProgram = strtol((const char*) xmlGetProp(cur_node, BAD_CAST "number"), NULL, 10);
          fst_program_change(fst, currentProgram);
       // Chunk
       } else if (xmlStrcmp(cur_node->name, BAD_CAST "chunk") == 0) {
          if ( ! (fst->plugin->flags & effFlagsProgramChunks) ) {
             printf("FPS: skip chunk - plugin expect params\n");
             continue;
          }

          int chunk_size = strtoul((const char*) xmlGetProp(cur_node, BAD_CAST "size"), NULL, 0);
          if ( ! chunk_size > 0 ) {
             printf("Error: chunk size: %d", chunk_size);
             return FALSE;
          }

          printf("Loading %dB chunk into plugin ... ", chunk_size);
          char *chunk_base64 = trim((char *) cur_node->children->content);
          gsize out_len;
          void *chunk_data = g_base64_decode(chunk_base64, &out_len);

          if (chunk_size != out_len) {
             printf("[ERROR]\n");
             printf ("Problem while decode base64. DecodedChunkSize: %d\n", (int) out_len);
             return FALSE;
          }
          fst_call_dispatcher( fst, effSetChunk, 0, chunk_size, chunk_data, 0 );
          printf("[DONE]\n");

          g_free(chunk_data);
       } else {
          if (! fps_process_node(jvst, cur_node->children))
		return FALSE;
       }
    }

    return TRUE;
}

bool fps_load(JackVST* jvst, const char* filename) {
   printf("Try load plugin state file: %s\n", filename);

   xmlDoc* doc = xmlReadFile(filename, NULL, 0);
   if (doc == NULL) {
      printf("error: could not parse file %s\n", filename);
      return FALSE;
   }

   xmlNode* plugin_state_node = xmlDocGetRootElement(doc);

   /* If plugin is not already loaded  - try load it now */
   if (! jvst->fst) {
       char* plug_path = fps_get_plugin_file( plugin_state_node );
       if (! jvst_load( jvst, plug_path, false ) ) return FALSE;
   }

   /* Cleanup midi filters */
   midi_filter_cleanup(&jvst->filters, false);

   bool success = fps_process_node(jvst, plugin_state_node);

   xmlFreeDoc(doc);

   return success;
}

// SAVE --------------
bool fps_save (JackVST* jvst, const char* filename) {
   unsigned int cc;
   xmlChar tString[64];
   xmlNode *cur_node;

   FILE * f = fopen (filename, "wb");
   if (! f) {
      printf ("Could not open state file: %s\n", filename);
      return FALSE;
   }

   FST* fst = jvst->fst;
   xmlDoc  *doc = xmlNewDoc(BAD_CAST "1.0");
   xmlNode *plugin_state_node = xmlNewDocRawNode(doc, NULL, BAD_CAST "plugin_state", NULL);
   xmlDocSetRootElement(doc, plugin_state_node);

   // File path
   cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "file", NULL);
   xmlNewProp(cur_node, BAD_CAST "path", BAD_CAST fst->handle->path);

   // Check - UniqueID
   cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "check", NULL);
   xmlNewProp(cur_node, BAD_CAST "field", BAD_CAST "uniqueID");
   xmlNewProp(cur_node, BAD_CAST "value", int2str(tString, sizeof tString, fst->plugin->uniqueID));

   // MIDI Map
   for (cc = 0; cc < 128; cc++ ) {
      int32_t paramIndex = jvst->midi_learn.map[cc];
      if ( paramIndex < 0 || paramIndex >= fst->plugin->numParams ) continue;

      fst_call_dispatcher( fst, effGetParamName, paramIndex, 0, tString, 0 );

      cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "map", NULL);
      xmlNewProp(cur_node, BAD_CAST "name", tString);
      xmlNewProp(cur_node, BAD_CAST "cc", int2str(tString, sizeof tString, cc));
      xmlNewProp(cur_node, BAD_CAST "index", int2str(tString, sizeof tString, paramIndex));
   }

   // MIDI Channel
   cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "channel", NULL);
   xmlNewProp(cur_node, BAD_CAST "number", int2str(tString, sizeof tString, midi_filter_one_channel_get(&jvst->channel)));

   // MIDI Transposition
   cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "transposition", NULL);
   xmlNewProp(cur_node, BAD_CAST "value", int2str(tString, sizeof tString, midi_filter_transposition_get(jvst->transposition)));

   // MIDI Filter
   MIDIFILTER *mf;
   for (mf = jvst->filters; mf; mf = mf->next) {
      if (mf->built_in) continue; /* Skip built-in filters */

      cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "filter", NULL);
      xmlNewProp(cur_node, BAD_CAST "enabled", BAD_CAST (mf->enabled ? "yes" : "no") );
      const char *msg_type = midi_filter_key2name ( mf->type );
      if (msg_type) xmlNewProp(cur_node, BAD_CAST "type", BAD_CAST msg_type);

      if (mf->channel) xmlNewProp(cur_node, BAD_CAST "channel", int2str(tString, sizeof tString, (int) mf->channel));
      if (mf->value1) xmlNewProp(cur_node, BAD_CAST "value1", int2str(tString, sizeof tString, (int) mf->value1));
      if (mf->value2) xmlNewProp(cur_node, BAD_CAST "value2", int2str(tString, sizeof tString, (int) mf->value2));
      const char *rule = midi_filter_key2name ( mf->rule );
      if (rule) xmlNewProp(cur_node, BAD_CAST "rule", BAD_CAST rule);

      if (mf->rvalue) xmlNewProp(cur_node, BAD_CAST "rvalue", int2str(tString, sizeof tString, (int) mf->rvalue));
   }

   // MIDI Program Change handling type
   cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "midi_pc", NULL);
   xmlStrPrintf(tString, sizeof tString, BAD_CAST ((jvst->midi_pc == MIDI_PC_SELF) ? "self" : "plugin"));
   xmlNewProp(cur_node, BAD_CAST "type", tString);

   // Volume
   if (jvst->volume != -1) {
      int level = jvst_get_volume(jvst);
      cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "volume", NULL);
      xmlNewProp(cur_node, BAD_CAST "level", int2str(tString, sizeof tString, level));
   }

   // Bypass/Resume MIDI CC
   if (jvst->want_state_cc >= 0 && jvst->want_state_cc <= 127) {
      cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "mode", NULL);
      int cc = (int) jvst->want_state_cc;
      xmlNewProp(cur_node, BAD_CAST "cc", int2str(tString, sizeof tString, cc));
   }

   // SysExDump UUID
   cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "sysex", NULL);
   xmlNewProp(cur_node, BAD_CAST "uuid", int2str(tString, sizeof tString, jvst->sysex_dump.uuid));

   // Current Program
   cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "program", NULL);
   xmlNewProp(cur_node, BAD_CAST "number", int2str(tString, sizeof tString, (int) jvst->fst->current_program));

   // Chunk
   if ( fst->plugin->flags & effFlagsProgramChunks ) {
      printf( "getting chunk ... " );
      void * chunk_data;
      intptr_t chunk_size = fst_call_dispatcher( fst, effGetChunk, 0, 0, &chunk_data, 0 );
      printf( "%d B [DONE]\n", (int) chunk_size );

      if ( chunk_size <= 0 ) {
         printf( "Chunke len =< 0 !!! Not saving chunk.\n" );
         return FALSE;
      }

      char *encoded = g_base64_encode( chunk_data, chunk_size );
      cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "chunk", BAD_CAST encoded);
      xmlNewProp(cur_node, BAD_CAST "size", int2str(tString, sizeof tString, chunk_size));
      g_free( encoded );
   // Params
   } else {
      int32_t paramIndex;
      for ( paramIndex=0; paramIndex < fst->plugin->numParams; paramIndex++ ) {
         float val = fst->plugin->getParameter( fst->plugin, paramIndex );
         cur_node = xmlNewChild(plugin_state_node, NULL, BAD_CAST "param", NULL);
         xmlNewProp(cur_node, BAD_CAST "index", int2str(tString, sizeof tString, paramIndex));
         xmlNewProp(cur_node, BAD_CAST "value", float2str(tString, sizeof tString, val));
      }
   }

   xmlDocFormatDump(f, doc, TRUE);
   fclose(f);

   xmlFreeDoc(doc);

   return TRUE;
}
