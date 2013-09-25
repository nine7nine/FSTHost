#include <dirent.h>

#include <libgen.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "fst.h"

#ifdef __x86_64__
#define ARCH "64"
#else
#define ARCH "32"
#endif

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
	char fullpath[PATH_MAX];
	if (! realpath(path,fullpath)) return 10;

	xmlNode* fst_node;
	for (fst_node = xml_rn->children; fst_node; fst_node = fst_node->next) {
		if (xmlStrcmp(fst_node->name, BAD_CAST "fst")) continue;

		if (! xmlStrcmp(xmlGetProp(fst_node, BAD_CAST "path"), BAD_CAST fullpath)) {
			printf("%s already exists\n", path);
			return TRUE;
		}
	}

	return FALSE;
}

static void fst_add2db(FST* fst, xmlNode *xml_rn) {
	xmlNode* fst_node;
	xmlChar tmpstr[32];

	fst_node = xmlNewChild(xml_rn, NULL,BAD_CAST "fst", NULL);

	xmlNewProp(fst_node,BAD_CAST "file",BAD_CAST fst->handle->name);
	xmlNewProp(fst_node,BAD_CAST "path",BAD_CAST fst->handle->path);
	xmlNewProp(fst_node,BAD_CAST "arch",BAD_CAST ARCH);

	xmlNewChild(fst_node, NULL,BAD_CAST "name", BAD_CAST fst->name);
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
	if (fst_exists(path, xml_rn)) return;

	// Load and open plugin
	FST* fst = fst_load_open(path);
	if (! fst) return;

	fst_add2db(fst, xml_rn);

	fst_close(fst);

	need_save = TRUE;
}

static void scandirectory( const char *dir, xmlNode *xml_rn ) {
	struct dirent *entry;
	DIR *d = opendir(dir);

	if ( !d ) {
		fst_error("Can't open directory %s", dir);
		return;
	}

	char fullname[PATH_MAX];
	while ( (entry = readdir( d )) ) {
		if (entry->d_type & DT_DIR) {
			/* Do not processing self and our parent */
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

char* fst_info_get_plugin_path(const char* dbpath, const char* filename) {
	xmlDoc* xml_db = xmlReadFile(dbpath, NULL, 0);
	if (!xml_db) return NULL;

	char* base = basename ( (char*) filename );
	char* ext = strrchr( base, '.' );
	char* fname = (ext) ? strndup(base, ext - base) : strdup( base );

	char* path = NULL;
	xmlNode* xml_rn = xmlDocGetRootElement(xml_db);
	xmlNode* n;
	for (n = xml_rn->children; n; n = n->next) {
		if (xmlStrcmp(n->name, BAD_CAST "fst")) continue;

		char* p = (char*) xmlGetProp(n, BAD_CAST "path");
		char* f = (char*) xmlGetProp(n, BAD_CAST "file");
		if (!p || !f) continue;
	
		if (! strcmp(f, fname) || ! strcmp(f, base)) {
			path = p;
			break;
		}
	}

	free(fname);
	xmlFreeDoc(xml_db);
	return (path) ? strdup (path) : NULL;
}

int fst_info_update(const char *dbpath, const char *fst_path) {
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

	if ( fst_path ) {
		scandirectory(fst_path, xml_rn);
	} else {
		/* Generate using VST_PATH - if fst_path is NULL */
		char* vst_path = getenv("VST_PATH");
		if (! vst_path) return 7;
		
		char* vpath = strtok (vst_path, ":");
		while (vpath) {
			scandirectory(vpath, xml_rn);
			vpath = strtok (NULL, ":");
		}
	}


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
