#include <dirent.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "fst.h"

bool need_save = FALSE;
xmlDoc*  xml_db = NULL;
xmlNode* xml_rn = NULL;

static char *
int2str(char *str, int integer) {
   sprintf(str, "%d", integer);
   return str;
}

static char *
bool2str(char *str, bool boolean) {
   if (boolean) {
   	sprintf(str, "TRUE");
   } else {
   	sprintf(str, "FALSE");
   }
   return str;
}

static bool
fst_exists(char *path) {
	xmlNode* fst_node;
	xmlChar fullpath[PATH_MAX];

	if (! realpath(path,fullpath))
		return 10;

	for (fst_node = xml_rn->children; fst_node; fst_node = fst_node->next) {
		if (strcmp(fst_node->name, "fst"))
			continue;

		if (! strcmp(xmlGetProp(fst_node, "path"), fullpath)) {
			printf("%s already exists\n", path);
			return TRUE;
		}
	}

	return FALSE;
}

// most simple one :) could be sufficient.... 
static long
simple_master_callback( struct AEffect *fx, long opcode, long index, long value, void *ptr, float opt ) {
	if ( opcode == audioMasterVersion ) {
		return 2;
	} else {
		return 0;
	}
}

static void fst_add2db(FST* fst) {
	xmlNode* fst_node;
	xmlChar fullpath[PATH_MAX];
	xmlChar tmpstr[32];

	fst_node = xmlNewChild(xml_rn, NULL,"fst", NULL);

	if (! realpath(fst->handle->path,fullpath))
		return;

	xmlNewProp(fst_node,"path",fullpath);

	if ( fst_call_dispatcher( fst, effGetEffectName, 0, 0, tmpstr, 0 ) ) {
		xmlNewChild(fst_node, NULL,"name",tmpstr);
	} else {
		xmlNewChild(fst_node, NULL,"name",fst->handle->name);
	}

	xmlNewChild(fst_node, NULL,"uniqueID", int2str(tmpstr,fst->plugin->uniqueID));
	xmlNewChild(fst_node, NULL,"version", int2str(tmpstr,fst->plugin->version));
	xmlNewChild(fst_node, NULL,"vst_version", int2str(tmpstr,fst->vst_version));
	xmlNewChild(fst_node, NULL, "isSynth", bool2str(tmpstr,&fst->isSynth));
	xmlNewChild(fst_node, NULL, "canReceiveVstEvents", bool2str(tmpstr,fst->canReceiveVstEvents));
	xmlNewChild(fst_node, NULL, "canReceiveVstMidiEvent", bool2str(tmpstr,fst->canReceiveVstMidiEvent));
	xmlNewChild(fst_node, NULL, "canSendVstEvents", bool2str(tmpstr,fst->canSendVstEvents));
	xmlNewChild(fst_node, NULL, "canSendVstMidiEvent", bool2str(tmpstr,fst->canSendVstMidiEvent));
	xmlNewChild(fst_node, NULL, "numInputs", int2str(tmpstr,fst->plugin->numInputs));
	xmlNewChild(fst_node, NULL, "numOutputs", int2str(tmpstr,fst->plugin->numOutputs));
	xmlNewChild(fst_node, NULL, "numParams", int2str(tmpstr,fst->plugin->numParams));
	xmlNewChild(fst_node, NULL, "hasEditor", 
		bool2str(tmpstr,fst->plugin->flags & effFlagsHasEditor ? TRUE : FALSE));

	/* TODO: Category need some changes in vestige (additional enum)
	if( (info->Category = read_string( fp )) == NULL ) goto error;
	*/
}

static void fst_get_info(char* path) {
	FST*		fst;
	FSTHandle*	handle;

	if (! fst_exists(path)) {
		printf("Load plugin %s\n", path);
		handle = fst_load(path);
		if (! handle) {
			fst_error ("can't load plugin %s", path);
			return;
		}

		printf( "Revive plugin: %s\n", handle->name);
		fst = fst_open(handle, (audioMasterCallback) simple_master_callback, NULL);
		if (! fst) {
			fst_error ("can't instantiate plugin %s", handle->name);
			return;
		}

		fst_add2db(fst);

		printf("Close plugin: %s\n", handle->name);
		fst_close(fst);

		need_save = TRUE;

		printf("Unload plugin: %s\n", path);
		fst_unload(handle);
	}
}

static void scandirectory( const char *dir ) {
	struct dirent *entry;
	DIR *d = opendir(dir);

	if ( !d ) {
		fst_error("Can't open directory %s\n", dir);
		return;
	}

	char fullname[PATH_MAX];
	while ( (entry = readdir( d )) ) {
		if (entry->d_type & DT_DIR) {
			/* Check that the directory is not "d" or d's parent. */
            
			if (! strcmp (entry->d_name, "..") || ! strcmp (entry->d_name, "."))
				continue;

			snprintf( fullname, PATH_MAX, "%s/%s", dir, entry->d_name );

			scandirectory(fullname);
		} else if (entry->d_type & DT_REG) {
			if (! strstr( entry->d_name, ".dll" ) && ! strstr( entry->d_name, ".DLL" ))
				continue;

			snprintf( fullname, PATH_MAX, "%s/%s", dir, entry->d_name );
    
			fst_get_info(fullname);
		}
	}
	closedir(d);
}

int fst_info(const char *dbpath, const char *fst_path) {
	xmlKeepBlanksDefault(0);
	xml_db = xmlReadFile(dbpath, NULL, 0);
	if (xml_db) {
		xml_rn = xmlDocGetRootElement(xml_db);
	} else {
		printf("Could not open/parse file %s. Create new one.\n", dbpath);
		xml_db = xmlNewDoc("1.0");
		xml_rn = xmlNewDocRawNode(xml_db, NULL, "fst_database", NULL);
		xmlDocSetRootElement(xml_db, xml_rn);
	}

	scandirectory(fst_path);

	if (need_save) {
		FILE * f = fopen (dbpath, "wb");
		if (! f) {
			printf ("Could not open xml database: %s\n", dbpath);
			return 8;
		}

		xmlDocFormatDump(f, xml_db, TRUE);
		fclose(f);
	}

	xmlFreeDoc(xml_db);
	
	return 0;
}
