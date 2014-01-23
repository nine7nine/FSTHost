#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

char* fst_info_default_path( const char* appname ) {
	const char* henv = getenv("HOME");
	size_t len = strlen(henv) + strlen(appname) + 7;
	char* dbif = malloc(len);
	snprintf(dbif, len, "%s/.%s.xml", henv, appname);
	return dbif;
}

int fst_info_list(const char* dbpath) {
	xmlDoc* xml_db = xmlReadFile(dbpath, NULL, 0);
	if (!xml_db) return 1;

	xmlNode* xml_rn = xmlDocGetRootElement(xml_db);
	xmlNode* n;
	for (n = xml_rn->children; n; n = n->next) {
		if (xmlStrcmp(n->name, BAD_CAST "fst")) continue;

		xmlChar* p = xmlGetProp(n, BAD_CAST "path");
		xmlChar* a = xmlGetProp(n, BAD_CAST "arch");
		xmlChar* f = NULL;
		xmlNode* nn;
		for (nn = n->children; nn; nn = nn->next) {
			if (xmlStrcmp(nn->name, BAD_CAST "name") == 0) {
				f = nn->children->content;
				break;
			}
		}
		if (!f) continue;
		printf("%s|%s|%s\n", f, a, p);
	}

	xmlFreeDoc(xml_db);
	return 0;
}
