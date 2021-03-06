#include <dirent.h>

#include <libgen.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "info.h"
#include "log/log.h"
#include "fst/fst_int.h"

#ifdef __x86_64__
#define ARCH "64"
#else
#define ARCH "32"
#endif

extern xmlDoc* fst_info_read_xmldb ( const char* dbpath );
static bool need_save = false;

static inline xmlChar *
int2str(xmlChar *str, int buf_len, int integer) {
   xmlStrPrintf(str, buf_len, "%d", integer);
   return str;
}

static inline xmlChar *
bool2str(xmlChar *str, int buf_len, bool boolean) {
   xmlStrPrintf(str, buf_len, (boolean) ? "TRUE" : "FALSE");
   return str;
}

static bool
fst_exists(char *path, xmlNode *xml_rn) {
	char fullpath[PATH_MAX];
	if (! realpath(path,fullpath)) return false;

	xmlNode* fst_node;
	for (fst_node = xml_rn->children; fst_node; fst_node = fst_node->next) {
		if (xmlStrcmp(fst_node->name, BAD_CAST "fst")) continue;

		if (! xmlStrcmp(xmlGetProp(fst_node, BAD_CAST "path"), BAD_CAST fullpath)) {
			log_info("%s already exists", path);
			return true;
		}
	}

	return false;
}

static void fst_add2db(FST* fst, xmlNode *xml_rn) {
	xmlNode* fst_node;
	xmlChar tmpstr[32];

	fst_node = xmlNewChild(xml_rn, NULL,BAD_CAST "fst", NULL);

	xmlNewProp(fst_node,BAD_CAST "file",BAD_CAST fst_name(fst));
	xmlNewProp(fst_node,BAD_CAST "path",BAD_CAST fst_path(fst));
	xmlNewProp(fst_node,BAD_CAST "arch",BAD_CAST ARCH);

	xmlNewChild(fst_node, NULL,BAD_CAST "name", BAD_CAST fst_name(fst));
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
	xmlNewChild(fst_node, NULL,BAD_CAST "hasEditor", bool2str(tmpstr,sizeof tmpstr,fst_has_editor(fst)));

	/* TODO: Category need some changes in vestige (additional enum)
	if( (info->Category = read_string( fp )) == NULL ) goto error;
	*/
}

static void fst_get_info(char* path, xmlNode *xml_rn) {
	if (fst_exists(path, xml_rn)) return;

	// Load and open plugin
	FST* fst = fst_load_open(path, NULL);
	if (! fst) return;

	fst_add2db(fst, xml_rn);

	fst_close(fst);

	need_save = true;
}

static void scandirectory( const char *dir, xmlNode *xml_rn ) {
	struct dirent *entry;
	DIR *d = opendir(dir);

	if ( !d ) {
		log_error("Can't open directory %s", dir);
		return;
	}

	size_t d_len = strlen(dir);

	while ( (entry = readdir( d )) ) {
		// Filter unnedded results
		if (entry->d_type & DT_DIR) {
			/* Do not processing self and our parent */
			if (!strcmp (entry->d_name, "..")
			 || !strcmp (entry->d_name, ".")
			) continue;
		} else if (entry->d_type & DT_REG) {
			if (!strstr( entry->d_name, ".dll" )
			 && !strstr( entry->d_name, ".DLL" )
			) continue;
		}

		// Processing entry
		size_t f_len = d_len + 2 + strlen(entry->d_name);
		char fullname[f_len];

		snprintf( fullname, f_len, "%s/%s", dir, entry->d_name );

		if (entry->d_type & DT_DIR) {
			scandirectory(fullname, xml_rn);
		} else if (entry->d_type & DT_REG) {
			fst_get_info(fullname, xml_rn);
		}
	}
	closedir(d);
}

static char* fst_info_get_plugin_path(const char* dbpath, const char* plug_spec) {
	xmlDoc* xml_db = fst_info_read_xmldb ( dbpath );
	if (!xml_db) return NULL;

	char* base = basename ( (char*) plug_spec );
	char* ext = strrchr( base, '.' );
	char* fname = (ext) ? strndup(base, ext - base) : strdup( base );

	char* path = NULL;
	bool found = false;
	xmlNode* xml_rn = xmlDocGetRootElement(xml_db);
	xmlNode* n;
	for (n = xml_rn->children; n; n = n->next) {
		if (xmlStrcmp(n->name, BAD_CAST "fst") != 0) /* is fst node ? */
			continue;

		/* Check ARCH */
		xmlChar* a = xmlGetProp(n, BAD_CAST "arch");
		if ( a ) {
			bool arch_ok = xmlStrcmp(a, BAD_CAST ARCH) == 0;
			xmlFree ( a );
			if ( ! arch_ok ) continue;
		}

		xmlChar* p = xmlGetProp(n, BAD_CAST "path");
		if ( !p ) continue;

		xmlChar* f = xmlGetProp(n, BAD_CAST "file");
		if ( !f ) { /* broken node ? */
			xmlFree( p );
			continue;
		}

		if ( !xmlStrcmp(f, BAD_CAST fname) || !xmlStrcmp(f, BAD_CAST base) ) {
			path = (char*) xmlStrdup ( p );
			found = true;
		}
		xmlFree ( f );

		/* Lookup for name/uuid nodes */
		xmlNode* nn;
		for (nn = n->children; nn; nn = nn->next) {
			if ( xmlStrcmp(nn->name, BAD_CAST "name")     != 0
			  && xmlStrcmp(nn->name, BAD_CAST "uniqueID") != 0
			) continue;

			xmlChar* content = xmlNodeGetContent ( nn );
			if ( xmlStrcmp(content, BAD_CAST plug_spec ) == 0 ) {
				path = (char*) xmlStrdup ( p );
				found = true;
			}
			xmlFree ( content );
		}
		xmlFree ( p );

		if ( found ) break;
	}

	free(fname);
	xmlFreeDoc(xml_db);
	return path;
}

FST* fst_info_load_open ( const char* dbpath, const char* plug_spec, FST_THREAD* fst_th ) {
	log_info ( "Try load directly" );
	FST* fst = fst_load_open ( plug_spec, fst_th );
	if ( fst ) return fst;

	log_info ( "Try load using XML DB" );
	char *p = fst_info_get_plugin_path ( dbpath, plug_spec );
	if (!p) return NULL;

	fst = fst_load_open(p, fst_th);
	free(p);

	return fst; /* Could be NULL */
}

int fst_info_update(const char *dbpath, const char *fst_path) {
	xmlNode* xml_rn = NULL;

	char* xmlpath = (dbpath) ? (char*) dbpath : fst_info_default_path();

	xmlDoc* xml_db = fst_info_read_xmldb ( xmlpath );
	if (xml_db) {
		xml_rn = xmlDocGetRootElement(xml_db);
	} else {
		log_debug("Could not open/parse file %s. Create new one.", xmlpath);
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
		FILE * f = fopen (xmlpath, "wb");
		if (! f) {
			log_error("Could not open xml database: %s", xmlpath);
			return 8;
		}

		xmlDocFormatDump(f, xml_db, true);
		fclose(f);
		log_info ( "xml database updated: %s", xmlpath );
	}

	xmlFreeDoc(xml_db);
	
	return 0;
}
