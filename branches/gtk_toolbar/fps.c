#include <glib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "jackvst.h"

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

static char *
int2str(char *str, int *integer) {
   sprintf(str, "%d", *integer);
   return str;
}

static char *
float2str(char *str, float *floating) {
   sprintf(str, "%f", *floating);
   return str;
}

static int
fps_check_this(FST *fst, char *field, char *value) {
   bool success;
   char testString[64];

   printf("Check %s : %s == ", field, value);
   if ( strcmp(  field, "productString" ) == 0 ) {
      success = fst_call_dispatcher( fst, effGetProductString, 0, 0, testString, 0 );
   } else if( strcmp( field, "effectName" ) == 0 ) {
      success = fst_call_dispatcher( fst, effGetEffectName, 0, 0, testString, 0 );
   } else if( strcmp( field, "vendorString" ) == 0 ) {
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
fps_process_node(JackVST* jvst, xmlNode *a_node)
{
    xmlNode *cur_node = NULL;
    FST* fst = jvst->fst;

    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
       if (cur_node->type != XML_ELEMENT_NODE)
           continue;

       // Check
       if (strcmp(cur_node->name, "check") == 0) {
          char *field = xmlGetProp(cur_node, "field");
          char *value = xmlGetProp(cur_node, "value");

          if (! fps_check_this(fst, field, value))
		return FALSE;
       // Map
       } else if (strcmp(cur_node->name, "map") == 0) {
          unsigned short cc = strtol(xmlGetProp(cur_node, "cc"), NULL, 10);
          int  index = strtol(xmlGetProp(cur_node, "index"), NULL, 10);
          char *name = (char *) xmlGetProp(cur_node, "name");

          printf( "Got map %d = %d (%s)\n", cc, index, name );
          if ( cc < 0 || cc >= 128 || index < 0 || index >= fst->plugin->numParams )
             continue;

          jvst->midi_map[cc] = index;
       // Param
       } else if (strcmp(cur_node->name, "param") == 0) {
          if (fst->plugin->flags & effFlagsProgramChunks) {
             printf("FPS: skip param - plugin do expect chunk\n");
             continue;
          }

          int index = strtol(xmlGetProp(cur_node, "index"), NULL, 10);
	  float val = strtof(xmlGetProp(cur_node, "value"), NULL);

	  pthread_mutex_lock( &fst->lock );
	  fst->plugin->setParameter( fst->plugin, index, val );
	  pthread_mutex_unlock( &fst->lock );
       // MIDI Channel
       } else if (strcmp(cur_node->name, "channel") == 0) {
	  int channel = strtol(xmlGetProp(cur_node, "number"), NULL, 10);

	  if (channel >= 0 && channel <= 17)
	     jvst->channel = channel;
       // MIDI Program Change handling type
       } else if (strcmp(cur_node->name, "midi_pc") == 0) {
          if ( strcmp(xmlGetProp(cur_node, "type"), "plugin") == 0 ) {
              jvst->midi_pc = MIDI_PC_PLUG;
          } else if ( strcmp(xmlGetProp(cur_node, "type"), "self") == 0 ) {
              jvst->midi_pc = MIDI_PC_SELF;
          } else {
              printf("FPS: midi_pc : wrong value - allowed: \"plugin\" or \"self\"\n");
          }
       // Volume
       } else if (strcmp(cur_node->name, "volume") == 0) {
          jvst_set_volume(jvst, strtol(xmlGetProp(cur_node, "level"), NULL, 10));
       // Bypass/Resume MIDI CC
       } else if (strcmp(cur_node->name, "mode") == 0) {
	  short cc = (short) strtol(xmlGetProp(cur_node, "cc"), NULL, 10);
          if (cc >= 0 && cc <= 127)
             jvst->want_state_cc = cc;
       // Current Program
       } else if (strcmp(cur_node->name, "program") == 0) {
          short currentProgram = strtol(xmlGetProp(cur_node, "number"), NULL, 10);
          fst_program_change(fst, currentProgram);
       // Chunk
       } else if (strcmp(cur_node->name, "chunk") == 0) {
          if (! fst->plugin->flags & effFlagsProgramChunks) {
             printf("FPS: skip chunk - plugin expect params\n");
             continue;
          }

          gsize out_len;
          int  chunk_size;
          void *chunk_data;
          char *chunk_base64;

          chunk_size = strtoul(xmlGetProp(cur_node, "size"), NULL, 0);
          if ( ! chunk_size > 0 ) {
             printf("Error: chunk size: %d", chunk_size);
             return FALSE;
          }

          printf("Loading %dB chunk into plugin ... ", chunk_size);
          chunk_base64 = trim((char *) cur_node->children->content);
          chunk_data = g_base64_decode(chunk_base64, &out_len);

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
   bool success;
   xmlDoc *doc = NULL;
   xmlNode *plugin_state_node = NULL;

   printf("Try load: %s\n", filename);

   doc = xmlReadFile(filename, NULL, 0);

   if (doc == NULL) {
      printf("error: could not parse file %s\n", filename);
      return FALSE;
   }

   plugin_state_node = xmlDocGetRootElement(doc);
   success = fps_process_node(jvst, plugin_state_node);

   xmlFreeDoc(doc);

   return success;
}

// SAVE --------------
static bool
fps_add_check (FST *fst, xmlNode *node, int opcode, const char *field) {
   char tString[64];
   xmlNode *myNode;

   if (fst_call_dispatcher( fst, opcode, 0, 0, tString, 0 )) {
      myNode = xmlNewChild(node, NULL, "check", NULL);
      xmlNewProp(myNode, "field", field);
      xmlNewProp(myNode, "value", tString);
      return TRUE;
   } else {
      printf ("No %s string\n", field);
      return FALSE;
   }
} 

bool fps_save (JackVST* jvst, const char* filename) {
   int paramIndex;
   unsigned int cc;
   char tString[64];
   xmlNode *cur_node;

   FILE * f = fopen (filename, "wb");
   if (! f) {
      printf ("Could not open state file: %s\n", filename);
      return FALSE;
   }

   FST* fst = jvst->fst;
   xmlDoc  *doc = xmlNewDoc("1.0");
   xmlNode *plugin_state_node = xmlNewDocRawNode(doc, NULL, "plugin_state", NULL);
   xmlDocSetRootElement(doc, plugin_state_node);

   // Check
   fps_add_check(fst, plugin_state_node, effGetProductString, "productString");
   fps_add_check(fst, plugin_state_node, effGetVendorString, "vendorString");
   fps_add_check(fst, plugin_state_node, effGetEffectName, "effectName");

   // MIDI Map
   for (cc = 0; cc < 128; cc++ ) {
      paramIndex = jvst->midi_map[cc];
      if( paramIndex < 0 || paramIndex >= fst->plugin->numParams )
          continue;

      fst->plugin->dispatcher( fst->plugin, effGetParamName, paramIndex, 0, tString, 0 );

      cur_node = xmlNewChild(plugin_state_node, NULL, "map", NULL);
      xmlNewProp(cur_node, "name", tString);
      xmlNewProp(cur_node, "cc", int2str(tString, &cc));
      xmlNewProp(cur_node, "index", int2str(tString, &paramIndex));
   }

   // MIDI Channel
   cur_node = xmlNewChild(plugin_state_node, NULL, "channel", NULL);
   xmlNewProp(cur_node, "number", int2str(tString, &jvst->channel));

   // MIDI Program Change handling type
   cur_node = xmlNewChild(plugin_state_node, NULL, "midi_pc", NULL);
   snprintf(tString, sizeof(tString), (jvst->midi_pc > MIDI_PC_PLUG) ? "self" : "plugin");
   xmlNewProp(cur_node, "type", tString);

   // Volume
   if (jvst->volume != -1) {
      int level = jvst_get_volume(jvst);
      cur_node = xmlNewChild(plugin_state_node, NULL, "volume", NULL);
      xmlNewProp(cur_node, "level", int2str(tString, &level));
   }

   // Bypass/Resume MIDI CC
   if (jvst->want_state_cc >= 0 && jvst->want_state_cc <= 127) {
      cur_node = xmlNewChild(plugin_state_node, NULL, "mode", NULL);
      int cc = (int) jvst->want_state_cc;
      xmlNewProp(cur_node, "cc", int2str(tString, &cc));
   }

   // Current Program
   cur_node = xmlNewChild(plugin_state_node, NULL, "program", NULL);
   xmlNewProp(cur_node, "number", int2str(tString, (int *) &jvst->fst->current_program));

   // Chunk
   if ( fst->plugin->flags & effFlagsProgramChunks ) {
      int chunk_size;
      void * chunk_data;
      printf( "getting chunk ... " );
      chunk_size = fst_call_dispatcher( fst, effGetChunk, 0, 0, &chunk_data, 0 );
      printf( "%d B [DONE]\n", chunk_size );

      if ( chunk_size <= 0 ) {
         printf( "Chunke len =< 0 !!! Not saving chunk.\n" );
         return FALSE;
      }

      char *encoded = g_base64_encode( chunk_data, chunk_size );
      cur_node = xmlNewChild(plugin_state_node, NULL, "chunk", encoded);
      xmlNewProp(cur_node, "size", int2str(tString, &chunk_size));
      g_free( encoded );
   // Params
   } else {
      float val;
      for ( paramIndex=0; paramIndex < fst->plugin->numParams; paramIndex++ ) {
         val = fst->plugin->getParameter( fst->plugin, paramIndex );
         cur_node = xmlNewChild(plugin_state_node, NULL, "param", NULL);
         xmlNewProp(cur_node, "index", int2str(tString, &paramIndex));
         xmlNewProp(cur_node, "value", float2str(tString, &val));
      }
   }

   xmlDocFormatDump(f, doc, TRUE);
   fclose(f);

   xmlFreeDoc(doc);

   return TRUE;
}
