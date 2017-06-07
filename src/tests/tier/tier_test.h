#ifndef _TIER_TEST_H_
#define _TIER_TEST_H_

#include <uuid/uuid.h>
#include <daos_types.h>

#define GRP_MAX_LEN 128
#define GRPNAME_KEY	"Group Name"
#define POOLID_KEY	"Pool UUID"
#define CONTID_KEY	"Cont UUID"
#define OID_KEY		"OID"
#define TGT_KEY		"TGT"
#define compare_key(k1, k2) (!strncmp(k1, k2, strlen(k2)))

struct tier_info {
	char group[GRP_MAX_LEN];
	uuid_t		pool_uuid;
	uuid_t		cont_uuid;
	daos_obj_id_t   tgt;
	daos_oid_list_t oids;
	int             max_oids;
};


void tinfo_init(struct tier_info *pinfo, daos_obj_id_t *poids, int max_oids);
int parse_info_file(char *filename, struct tier_info *pinfo);

#endif /* _TIER_TEST_H_ */
