#include <dirent.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "fst.h"

static bool need_save = FALSE;

static xmlChar *
int2str(xmlChar *str, int buf_len, int integer) {
   xmlStrPrintf(str, buf_len, BAD_CAST "%d", integer);
   return str;
}

static xmlChar *
bool2str(xmlChar *str, int buf_len, bool boolean) {
   xmlStrPrintf(str, buf_len, BAD_CAST ( (boolean) ? "TRUE" : "FALSE" ));
   return str;
}

static bool
fst_exists(char *path, xmlNode *xml_rn) {
	xmlNode* fst_node;
	char fullpath[PATH_MAX];

	if (! realpath(path,fullpath))
		return 10;

	for (fst_node = xml_rn->children; fst_node; fst_node = fst_node->next) {
		if (xmlStrcmp(fst_node->name, BAD_CAST "fst"))
			continue;

		if (! xmlStrcmp(xmlGetProp(fst_node, BAD_CAST "path"), BAD_CAST fullpath)) {
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

static void fst_add2db(FST* fst, xmlNode *xml_rn) {
	xmlNode* fst_node;
	xmlChar tmpstr[32];

	fst_node = xmlNewChild(xml_rn, NULL,BAD_CAST "fst", NULL);

	xmlNewProp(fst_node,BAD_CAST "path",BAD_CAST fst->handle->path);

	if ( fst_call_dispatcher( fst, effGetEffectName, 0, 0, tmpstr, 0 ) ) {
		xmlNewChild(fst_node, NULL,BAD_CAST "name",tmpstr);
	} else {
		xmlNewChild(fst_node, NULL,BAD_CAST "name",BAD_CAST fst->handle->name);
	}

	xmlNewChild(fst_node, NULL,BAD_CAST "uniqueID", int2str(tmpstr,sizeof tmpstr,fst->plugin->uniqueID));
	xmlNewChild(fst_node, NULL,BAD_CAST "version", int2str(tmpstr,sizeof tmpstr,fst->plugin->version));
	xmlNewChild(fst_node, NULL,BAD_CAST "vst_version", int2str(tmpstr,sizeof tmpstr,fst->vst_version));
	xmlNewChild(fst_node, NULL,BAD_CAST "isSynth", bool2str(tmpstr,sizeof tmpstr,&fst->isSynth));
	xmlNewChild(fst_node, NULL,BAD_CAST "canReceiveVstEvents", bool2str(tmpstr,sizeof tmpstr,fst->canReceiveVstEvents));
	xmlNewChild(fst_node, NULL,BAD_CAST "canReceiveVstMidiEvent", bool2str(tmpstr,sizeof tmpstr,fst->canReceiveVstMidiEvent));
	xmlNewChild(fst_node, NULL,BAD_CAST "canSendVstEvents", bool2str(tmpstr,sizeof tmpstr,fst->canSendVstEvents));
	xmlNewChild(fst_node, NULL,BAD_CAST "canSendVstMidiEvent", bool2str(tmpstr,sizeof tmpstr,fst->canSendVstMidiEvent));
	xmlNewChild(fst_node, NULL,BAD_CAST "numInputs", int2str(tmpstr,sizeof tmpstr,fst->plugin->numInputs));
	xmlNewChild(fst_node, NULL,BAD_CAST "numOutputs", int2str(tmpstr,sizeof tmpstr,fst->plugin->numOutputs));
	xmlNewChild(fst_node, NULL,BAD_CAST "numParams", int2str(tmpstr,sizeof tmpstr,fst->plugin->numParams));
	xmlNewChild(fst_node, NULL,BAD_CAST "hasEditor", 
		bool2str(tmpstr,sizeof tmpstr, fst->plugin->flags & effFlagsHasEditor ? TRUE : FALSE));

	/* TODO: Category need some changes in vestige (additional enum)
	if( (info->Category = read_string( fp )) == NULL ) goto error;
	*/
}

static void fst_get_info(char* path, xmlNode *xml_rn) {
	FST*		fst;
	FSTHandle*	handle;

	if (! fst_exists(path, xml_rn)) {
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

		fst_add2db(fst, xml_rn);

		printf("Close plugin: %s\n", handle->name);
		fst_close(fst);

		need_save = TRUE;

		printf("Unload plugin: %s\n", path);
		fst_unload(handle);
	}
}

static void scandirectory( const char *dir, xmlNode *xml_rn ) {
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

			scandirectory(fullname, xml_rn);
		} else if (entry->d_type & DT_REG) {
			if (! strstr( entry->d_name, ".dll" ) && ! strstr( entry->d_name, ".DLL" ))
				continue;

			snprintf( fullname, PATH_MAX, "%s/%s", dir, entry->d_name );
    
			fst_get_info(fullname, xml_rn);
		}
	}
	closedir(d);
}

int fst_info(const char *dbpath, const char *fst_path) {
	xmlDoc*  xml_db = NULL;
	xmlNode* xml_rn = NULL;

	xmlKeepBlanksDefault(0);
	xml_db = xmlReadFile(dbpath, NULL, 0);
	if (xml_db) {
		xml_rn = xmlDocGetRootElement(xml_db);
	} else {
		printf("Could not open/parse file %s. Create new one.\n", dbpath);
		xml_db = xmlNewDoc(BAD_CAST "1.0");
		xml_rn = xmlNewDocRawNode(xml_db, NULL, BAD_CAST "fst_database", NULL);
		xmlDocSetRootElement(xml_db, xml_rn);
	}

	scandirectory(fst_path, xml_rn);

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
