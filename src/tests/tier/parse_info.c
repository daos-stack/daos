#include <stdio.h>
#include <string.h>
#include "tier_test.h"
#include "daos_test.h"

static int
parse_record(char *rec, char **pf2)
{
	int rc = 0;
	char *p;

	p = rec;
	while (*p && (*p != ':'))
		++p;
	if (*p != '\0') {
		*p = '\0';
		*pf2 = ++p;
		rc = 1;
	}
	return rc;
}


static int
parse_oid(char *p, struct tier_info *pinfo)
{
	daos_obj_id_t oid;
	int nr;

	if (pinfo->oids.ol_nr_out >= pinfo->oids.ol_nr)
		return 0;
	if (pinfo->oids.ol_oids == NULL)
		return 0;

	nr = sscanf(p, DF_OID, &oid.hi, &oid.lo);
	if (nr == 3) {
		pinfo->oids.ol_oids[pinfo->oids.ol_nr_out] = oid;
		pinfo->oids.ol_nr_out += 1;
	}
	return 0;
}

static int
parse_tgt(char *p, struct tier_info *pinfo)
{
	int nr;

	nr = sscanf(p, DF_OID, &pinfo->tgt.hi, &pinfo->tgt.lo);
	return nr == 3 ? 0 : -1;
}

void
tinfo_init(struct tier_info *pinfo, daos_obj_id_t *poids, int max_oids)
{
	memset(pinfo, 0, sizeof(*pinfo));
	pinfo->oids.ol_oids = poids;
	pinfo->oids.ol_nr = max_oids;
	pinfo->oids.ol_nr_out = 0;
}

int
parse_info_file(char *filename, struct tier_info *pinfo)
{
	FILE *fp;
	char lb[128];
	char *value;
	char *key = lb;

	fp = fopen(filename, "r");
	if (!fp) {
		print_message("parse_info_file: failed to open %s\n", filename);
		return -1;
	}
	while (fgets(lb, 127, fp)) {
		if (lb[strlen(lb) - 1] == '\n')
			lb[strlen(lb) - 1] = '\0';
		if (parse_record(key, &value)) {
			if (compare_key(key, GRPNAME_KEY)) {
				strcpy(pinfo->group, value);
			} else if (compare_key(key, POOLID_KEY)) {
				uuid_parse(value, pinfo->pool_uuid);
			} else if (compare_key(key, CONTID_KEY)) {
				uuid_parse(value, pinfo->cont_uuid);
			} else if (compare_key(key, OID_KEY)) {
				parse_oid(value, pinfo);
			} else if (compare_key(key, TGT_KEY)) {
				parse_tgt(value, pinfo);
			} else {
				print_message("parse_info_file:UNK key:%s\n",
					      key);
				return -1;
			}
		}
	}
	fclose(fp);
	return 0;
}
