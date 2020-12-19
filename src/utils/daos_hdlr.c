/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/* daos_hdlr.c - resource and operation-specific handler functions
 * invoked by daos(8) utility
 */

#define D_LOGFAC	DD_FAC(client)
#define ENUM_KEY_BUF		128 /* size of each dkey/akey */
#define ENUM_LARGE_KEY_BUF	(512 * 1024) /* 512k large key */
#define ENUM_DESC_NR		5 /* number of keys/records returned by enum */
#define ENUM_DESC_BUF		512 /* all keys/records returned by enum */

#include <stdio.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/checksum.h>
#include <daos/rpc.h>
#include <daos/debug.h>
#include <daos/object.h>

#include "daos_types.h"
#include "daos_api.h"
#include "daos_fs.h"
#include "daos_uns.h"
#include "daos_prop.h"

#include "daos_hdlr.h"

#define NUM_DIRENTS 24
#define MAX_FILENAME 256
#define OID_ARR_SIZE 8

struct fs_copy_dirent {
	dfs_obj_t *dir;
	struct dirent ents[NUM_DIRENTS];
	daos_anchor_t anchor;
	uint32_t num_ents;
};

struct file_dfs {
	enum {POSIX, DAOS} type;
	int fd;
	daos_off_t offset;
	dfs_obj_t *obj;
	dfs_t *dfs;
};

struct dm_args {
	char *src;
	char *dst;
	uuid_t src_p_uuid;
	uuid_t src_c_uuid;
	uuid_t dst_p_uuid;
	uuid_t dst_c_uuid;
	daos_handle_t src_poh;
	daos_handle_t src_coh;
	daos_handle_t dst_poh;
	daos_handle_t dst_coh;
	uint32_t cont_prop_oid;
	uint32_t cont_prop_layout;
	uint64_t cont_layout;
	uint64_t cont_oid;

};

static int
parse_acl_file(const char *path, struct daos_acl **acl);

/* TODO: implement these pool op functions
 * int pool_stat_hdlr(struct cmd_args_s *ap);
 */

static int
pool_decode_props(daos_prop_t *props)
{
	struct daos_prop_entry		*entry;
	int				rc = 0;

	/* unset properties should get default value */

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_LABEL);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "label property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("label:\t\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_SPACE_RB);
	if (entry == NULL) {
		fprintf(stderr, "rebuild space ratio property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("rebuild space ratio:\t"DF_U64"%%\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_SELF_HEAL);
	if (entry == NULL) {
		fprintf(stderr, "self-healing property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("self-healing:\t\t");
		D_PRINT("%s-exclude,", entry->dpe_val &
				       DAOS_SELF_HEAL_AUTO_EXCLUDE ?
				       "auto" : "manual");
		D_PRINT("%s-rebuild\n", entry->dpe_val &
					DAOS_SELF_HEAL_AUTO_REBUILD ?
					"auto" : "manual");
		if (entry->dpe_val & ~(DAOS_SELF_HEAL_AUTO_EXCLUDE |
				       DAOS_SELF_HEAL_AUTO_REBUILD))
			D_PRINT("unknown bits set in self-healing property ("DF_X64")\n",
				entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_RECLAIM);
	if (entry == NULL) {
		fprintf(stderr, "reclaim property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("reclaim strategy:\t");
		switch (entry->dpe_val) {
		case DAOS_RECLAIM_DISABLED:
			D_PRINT("disabled\n");
			break;
		case DAOS_RECLAIM_LAZY:
			D_PRINT("lazy\n");
			break;
		case DAOS_RECLAIM_SNAPSHOT:
			D_PRINT("snapshot\n");
			break;
		case DAOS_RECLAIM_BATCH:
			D_PRINT("batch\n");
			break;
		case DAOS_RECLAIM_TIME:
			D_PRINT("time\n");
			break;
		default:
			D_PRINT("<unknown value> ("DF_X64")\n", entry->dpe_val);
			break;
		}
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "owner property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("owner:\t\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "owner-group property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("owner-group:\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL) {
		fprintf(stderr, "acl property not found\n");
		rc = -DER_INVAL;
	} else {
		daos_acl_dump(entry->dpe_val_ptr);
	}

	return rc;
}

int
pool_get_prop_hdlr(struct cmd_args_s *ap)
{
	daos_prop_t	*prop_query;
	int		rc = 0;
	int		rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_GET_PROP);

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	prop_query = daos_prop_alloc(0);
	if (prop_query == NULL)
		D_GOTO(out_disconnect, rc = -DER_NOMEM);

	rc = daos_pool_query(ap->pool, NULL, NULL, prop_query, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to query properties for pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	D_PRINT("Pool properties for "DF_UUIDF" :\n", DP_UUID(ap->p_uuid));

	rc = pool_decode_props(prop_query);

out_disconnect:
	daos_prop_free(prop_query);

	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

int
pool_set_attr_hdlr(struct cmd_args_s *ap)
{
	size_t	value_size;
	int	rc = 0;
	int	rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_SET_ATTR);

	if (ap->attrname_str == NULL || ap->value_str == NULL) {
		fprintf(stderr, "both attribute name and value must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RW, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	value_size = strlen(ap->value_str);
	rc = daos_pool_set_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str,
				(const void * const*)&ap->value_str,
				(const size_t *)&value_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to set attribute '%s' for pool "DF_UUIDF
			": %s (%d)\n", ap->attrname_str, DP_UUID(ap->p_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

int
pool_del_attr_hdlr(struct cmd_args_s *ap)
{
	int rc = 0;
	int rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_DEL_ATTR);

	if (ap->attrname_str == NULL) {
		fprintf(stderr, "attribute name must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RW, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	rc = daos_pool_del_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to delete attribute '%s' for pool "
			DF_UUIDF": %s (%d)\n", ap->attrname_str,
			DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

int
pool_get_attr_hdlr(struct cmd_args_s *ap)
{
	size_t	attr_size, expected_size;
	char	*buf = NULL;
	int	rc = 0;
	int	rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_GET_ATTR);

	if (ap->attrname_str == NULL) {
		fprintf(stderr, "attribute name must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* evaluate required size to get attr */
	attr_size = 0;
	rc = daos_pool_get_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str, NULL,
				&attr_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to retrieve size of attribute '%s' for "
			"pool "DF_UUIDF": %s (%d)\n", ap->attrname_str,
			DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	D_PRINT("Pool's '%s' attribute value: ", ap->attrname_str);
	if (attr_size <= 0) {
		D_PRINT("empty attribute\n");
		D_GOTO(out_disconnect, rc);
	}

	D_ALLOC(buf, attr_size);
	if (buf == NULL)
		D_GOTO(out_disconnect, rc = -DER_NOMEM);

	expected_size = attr_size;
	rc = daos_pool_get_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str,
				(void * const*)&buf, &attr_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to get attribute '%s' for pool "DF_UUIDF
			": %s (%d)\n", ap->attrname_str, DP_UUID(ap->p_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	if (expected_size < attr_size)
		fprintf(stderr, "size required to get attributes has raised, "
			"value has been truncated\n");
	D_PRINT("%s\n", buf);

out_disconnect:
	D_FREE(buf);

	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d\n)", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

int
pool_list_attrs_hdlr(struct cmd_args_s *ap)
{
	size_t	total_size, expected_size, cur = 0, len;
	char	*buf = NULL;
	int	rc = 0;
	int	rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_LIST_ATTRS);

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* evaluate required size to get all attrs */
	total_size = 0;
	rc = daos_pool_list_attr(ap->pool, NULL, &total_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to list attribute for pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	D_PRINT("Pool attributes:\n");
	if (total_size == 0) {
		D_PRINT("No attributes\n");
		D_GOTO(out_disconnect, rc);
	}

	D_ALLOC(buf, total_size);
	if (buf == NULL)
		D_GOTO(out_disconnect, rc = -DER_NOMEM);

	expected_size = total_size;
	rc = daos_pool_list_attr(ap->pool, buf, &total_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to list attribute for pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	if (expected_size < total_size)
		fprintf(stderr, "size required to gather all attributes has raised, list has been truncated\n");
	while (cur < total_size) {
		len = strnlen(buf + cur, total_size - cur);
		if (len == total_size - cur) {
			fprintf(stderr,
				"end of buf reached but no end of string encountered, ignoring\n");
			break;
		}
		D_PRINT("%s\n", buf + cur);
		cur += len + 1;
	}

out_disconnect:
	D_FREE(buf);

	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

int
pool_list_containers_hdlr(struct cmd_args_s *ap)
{
	daos_size_t			 ncont = 0;
	const daos_size_t		 extra_cont_margin = 16;
	struct daos_pool_cont_info	*conts = NULL;
	int				 i;
	int				 rc = 0;
	int				 rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_LIST_CONTAINERS);

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* Issue first API call to get current number of containers */
	rc = daos_pool_list_cont(ap->pool, &ncont, NULL /* cbuf */,
				 NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to retrieve number of containers for "
			"pool "DF_UUIDF": %s (%d)\n", DP_UUID(ap->p_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	/* If no containers, no need for a second call */
	if (ncont == 0)
		D_GOTO(out_disconnect, rc);

	/* Allocate conts[] with some margin to avoid -DER_TRUNC if more
	 * containers were created after the first call
	 */
	ncont += extra_cont_margin;
	D_ALLOC_ARRAY(conts, ncont);
	if (conts == NULL) {
		rc = -DER_NOMEM;
		fprintf(stderr, "failed to allocate memory for "
			"pool "DF_UUIDF": %s (%d)\n", DP_UUID(ap->p_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out_disconnect, 0);
	}

	rc = daos_pool_list_cont(ap->pool, &ncont, conts, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to list containers for pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_free, rc);
	}

	for (i = 0; i < ncont; i++) {
		D_PRINT(DF_UUIDF"\n", DP_UUID(conts[i].pci_uuid));
	}

out_free:
	D_FREE(conts);

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	/* Automatically retry in case of DER_NOMEM.  This is to allow the
	 * NLT testing to correctly stress-test all code paths and not
	 * register any memory leaks.
	 * TODO: Move this retry login into daos_pool_disconnect()
	 * or work out another way to effectively shut down and release
	 * resources in this case.
	 */
	if (rc2 == -DER_NOMEM)
		rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

int
pool_query_hdlr(struct cmd_args_s *ap)
{
	daos_pool_info_t		 pinfo = {0};
	struct daos_pool_space		*ps = &pinfo.pi_space;
	struct daos_rebuild_status	*rstat = &pinfo.pi_rebuild_st;
	int				 i;
	int				rc = 0;
	int				rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_QUERY);

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	pinfo.pi_bits = DPI_ALL;
	rc = daos_pool_query(ap->pool, NULL, &pinfo, NULL, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to query pool "DF_UUIDF": %s (%d)\n",
			DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}
	D_PRINT("Pool "DF_UUIDF", ntarget=%u, disabled=%u, version=%u\n",
		DP_UUID(pinfo.pi_uuid), pinfo.pi_ntargets,
		pinfo.pi_ndisabled, pinfo.pi_map_ver);

	D_PRINT("Pool space info:\n");
	D_PRINT("- Target(VOS) count:%d\n", ps->ps_ntargets);
	for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
		D_PRINT("- %s:\n",
			i == DAOS_MEDIA_SCM ? "SCM" : "NVMe");
		D_PRINT("  Total size: "DF_U64"\n",
			ps->ps_space.s_total[i]);
		D_PRINT("  Free: "DF_U64", min:"DF_U64", max:"DF_U64", "
			"mean:"DF_U64"\n", ps->ps_space.s_free[i],
			ps->ps_free_min[i], ps->ps_free_max[i],
			ps->ps_free_mean[i]);
	}

	if (rstat->rs_errno == 0) {
		char	*sstr;

		if (rstat->rs_version == 0)
			sstr = "idle";
		else if (rstat->rs_done)
			sstr = "done";
		else
			sstr = "busy";

		D_PRINT("Rebuild %s, "DF_U64" objs, "DF_U64" recs\n",
			sstr, rstat->rs_obj_nr, rstat->rs_rec_nr);
	} else {
		D_PRINT("Rebuild failed, rc=%d, status=%d\n",
			rc, rstat->rs_errno);
	}

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

int
cont_check_hdlr(struct cmd_args_s *ap)
{
	daos_obj_id_t		oids[OID_ARR_SIZE];
	daos_handle_t		oit;
	daos_anchor_t		anchor = { 0 };
	uint64_t		begin = 0;
	uint64_t		end = 0;
	unsigned long		duration;
	unsigned long		checked = 0;
	unsigned long		skipped = 0;
	unsigned long		inconsistent = 0;
	uint32_t		oids_nr;
	int			rc, i;

	/* Create a snapshot with OIT */
	rc = daos_cont_create_snap_opt(ap->cont, &ap->epc, NULL,
				       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	if (rc != 0)
		goto out;

	/* Open OIT */
	rc = daos_oit_open(ap->cont, ap->epc, &oit, NULL);
	if (rc != 0) {
		fprintf(stderr, "open of container's OIT failed: "DF_RC"\n",
			DP_RC(rc));
		goto out_snap;
	}

	D_PRINT("check container "DF_UUIDF" stated at: %s\n",
		DP_UUID(ap->c_uuid), ctime(NULL));

	daos_gettime_coarse(&begin);

	while (!daos_anchor_is_eof(&anchor)) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(oit, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			fprintf(stderr,
				"object IDs enumeration failed: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out_close, rc);
		}

		for (i = 0; i < oids_nr; i++) {
			rc = daos_obj_verify(ap->cont, oids[i], ap->epc);
			if (rc == -DER_NOSYS) {
				/* XXX: NOT support to verif EC object yet. */
				skipped++;
				continue;
			}

			checked++;
			if (rc == -DER_MISMATCH) {
				fprintf(stderr,
					"found data inconsistency for object: "
					DF_OID"\n", DP_OID(oids[i]));
				inconsistent++;
				continue;
			}

			if (rc < 0) {
				fprintf(stderr,
					"check object "DF_OID" failed: "
					DF_RC"\n", DP_OID(oids[i]), DP_RC(rc));
				D_GOTO(out_close, rc);
			}
		}
	}

	daos_gettime_coarse(&end);

	duration = end - begin;
	if (duration == 0)
		duration = 1;

	if (rc == 0 || rc == -DER_NOSYS || rc == -DER_MISMATCH) {
		D_PRINT("check container "DF_UUIDF" completed at %s\n"
			"checked: %lu\n"
			"skipped: %lu\n"
			"inconsistent: %lu\n"
			"run_time: %lu seconds\n"
			"scan_speed: %lu objs/sec\n",
			DP_UUID(ap->c_uuid), ctime(NULL), checked, skipped,
			inconsistent, duration, (checked + skipped) / duration);
		rc = 0;
	}

out_close:
	daos_oit_close(oit, NULL);
out_snap:
	cont_destroy_snap_hdlr(ap);
out:
	return rc;
}

/* TODO implement the following container op functions
 * all with signatures similar to this:
 * int cont_FN_hdlr(struct cmd_args_s *ap)
 *
 * int cont_stat_hdlr()
 */

/* this routine can be used to list all snapshots or to map a snapshot name
 * to its epoch number.
 */
int
cont_list_snaps_hdlr(struct cmd_args_s *ap, char *snapname, daos_epoch_t *epoch)
{
	daos_epoch_t	*epochs = NULL;
	char		**names = NULL;
	daos_anchor_t	anchor;
	int		rc;
	int		i;
	int		snaps_count;
	int		expected_count;

	/* evaluate size for listing */
	snaps_count = 0;
	memset(&anchor, 0, sizeof(anchor));
	rc = daos_cont_list_snap(ap->cont, &snaps_count, NULL, NULL, &anchor,
				 NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to retrieve number of snapshots for "
			"container "DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	if (snapname == NULL)
		D_PRINT("Container's snapshots :\n");

	if (!daos_anchor_is_eof(&anchor)) {
		fprintf(stderr, "too many snapshots returned\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (snaps_count == 0) {
		D_PRINT("no snapshots\n");
		D_GOTO(out, rc);
	}

	D_ALLOC_ARRAY(epochs, snaps_count);
	if (epochs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(names, snaps_count);
	if (names == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	for (i = 0; i < snaps_count; i++) {
		D_ALLOC_ARRAY(names[i], DAOS_SNAPSHOT_MAX_LEN);
		if (names[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	expected_count = snaps_count;
	memset(&anchor, 0, sizeof(anchor));
	rc = daos_cont_list_snap(ap->cont, &snaps_count, epochs, names, &anchor,
				 NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to list snapshots for container "
			DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}
	if (expected_count < snaps_count)
		fprintf(stderr, "size required to gather all snapshots has raised, list has been truncated\n");

	if (snapname == NULL) {
		for (i = 0; i < min(expected_count, snaps_count); i++)
			D_PRINT(DF_U64" %s\n", epochs[i], names[i]);
	} else {
		for (i = 0; i < min(expected_count, snaps_count); i++)
			if (strcmp(snapname, names[i]) == 0) {
				if (epoch != NULL)
					*epoch = epochs[i];
				break;
			}
		if (i == min(expected_count, snaps_count)) {
			fprintf(stderr, "%s not found in snapshots list\n",
				snapname);
			rc = -DER_NONEXIST;
		}
	}

out:
	D_FREE(epochs);
	if (names != NULL) {
		for (i = 0; i < snaps_count; i++)
			D_FREE(names[i]);
		D_FREE(names);
	}

	return rc;
}

int
cont_create_snap_hdlr(struct cmd_args_s *ap)
{
	int rc;

	rc = daos_cont_create_snap(ap->cont, &ap->epc, ap->snapname_str, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to create snapshot for container "
			DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	D_PRINT("snapshot/epoch "DF_U64" has been created\n", ap->epc);
out:
	return rc;
}

int
cont_destroy_snap_hdlr(struct cmd_args_s *ap)
{
	daos_epoch_range_t epr;
	int rc;

	if (ap->epc == 0 &&
	    (ap->epcrange_begin == 0 || ap->epcrange_end == 0)) {
		fprintf(stderr, "a single epoch or a range must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (ap->epc != 0 &&
	    (ap->epcrange_begin != 0 || ap->epcrange_end != 0)) {
		fprintf(stderr, "both a single epoch and a range not allowed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (ap->epc != 0) {
		epr.epr_lo = ap->epc;
		epr.epr_hi = ap->epc;
	} else {
		epr.epr_lo = ap->epcrange_begin;
		epr.epr_hi = ap->epcrange_end;
	}

	rc = daos_cont_destroy_snap(ap->cont, epr, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to destroy snapshots for container "
			DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
cont_set_attr_hdlr(struct cmd_args_s *ap)
{
	size_t	value_size;
	int	rc = 0;

	if (ap->attrname_str == NULL || ap->value_str == NULL) {
		fprintf(stderr, "both attribute name and value must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	value_size = strlen(ap->value_str);
	rc = daos_cont_set_attr(ap->cont, 1,
				(const char * const*)&ap->attrname_str,
				(const void * const*)&ap->value_str,
				(const size_t *)&value_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to set attribute '%s' for container "
			DF_UUIDF": %s (%d)\n", ap->attrname_str,
			DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
cont_del_attr_hdlr(struct cmd_args_s *ap)
{
	int rc = 0;

	if (ap->attrname_str == NULL) {
		fprintf(stderr, "attribute name must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_cont_del_attr(ap->cont, 1,
				(const char * const*)&ap->attrname_str, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to delete attribute '%s' for container "
			DF_UUIDF": %s (%d)\n", ap->attrname_str,
			DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
cont_get_attr_hdlr(struct cmd_args_s *ap)
{
	size_t	attr_size, expected_size;
	char	*buf = NULL;
	int	rc = 0;

	if (ap->attrname_str == NULL) {
		fprintf(stderr, "attribute name must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* evaluate required size to get attr */
	attr_size = 0;
	rc = daos_cont_get_attr(ap->cont, 1,
				(const char * const*)&ap->attrname_str, NULL,
				&attr_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to retrieve size of attribute '%s' for "
			"container "DF_UUIDF": %s (%d)\n", ap->attrname_str,
			DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	D_PRINT("Container's '%s' attribute value: ", ap->attrname_str);
	if (attr_size <= 0) {
		D_PRINT("empty attribute\n");
		D_GOTO(out, rc);
	}

	D_ALLOC(buf, attr_size);
	if (buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	expected_size = attr_size;
	rc = daos_cont_get_attr(ap->cont, 1,
				(const char * const*)&ap->attrname_str,
				(void * const*)&buf, &attr_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to get attribute '%s' for container "
			DF_UUIDF": %s (%d)\n", ap->attrname_str,
			DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	if (expected_size < attr_size)
		fprintf(stderr, "size required to get attributes has raised, value has been truncated\n");
	D_PRINT("%s\n", buf);

out:
	D_FREE(buf);

	return rc;
}

int
cont_list_attrs_hdlr(struct cmd_args_s *ap)
{
	size_t	size;
	size_t	total_size;
	size_t	expected_size;
	size_t	cur = 0;
	size_t	len;
	char	*buf = NULL;
	int	rc = 0;

	/* evaluate required size to get all attrs */
	total_size = 0;
	rc = daos_cont_list_attr(ap->cont, NULL, &total_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to retrieve number of attributes for "
			"container "DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	D_PRINT("Container attributes:\n");
	if (total_size == 0) {
		D_PRINT("No attributes\n");
		D_GOTO(out, rc);
	}

	D_ALLOC(buf, total_size);
	if (buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	expected_size = total_size;
	rc = daos_cont_list_attr(ap->cont, buf, &total_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to list attributes for container "
			DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	if (expected_size < total_size)
		fprintf(stderr, "size required to gather all attributes has raised, list has been truncated\n");
	size = min(expected_size, total_size);
	while (cur < size) {
		len = strnlen(buf + cur, size - cur);
		if (len == size - cur) {
			fprintf(stderr,
				"end of buf reached but no end of string encountered, ignoring\n");
			break;
		}
		D_PRINT("%s\n", buf + cur);
		cur += len + 1;
	}

out:
	D_FREE(buf);

	return rc;
}

static int
cont_decode_props(daos_prop_t *props, daos_prop_t *prop_acl)
{
	struct daos_prop_entry		*entry;
	char				type[10];
	int				rc = 0;

	/* unset properties should get default value */

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_LABEL);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "label property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("label:\t\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL) {
		fprintf(stderr, "layout type property not found\n");
		rc = -DER_INVAL;
	} else {
		daos_unparse_ctype(entry->dpe_val, type);
		D_PRINT("layout type:\t\t%s ("DF_X64")\n", type,
			entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_LAYOUT_VER);
	if (entry == NULL) {
		fprintf(stderr, "layout version property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("layout version:\t\t"DF_U64"\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_CSUM);
	if (entry == NULL) {
		fprintf(stderr, "checksum type property not found\n");
		rc = -DER_INVAL;
	} else {
		struct hash_ft *csum;

		D_PRINT("checksum type:\t\t");
		if (entry->dpe_val == DAOS_PROP_CO_CSUM_OFF) {
			D_PRINT("off\n");
		} else {
			csum = daos_mhash_type2algo(
				daos_contprop2hashtype(entry->dpe_val));
			if (csum == NULL)
				D_PRINT("<unknown value> ("DF_X64")\n",
					entry->dpe_val);
			else
				D_PRINT("%s\n", csum->cf_name);
		}
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_CSUM_CHUNK_SIZE);
	if (entry == NULL) {
		fprintf(stderr, "checksum chunk-size property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("checksum chunk-size:\t"DF_U64"\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_CSUM_SERVER_VERIFY);
	if (entry == NULL) {
		fprintf(stderr, "checksum verification on server property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("cksum verif. on server:\t");
		if (entry->dpe_val == DAOS_PROP_CO_CSUM_SV_OFF)
			D_PRINT("off\n");
		else if (entry->dpe_val == DAOS_PROP_CO_CSUM_SV_ON)
			D_PRINT("on\n");
		else
			D_PRINT("<unknown value> ("DF_X64")\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_DEDUP);
	if (entry == NULL) {
		fprintf(stderr, "dedup property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("deduplication:\t\t");
		switch (entry->dpe_val) {
		case DAOS_PROP_CO_DEDUP_OFF:
			D_PRINT("off\n");
			break;
		case DAOS_PROP_CO_DEDUP_MEMCMP:
			D_PRINT("memcmp\n");
			break;
		case DAOS_PROP_CO_DEDUP_HASH:
			D_PRINT("hash\n");
			break;
		default:
			D_PRINT("<unknown value> ("DF_X64")\n", entry->dpe_val);
			break;
		}
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_DEDUP_THRESHOLD);
	if (entry == NULL) {
		fprintf(stderr, "dedup threshold property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("dedup threshold:\t"DF_U64"\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_REDUN_FAC);
	if (entry == NULL) {
		fprintf(stderr, "redundancy factor property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("redundancy factor:\t");
		switch (entry->dpe_val) {
		case DAOS_PROP_CO_REDUN_RF0:
			D_PRINT("rf0\n");
			break;
		case DAOS_PROP_CO_REDUN_RF1:
			D_PRINT("rf1\n");
			break;
		case DAOS_PROP_CO_REDUN_RF2:
			D_PRINT("rf2\n");
			break;
		case DAOS_PROP_CO_REDUN_RF3:
			D_PRINT("rf3\n");
			break;
		case DAOS_PROP_CO_REDUN_RF4:
			D_PRINT("rf4\n");
			break;
		default:
			D_PRINT("<unknown value> ("DF_X64")\n", entry->dpe_val);
			break;
		}
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_REDUN_LVL);
	if (entry == NULL) {
		fprintf(stderr, "redundancy level property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("redundancy level:\t");
		if (entry->dpe_val == DAOS_PROP_CO_REDUN_RACK)
			D_PRINT("rack\n");
		else if (entry->dpe_val == DAOS_PROP_CO_REDUN_NODE)
			D_PRINT("node\n");
		else
			D_PRINT("<unknown value> ("DF_X64")\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_SNAPSHOT_MAX);
	if (entry == NULL) {
		fprintf(stderr, "max snapshots property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("max snapshots:\t\t"DF_U64"\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_COMPRESS);
	if (entry == NULL) {
		fprintf(stderr, "compression type property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("compression type:\t");
		if (entry->dpe_val == DAOS_PROP_CO_COMPRESS_OFF)
			D_PRINT("off\n");
		else if (entry->dpe_val == DAOS_PROP_CO_COMPRESS_LZ4)
			D_PRINT("lz4\n");
		else if (entry->dpe_val == DAOS_PROP_CO_COMPRESS_DEFLATE)
			D_PRINT("deflate\n");
		else if (entry->dpe_val == DAOS_PROP_CO_COMPRESS_DEFLATE1)
			D_PRINT("deflate1\n");
		else if (entry->dpe_val == DAOS_PROP_CO_COMPRESS_DEFLATE2)
			D_PRINT("deflate2\n");
		else if (entry->dpe_val == DAOS_PROP_CO_COMPRESS_DEFLATE3)
			D_PRINT("deflate3\n");
		else if (entry->dpe_val == DAOS_PROP_CO_COMPRESS_DEFLATE4)
			D_PRINT("deflate4\n");
		else
			D_PRINT("<unknown> ("DF_X64")\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_ENCRYPT);
	if (entry == NULL) {
		fprintf(stderr, "encryption type property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("encryption type:\t");
		if (entry->dpe_val == DAOS_PROP_CO_ENCRYPT_OFF)
			D_PRINT("off\n");
		else
			D_PRINT("<unknown value> ("DF_X64")\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_ALLOCED_OID);
	if (entry == NULL) {
		fprintf(stderr, "Container allocated oid property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("Allocated OID:\t\t"DF_U64"\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "owner property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("owner:\t\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "owner-group property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("owner-group:\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_ROOTS);
	if (entry == NULL || entry->dpe_val_ptr == NULL) {
		fprintf(stderr, "roots property not found\n");
		rc = -DER_INVAL;
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_CO_STATUS);
	if (entry == NULL) {
		fprintf(stderr, "status property not found\n");
		rc = -DER_INVAL;
	} else {
		struct daos_co_status	co_stat = { 0 };

		daos_prop_val_2_co_status(entry->dpe_val, &co_stat);
		if (co_stat.dcs_status == DAOS_PROP_CO_HEALTHY)
			D_PRINT("status:\t\t\tHEALTHY\n");
		else if (co_stat.dcs_status == DAOS_PROP_CO_UNCLEAN)
			D_PRINT("status:\t\t\tUNCLEAN\n");
		else
			fprintf(stderr, "bad dcs_status %d\n",
				co_stat.dcs_status);
	}

	/* Only mention ACL if there's something to print */
	if (prop_acl != NULL) {
		entry = daos_prop_entry_get(prop_acl, DAOS_PROP_CO_ACL);
		if (entry != NULL && entry->dpe_val_ptr != NULL) {
			struct daos_acl *acl;

			acl = (struct daos_acl *)entry->dpe_val_ptr;
			D_PRINT("acl:\n");
			rc = daos_acl_to_stream(stdout, acl, false);
			if (rc)
				fprintf(stderr,
					"unable to decode ACL: %s (%d)\n",
					d_errdesc(rc), rc);
		}
	}

	return rc;
}

/* cont_get_prop_hdlr() - get container properties */
int
cont_get_prop_hdlr(struct cmd_args_s *ap)
{
	daos_prop_t	*prop_query;
	daos_prop_t	*prop_acl = NULL;
	int		rc = 0;
	uint32_t	i;
	uint32_t	entry_type;

	/*
	 * Get all props except the ACL first.
	 */
	prop_query = daos_prop_alloc(DAOS_PROP_CO_NUM - 1);
	if (prop_query == NULL)
		return -DER_NOMEM;

	entry_type = DAOS_PROP_CO_MIN + 1;
	for (i = 0; i < prop_query->dpp_nr; entry_type++) {
		if (entry_type == DAOS_PROP_CO_ACL)
			continue; /* skip ACL */
		prop_query->dpp_entries[i].dpe_type = entry_type;
		i++;
	}

	rc = daos_cont_query(ap->cont, NULL, prop_query, NULL);
	if (rc) {
		fprintf(stderr, "failed to query container "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		D_GOTO(err_out, rc);
	}

	/* Fetch the ACL separately in case user doesn't have access */
	rc = daos_cont_get_acl(ap->cont, &prop_acl, NULL);
	if (rc && rc != -DER_NO_PERM) {
		fprintf(stderr, "failed to query container ACL "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		D_GOTO(err_out, rc);
	}

	D_PRINT("Container properties for "DF_UUIDF" :\n", DP_UUID(ap->c_uuid));

	rc = cont_decode_props(prop_query, prop_acl);

err_out:
	daos_prop_free(prop_query);
	daos_prop_free(prop_acl);
	return rc;
}

int
cont_set_prop_hdlr(struct cmd_args_s *ap)
{
	int			 rc;
	struct daos_prop_entry	*entry;
	uint32_t		 i;

	if (ap->props == NULL || ap->props->dpp_nr == 0) {
		fprintf(stderr, "at least one property must be requested\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}

	/* Validate the properties are supported for set */
	for (i = 0; i < ap->props->dpp_nr; i++) {
		entry = &ap->props->dpp_entries[i];
		if (entry->dpe_type != DAOS_PROP_CO_LABEL &&
		    entry->dpe_type != DAOS_PROP_CO_STATUS) {
			fprintf(stderr, "property not supported for set\n");
			D_GOTO(err_out, rc = -DER_INVAL);
		}
	}

	rc = daos_cont_set_prop(ap->cont, ap->props, NULL);
	if (rc) {
		fprintf(stderr, "failed to set properties for container "
			DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		D_GOTO(err_out, rc);
	}

	D_PRINT("Properties were successfully set\n");

err_out:
	return rc;
}

static size_t
get_num_prop_entries_to_add(struct cmd_args_s *ap)
{
	size_t nr = 0;

	if (ap->aclfile)
		nr++;
	if (ap->user)
		nr++;
	if (ap->group)
		nr++;

	return nr;
}

/*
 * Returns the first empty prop entry in ap->props.
 * If ap->props wasn't set previously, a new prop is created.
 */
static int
get_first_empty_prop_entry(struct cmd_args_s *ap,
			   struct daos_prop_entry **entry)
{
	size_t nr = 0;

	nr = get_num_prop_entries_to_add(ap);
	if (nr == 0) {
		*entry = NULL;
		return 0; /* nothing to do */
	}

	if (ap->props == NULL) {
		/*
		 * Note that we don't control the memory this way, the prop is
		 * freed by the external caller
		 */
		ap->props = daos_prop_alloc(nr);
		if (ap->props == NULL) {
			fprintf(stderr,
				"failed to allocate memory while processing "
				"access control parameters\n");
			return -DER_NOMEM;
		}
		*entry = &ap->props->dpp_entries[0];
	} else {
		*entry = &ap->props->dpp_entries[ap->props->dpp_nr];
		ap->props->dpp_nr += nr;
	}

	if (ap->props->dpp_nr > DAOS_PROP_ENTRIES_MAX_NR) {
		fprintf(stderr,
			"too many properties supplied. Try again with "
			"fewer props set.\n");
		return -DER_INVAL;
	}

	return 0;
}

static int
update_props_for_access_control(struct cmd_args_s *ap)
{
	int			rc = 0;
	struct daos_acl		*acl = NULL;
	struct daos_prop_entry	*entry = NULL;

	rc = get_first_empty_prop_entry(ap, &entry);
	if (rc != 0 || entry == NULL)
		return rc;

	D_ASSERT(entry->dpe_type == 0);
	D_ASSERT(entry->dpe_val_ptr == NULL);

	/*
	 * When we allocate new memory here, we always do it in the prop entry,
	 * which is a pointer into ap->props.
	 * This will be freed by the external caller on exit, so we don't have
	 * to worry about it here.
	 */

	if (ap->aclfile) {
		rc = parse_acl_file(ap->aclfile, &acl);
		if (rc != 0)
			return rc;

		entry->dpe_type = DAOS_PROP_CO_ACL;
		entry->dpe_val_ptr = acl;
		acl = NULL; /* acl will be freed with the prop now */

		entry++;
	}

	if (ap->user) {
		if (!daos_acl_principal_is_valid(ap->user)) {
			fprintf(stderr,
				"invalid user name.\n");
			return -DER_INVAL;
		}

		entry->dpe_type = DAOS_PROP_CO_OWNER;
		D_STRNDUP(entry->dpe_str, ap->user, DAOS_ACL_MAX_PRINCIPAL_LEN);
		if (entry->dpe_str == NULL) {
			fprintf(stderr,
				"failed to allocate memory for user name.\n");
			return -DER_NOMEM;
		}

		entry++;
	}

	if (ap->group) {
		if (!daos_acl_principal_is_valid(ap->group)) {
			fprintf(stderr,
				"invalid group name.\n");
			return -DER_INVAL;
		}

		entry->dpe_type = DAOS_PROP_CO_OWNER_GROUP;
		D_STRNDUP(entry->dpe_str, ap->group,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);
		if (entry->dpe_str == NULL) {
			fprintf(stderr,
				"failed to allocate memory for group name.\n");
			return -DER_NOMEM;
		}

		entry++;
	}

	return 0;
}

/* cont_create_hdlr() - create container by UUID */
int
cont_create_hdlr(struct cmd_args_s *ap)
{
	int rc;

	rc = update_props_for_access_control(ap);
	if (rc != 0)
		return rc;

	/** allow creating a POSIX container without a link in the UNS path */
	if (ap->type == DAOS_PROP_CO_LAYOUT_POSIX) {
		dfs_attr_t attr;

		attr.da_id = 0;
		attr.da_oclass_id = ap->oclass;
		attr.da_chunk_size = ap->chunk_size;
		attr.da_props = ap->props;
		rc = dfs_cont_create(ap->pool, ap->c_uuid, &attr, NULL, NULL);
	} else {
		rc = daos_cont_create(ap->pool, ap->c_uuid, ap->props, NULL);
	}

	if (rc != 0) {
		fprintf(stderr, "failed to create container: %s (%d)\n",
			d_errdesc(rc), rc);
		return rc;
	}

	fprintf(stdout, "Successfully created container "DF_UUIDF"\n",
		DP_UUID(ap->c_uuid));

	return rc;
}

/* cont_create_uns_hdlr() - create container and link to
 * POSIX filesystem directory or HDF5 file.
 */
int
cont_create_uns_hdlr(struct cmd_args_s *ap)
{
	struct duns_attr_t	dattr = {0};
	char			type[10];
	int			rc;
	const int		RC_PRINT_HELP = 2;

	/* Required: pool UUID, container type, obj class, chunk_size.
	 * Optional: user-specified container UUID.
	 */
	ARGS_VERIFY_PATH_CREATE(ap, err_rc, rc = RC_PRINT_HELP);

	rc = update_props_for_access_control(ap);
	if (rc != 0)
		return rc;

	uuid_copy(dattr.da_puuid, ap->p_uuid);
	uuid_copy(dattr.da_cuuid, ap->c_uuid);
	dattr.da_type = ap->type;
	dattr.da_oclass_id = ap->oclass;
	dattr.da_chunk_size = ap->chunk_size;
	dattr.da_props = ap->props;

	rc = duns_create_path(ap->pool, ap->path, &dattr);
	if (rc) {
		fprintf(stderr, "duns_create_path() error: %s\n", strerror(rc));
		D_GOTO(err_rc, rc);
	}

	uuid_copy(ap->c_uuid, dattr.da_cuuid);
	daos_unparse_ctype(ap->type, type);
	fprintf(stdout, "Successfully created container "DF_UUIDF" type %s\n",
			DP_UUID(ap->c_uuid), type);

	return 0;

err_rc:
	return rc;
}

int
cont_query_hdlr(struct cmd_args_s *ap)
{
	daos_cont_info_t	cont_info;
	char			oclass[10], type[10];
	int			rc;

	rc = daos_cont_query(ap->cont, &cont_info, NULL, NULL);
	if (rc) {
		fprintf(stderr, "Container query failed, result: %d\n", rc);
		D_GOTO(err_out, rc);
	}

	printf("Pool UUID:\t"DF_UUIDF"\n", DP_UUID(ap->p_uuid));
	printf("Container UUID:\t"DF_UUIDF"\n", DP_UUID(cont_info.ci_uuid));
	printf("Number of snapshots: %i\n", (int)cont_info.ci_nsnapshots);
	printf("Latest Persistent Snapshot: %i\n",
		(int)cont_info.ci_lsnapshot);
	printf("Highest Aggregated Epoch: "DF_U64"\n", cont_info.ci_hae);
	printf("Container redundancy factor: %d\n", cont_info.ci_redun_fac);

	/* TODO: list snapshot epoch numbers, including ~80 column wrap. */

	if (ap->oid.hi || ap->oid.lo) {
		printf("Path is within container, oid: " DF_OID "\n",
			DP_OID(ap->oid));
	}

	if (ap->path != NULL) {
		/* cont_op_hdlr() already did resolve_by_path()
		 * all resulting fields should be populated
		 */
		assert(ap->type != DAOS_PROP_CO_LAYOUT_UNKOWN);

		printf("DAOS Unified Namespace Attributes on path %s:\n",
			ap->path);
		daos_unparse_ctype(ap->type, type);
		printf("Container Type:\t%s\n", type);

		if (ap->type == DAOS_PROP_CO_LAYOUT_POSIX) {
			dfs_t		*dfs;
			dfs_attr_t	attr;

			rc = dfs_mount(ap->pool, ap->cont, O_RDONLY, &dfs);
			if (rc) {
				fprintf(stderr, "failed to mount container "
					DF_UUIDF": %s (%d)\n",
					DP_UUID(ap->c_uuid), strerror(rc), rc);
				D_GOTO(err_out, rc = daos_errno2der(rc));
			}

			dfs_query(dfs, &attr);
			daos_oclass_id2name(attr.da_oclass_id, oclass);
			printf("Object Class:\t%s\n", oclass);
			printf("Chunk Size:\t%zu\n", attr.da_chunk_size);

			rc = dfs_umount(dfs);
			if (rc) {
				fprintf(stderr, "failed to unmount container "
					DF_UUIDF": %s (%d)\n",
					DP_UUID(ap->c_uuid), strerror(rc), rc);
				D_GOTO(err_out, rc = daos_errno2der(rc));
			}
		}
	}

	return 0;

err_out:
	return rc;
}

int
cont_destroy_hdlr(struct cmd_args_s *ap)
{
	int	rc;

	if (ap->path) {
		rc = duns_destroy_path(ap->pool, ap->path);
		if (rc)
			fprintf(stderr, "failed to unlink container path %s:"
				"%s\n", ap->path, strerror(rc));
		else
			fprintf(stdout, "Successfully destroyed path %s\n",
				ap->path);
		return rc;
	}

	rc = daos_cont_destroy(ap->pool, ap->c_uuid, ap->force, NULL);
	if (rc != 0)
		fprintf(stderr, "failed to destroy container "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
	else
		fprintf(stdout, "Successfully destroyed container "
				DF_UUIDF"\n", DP_UUID(ap->c_uuid));

	return rc;
}

static int
parse_filename_dfs(const char *path, char **_obj_name, char **_cont_name)
{
	char	*f1 = NULL;
	char	*f2 = NULL;
	char	*fname = NULL;
	char	*cont_name = NULL;
	int	path_len;
	int	rc = 0;

	if (path == NULL || _obj_name == NULL || _cont_name == NULL)
		return -EINVAL;
	path_len = strlen(path) + 1;

	if (strcmp(path, "/") == 0) {
		D_STRNDUP(*_cont_name, "/", 2);
		if (*_cont_name == NULL)
			return -ENOMEM;
		*_obj_name = NULL;
		return 0;
	}
	D_STRNDUP(f1, path, path_len);
	if (f1 == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	D_STRNDUP(f2, path, path_len);
	if (f2 == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	fname = basename(f1);
	cont_name = dirname(f2);

	if (cont_name[0] != '/') {
		char cwd[1024];

		if (getcwd(cwd, 1024) == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		if (strcmp(cont_name, ".") == 0) {
			D_STRNDUP(cont_name, cwd, 1024);
			if (cont_name == NULL) {
				rc = -ENOMEM;
				goto out;
			}
		} else {
			char *new_dir = calloc(strlen(cwd) + strlen(cont_name)
						+ 1, sizeof(char));

			if (new_dir == NULL) {
				rc = -ENOMEM;
				goto out;
			}

			strcpy(new_dir, cwd);
			if (cont_name[0] == '.') {
				strcat(new_dir, &cont_name[1]);
			} else {
				strcat(new_dir, "/");
				strcat(new_dir, cont_name);
			}
			cont_name = new_dir;
		}
		*_cont_name = cont_name;
	} else {
		D_STRNDUP(*_cont_name, cont_name,
			  strlen(cont_name) + 1);
		if (*_cont_name == NULL) {
			rc = -ENOMEM;
			goto out;
		}
	}
	D_STRNDUP(*_obj_name, fname, strlen(fname) + 1);
	if (*_obj_name == NULL) {
		D_FREE(*_cont_name);
		rc = -ENOMEM;
		goto out;
	}
out:
	D_FREE(f1);
	D_FREE(f2);
	return rc;
}

static ssize_t
write_dfs(struct file_dfs *file_dfs, char *file, void *buf, ssize_t size)
{
	int		rc;
	d_iov_t		iov;
	d_sg_list_t	sgl;

	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	sgl.sg_nr_out = 1;
	d_iov_set(&iov, buf, size);
	rc = dfs_write(file_dfs->dfs, file_dfs->obj, &sgl,
		       file_dfs->offset, NULL);
	if (rc) {
		fprintf(stderr, "dfs_write %s failed (%d %s)\n",
			file, rc, strerror(rc));
		errno = rc;
		size = -1;
		D_GOTO(out, rc);
	}
	file_dfs->offset += (daos_off_t)size;
out:
	return (ssize_t)size;
}

static ssize_t
file_write(struct file_dfs *file_dfs, char *file,
	   void *buf, size_t size)
{
	ssize_t num_bytes_written = 0;

	if (file_dfs->type == POSIX) {
		num_bytes_written = write(file_dfs->fd, buf, size);
	} else if (file_dfs->type == DAOS) {
		num_bytes_written = write_dfs(file_dfs, file, buf, size);
	} else {
		fprintf(stderr, "File type not known: %s type=%d\n",
			file, file_dfs->type);
	}
	if (num_bytes_written < 0) {
		fprintf(stderr, "write error on %s type=%d\n",
			file, file_dfs->type);
	}
	return num_bytes_written;
}

static int
open_dfs(struct file_dfs *file_dfs, char *file, int flags, mode_t mode)
{
	int		rc = 0;
	int		tmp_rc = 0;
	dfs_obj_t	*parent = NULL;
	char		*name = NULL;
	char		*dir_name = NULL;

	parse_filename_dfs(file, &name, &dir_name);
	assert(dir_name);
	rc = dfs_lookup(file_dfs->dfs, dir_name, O_RDWR, &parent, NULL, NULL);
	if (parent == NULL) {
		fprintf(stderr, "dfs_lookup %s failed with error %d\n",
			dir_name, rc);
		D_GOTO(out, rc = EINVAL);
	}
	rc = dfs_open(file_dfs->dfs, parent, name, mode | S_IFREG,
		      flags, 0, 0, NULL, &file_dfs->obj);
	if (rc != 0) {
		fprintf(stderr, "dfs_open %s failed (%d)\n", name, rc);
	}
out:
	if (parent != NULL) {
		tmp_rc = dfs_release(parent);
		if (tmp_rc && rc != 0) {
			fprintf(stderr, "dfs_release %s failed with error %d\n",
				dir_name, rc);
		}
	}
	if (name != NULL)
		D_FREE(name);
	if (dir_name != NULL)
		D_FREE(dir_name);
	return rc;
}

int
file_open(struct file_dfs *file_dfs, char *file, int flags, ...)
{
	/* extract the mode */
	int	rc = 0;
	int	mode_set = 0;
	mode_t	mode = 0;

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
		mode_set = 1;
	}

	if (file_dfs->type == POSIX) {
		if (mode_set) {
			file_dfs->fd = open(file, flags, mode);
		} else {
			file_dfs->fd = open(file, flags);
		}
		if (file_dfs->fd < 0) {
			rc = EINVAL;
			fprintf(stderr, "file_open failed on %s: %d\n",
				file, file_dfs->fd);
		}
	} else if (file_dfs->type == DAOS) {
		rc = open_dfs(file_dfs, file, flags, mode);
		if (rc != 0) {
			fprintf(stderr, "file_open failed on %s: %d\n",
				file, rc);
		}
	} else {
		rc = EINVAL;
		fprintf(stderr, "File type not known: %s type=%d\n",
			file, file_dfs->type);
	}
	return rc;
}

static int
mkdir_dfs(struct file_dfs *file_dfs, const char *path, mode_t *mode)
{
	int		rc = 0;
	int		tmp_rc = 0;
	dfs_obj_t	*parent = NULL;
	char		*name = NULL;
	char		*dname = NULL;

	parse_filename_dfs(path, &name, &dname);
	if (dname == NULL || name == NULL) {
		fprintf(stderr, "parsing filename failed, %s\n", dname);
		D_GOTO(out, rc = EINVAL);
	}
	/* if the "/" path is given to DAOS the dfs_mkdir fails with
	 * INVALID argument, so skip creation of that in DAOS since
	 * it always already exists. This happens when copying from
	 * DAOS -> DAOS from the root, because the first source
	 * directory is always "/"
	 */
	if (name && strcmp(name, "/") != 0) {
		rc = dfs_lookup(file_dfs->dfs, dname,
				O_RDWR, &parent, NULL, NULL);
		if (parent == NULL) {
			fprintf(stderr, "dfs_lookup %s failed\n", dname);
			D_GOTO(out, rc = EINVAL);
		}
		rc = dfs_mkdir(file_dfs->dfs, parent, name, *mode, 0);
		if (rc != 0) {
			/* continue if directory exists, fail otherwise */
			fprintf(stderr, "dfs_mkdir %s failed, %s\n",
				name, strerror(rc));
		}
	}
out:
	if (parent != NULL) {
		tmp_rc = dfs_release(parent);
		if (tmp_rc && rc != 0) {
			fprintf(stderr, "dfs_release %s failed with error %d\n",
				dname, rc);
		}
	}
	if (name != NULL)
		D_FREE(name);
	if (dname != NULL)
		D_FREE(dname);
	return rc;
}

static int
file_mkdir(struct file_dfs *file_dfs, const char *dir, mode_t *mode)
{
	int rc = 0;

	/* continue if directory already exists */
	if (file_dfs->type == POSIX) {
		rc = mkdir(dir, *mode);
		if (rc != 0) {
			/* return error code for POSIX mkdir */
			rc = errno;
			fprintf(stderr, "mkdir %s failed, %s\n",
				dir, strerror(errno));
		}
	} else if (file_dfs->type == DAOS) {
		rc = mkdir_dfs(file_dfs, dir, mode);
		if (rc != 0) {
			/* mkdir_dfs already prints error */
			D_GOTO(out, rc);
		}
	} else {
		rc = EINVAL;
		fprintf(stderr, "File type not known: %s type=%d\n",
			dir, file_dfs->type);
	}
out:
	return rc;
}

static DIR*
opendir_dfs(struct file_dfs *file_dfs, const char *dir)
{
	int	rc = 0;
	struct	fs_copy_dirent *dirp;

	D_ALLOC_PTR(dirp);
	if (dirp == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	rc = dfs_lookup(file_dfs->dfs, dir, O_RDWR, &dirp->dir, NULL, NULL);
	if (rc != 0) {
		fprintf(stderr, "dfs_lookup %s failed\n", dir);
		errno = rc;
		D_FREE(dirp);
	}
	return (DIR *)dirp;
}

static DIR*
file_opendir(struct file_dfs *file_dfs, const char *dir)
{
	DIR *dirp = NULL;

	if (file_dfs->type == POSIX) {
		dirp = opendir(dir);
	} else if (file_dfs->type == DAOS) {
		dirp = opendir_dfs(file_dfs, dir);
	} else {
		fprintf(stderr, "File type not known: %s type=%d\n",
			dir, file_dfs->type);
	}
	return dirp;
}

static struct dirent*
readdir_dfs(struct file_dfs *file_dfs, DIR *_dirp)
{
	int	rc = 0;
	struct	fs_copy_dirent *dirp = (struct fs_copy_dirent *)_dirp;

	if (dirp->num_ents) {
		goto ret;
	}
	dirp->num_ents = NUM_DIRENTS;
	while (!daos_anchor_is_eof(&dirp->anchor)) {
		rc = dfs_readdir(file_dfs->dfs, dirp->dir,
				 &dirp->anchor, &dirp->num_ents,
				dirp->ents);
		if (rc) {
			fprintf(stderr, "dfs_readdir failed (%d %s)\n",
				rc, strerror(rc));
			dirp->num_ents = 0;
			memset(&dirp->anchor, 0, sizeof(dirp->anchor));
			errno = rc;
			return NULL;
		}
		if (dirp->num_ents == 0) {
			continue;
		}
		goto ret;
	}
	return NULL;
ret:
	dirp->num_ents--;
	return &dirp->ents[dirp->num_ents];
}

static struct dirent*
file_readdir(struct file_dfs *file_dfs, DIR *dirp)
{
	struct dirent *entry = NULL;

	if (file_dfs->type == POSIX) {
		entry = readdir(dirp);
	} else if (file_dfs->type == DAOS) {
		entry = readdir_dfs(file_dfs, dirp);
	} else {
		fprintf(stderr, "File type not known, type=%d\n",
			file_dfs->type);
	}
	return entry;
}

static int
stat_dfs(struct file_dfs *file_dfs, const char *path, struct stat *buf)
{
	int		rc = 0;
	int		tmp_rc = 0;
	dfs_obj_t	*parent = NULL;
	char		*name = NULL;
	char		*dir_name = NULL;

	parse_filename_dfs(path, &name, &dir_name);
	assert(dir_name);
	/* Lookup the parent directory */
	rc = dfs_lookup(file_dfs->dfs, dir_name, O_RDWR, &parent, NULL, NULL);
	if (parent == NULL) {
		fprintf(stderr, "dfs_lookup %s failed, %d\n", dir_name, rc);
		errno = rc;
		D_GOTO(out, rc);
	} else {
		/* Stat the path */
		rc = dfs_stat(file_dfs->dfs, parent, name, buf);
		if (rc) {
			fprintf(stderr, "dfs_stat %s failed (%d %s)\n",
				name, rc, strerror(rc));
			errno = rc;
		}
	}
out:
	if (parent != NULL) {
		tmp_rc = dfs_release(parent);
		if (tmp_rc && rc != 0) {
			fprintf(stderr, "dfs_release %s failed with error %d\n",
				dir_name, rc);
		}
	}
	if (name != NULL)
		D_FREE(name);
	if (dir_name != NULL)
		D_FREE(dir_name);
	return rc;
}

static int
file_lstat(struct file_dfs *file_dfs, const char *path, struct stat *buf)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = lstat(path, buf);
		/* POSIX returns -1 on error and sets errno
		 * to the error code
		 */
		if (rc != 0) {
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		rc = stat_dfs(file_dfs, path, buf);
	} else {
		fprintf(stderr, "File type not known, file=%s, type=%d\n",
			path, file_dfs->type);
	}
	return rc;
}

static ssize_t
read_dfs(struct file_dfs *file_dfs,
	 const char *file,
	 void *buf,
	 size_t size)
{
	d_iov_t		iov;
	d_sg_list_t	sgl;
	daos_size_t	got_size;

	d_iov_set(&iov, buf, size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	sgl.sg_nr_out = 1;

	/* execute read operation */
	d_iov_set(&iov, buf, size);
	int rc = dfs_read(file_dfs->dfs, file_dfs->obj, &sgl,
			  file_dfs->offset, &got_size, NULL);
	if (rc) {
		fprintf(stderr, "dfs_read %s failed (%d %s)\n",
			file, rc, strerror(rc));
		errno = rc;
		got_size = -1;
		D_GOTO(out, rc);
	}

	/* update file pointer with number of bytes read */
	file_dfs->offset += (daos_off_t)got_size;
out:
	return (ssize_t)got_size;
}

static ssize_t
file_read(struct file_dfs *file_dfs, char *file,
	  void *buf, size_t size)
{
	ssize_t got_size = 0;

	if (file_dfs->type == POSIX) {
		got_size = read(file_dfs->fd, buf, size);
	} else if (file_dfs->type == DAOS) {
		got_size = read_dfs(file_dfs, file, buf, size);
	} else {
		got_size = -1;
		fprintf(stderr, "File type not known: %s type=%d\n",
			file, file_dfs->type);
	}
	return got_size;
}

static int
closedir_dfs(DIR *_dirp)
{
	struct	fs_copy_dirent *dirp	= (struct fs_copy_dirent *)_dirp;
	int	rc			= dfs_release(dirp->dir);

	if (rc) {
		fprintf(stderr, "dfs_release failed (%d %s)\n",
			rc, strerror(rc));
		rc = EINVAL;
	}
	D_FREE(dirp);
	return rc;
}

static int
file_closedir(struct file_dfs *file_dfs, DIR *dirp)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = closedir(dirp);
		/* POSIX returns -1 on error and sets errno
		 * to the error code
		 */
		if (rc != 0) {
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		/* dfs returns positive error code already */
		rc = closedir_dfs(dirp);
	} else {
		rc = EINVAL;
		fprintf(stderr, "File type not known, type=%d\n",
			file_dfs->type);
	}
	return rc;
}

static int
close_dfs(struct file_dfs *file_dfs, const char *file)
{
	int rc = dfs_release(file_dfs->obj);

	if (rc) {
		fprintf(stderr, "dfs_close %s failed (%d %s)\n",
			file, rc, strerror(rc));
	}
	return rc;
}

static int
file_close(struct file_dfs *file_dfs, const char *file)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = close(file_dfs->fd);
		if (rc == 0) {
			file_dfs->fd = -1;
		} else {
			/* POSIX returns -1 on error and sets errno
			 * to the error code
			 */
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		rc = close_dfs(file_dfs, file);
		if (rc == 0)
			file_dfs->obj = NULL;
	} else {
		rc = EINVAL;
		fprintf(stderr, "File type not known, file=%s, type=%d\n",
			file, file_dfs->type);
	}
	return rc;
}

static int
chmod_dfs(struct file_dfs *file_dfs, const char *file, mode_t mode)
{
	int		rc = 0;
	int		tmp_rc = 0;
	dfs_obj_t	*parent	= NULL;
	char		*name = NULL;
	char		*dir_name = NULL;

	parse_filename_dfs(file, &name, &dir_name);
	assert(dir_name);
	/* Lookup the parent directory */
	rc = dfs_lookup(file_dfs->dfs, dir_name, O_RDWR, &parent, NULL, NULL);
	if (parent == NULL) {
		fprintf(stderr, "dfs_lookup %s failed\n", dir_name);
		errno = rc;
		rc = EINVAL;
	} else {
		rc = dfs_chmod(file_dfs->dfs, parent, name, mode);
		if (rc) {
			fprintf(stderr, "dfs_chmod %s failed (%d %s)",
				name, rc, strerror(rc));
			errno = rc;
		}
	}
	if (parent != NULL) {
		tmp_rc = dfs_release(parent);
		if (tmp_rc && rc != 0) {
			fprintf(stderr, "dfs_release %s failed with error %d\n",
				dir_name, rc);
		}
	}
	if (name != NULL)
		D_FREE(name);
	if (dir_name != NULL)
		D_FREE(dir_name);
	return rc;
}

static int
file_chmod(struct file_dfs *file_dfs, const char *path, mode_t mode)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = chmod(path, mode);
		/* POSIX returns -1 on error and sets errno
		 * to the error code, return positive errno
		 * set similar to dfs
		 */
		if (rc != 0) {
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		rc = chmod_dfs(file_dfs, path, mode);
	} else {
		rc = EINVAL;
		fprintf(stderr, "File type not known=%s, type=%d",
			path, file_dfs->type);
	}
	return rc;
}

static int
fs_copy(struct file_dfs *src_file_dfs,
	struct file_dfs *dst_file_dfs,
	const char *dir_name,
	int dfs_prefix_len,
	const char *fs_dst_prefix)
{
	int	rc = 0;
	DIR	*src_dir = NULL;
	struct	stat st_dir_name;
	char	*filename = NULL;
	char	*dst_filename = NULL;
	char	*next_path = NULL;
	char	*next_dpath = NULL;


	/* stat the source, and make sure it is a directory  */
	rc = file_lstat(src_file_dfs, dir_name, &st_dir_name);
	if (!S_ISDIR(st_dir_name.st_mode)) {
		fprintf(stderr, "Source is not a directory: %s\n", dir_name);
		D_GOTO(out, rc);
	}

	/* begin by opening source directory */
	src_dir = file_opendir(src_file_dfs, dir_name);

	/* check it was opened. */
	if (!src_dir) {
		fprintf(stderr, "Cannot open directory '%s': %s\n",
			dir_name, strerror(errno));
		D_GOTO(out, rc);
	}

	/* initialize DAOS anchor */
	struct fs_copy_dirent *dirp = (struct fs_copy_dirent *)src_dir;

	memset(&dirp->anchor, 0, sizeof(dirp->anchor));
	while (1) {
		struct dirent *entry;
		const char *d_name;

		/* walk source directory */
		entry = file_readdir(src_file_dfs, src_dir);
		if (!entry) {
			/* There are no more entries in this directory, so break
			 * out of the while loop.
			 */
			break;
		}

		d_name = entry->d_name;
		D_ASPRINTF(filename, "%s/%s", dir_name, d_name);
		if (filename == NULL) {
			D_GOTO(out, rc = -DER_NOMEM);
		}

		/* stat the source file */
		struct stat st;

		rc = file_lstat(src_file_dfs, filename, &st);
		if (rc) {
			fprintf(stderr, "Cannot stat path %s, %s\n",
				d_name, strerror(errno));
			D_GOTO(out, rc);
		}

		D_ASPRINTF(dst_filename, "%s/%s", fs_dst_prefix,
			   filename + dfs_prefix_len);
		if (dst_filename == NULL) {
			D_GOTO(out, rc = -DER_NOMEM);
		}

		if (S_ISREG(st.st_mode)) {
			int src_flags        = O_RDONLY;
			int dst_flags        = O_CREAT | O_WRONLY;
			mode_t tmp_mode_file = S_IRUSR | S_IWUSR;

			rc = file_open(src_file_dfs, filename, src_flags,
				       tmp_mode_file);
			if (rc != 0) {
				D_GOTO(out, rc);
			}
			rc = file_open(dst_file_dfs, dst_filename, dst_flags,
				       tmp_mode_file);
			if (rc != 0) {
				D_GOTO(err_file, rc);
			}

			/* read from source file, then write to dest file */
			uint64_t file_length = st.st_size;
			uint64_t total_bytes = 0;
			uint64_t buf_size = 64 * 1024 * 1024;
			void *buf;

			D_ALLOC(buf, buf_size);
			if (buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			while (total_bytes < file_length) {
				size_t left_to_read = buf_size;
				uint64_t bytes_left = file_length - total_bytes;

				if (bytes_left < buf_size) {
					left_to_read = (size_t)bytes_left;
				}
				ssize_t bytes_read = file_read(src_file_dfs,
								filename, buf,
								left_to_read);
				if (bytes_read < 0) {
					fprintf(stderr, "read failed on %s\n",
						filename);
					D_GOTO(err_file, rc = EIO);
				}
				size_t bytes_to_write = (size_t)bytes_read;
				ssize_t bytes_written;

				bytes_written = file_write(dst_file_dfs,
							   dst_filename,
							buf, bytes_to_write);
				if (bytes_written < 0) {
					fprintf(stderr,
						"error writing bytes\n");
					D_GOTO(err_file, rc = EIO);
				}

				total_bytes += bytes_read;
			}
			D_FREE(buf);

			/* reset offsets if there is another file to copy */
			src_file_dfs->offset = 0;
			dst_file_dfs->offset = 0;

			/* set perms on files to original source perms */
			rc = file_chmod(dst_file_dfs, dst_filename, st.st_mode);
			if (rc != 0) {
				fprintf(stderr, "updating dst file "
					"permissions failed (%d)\n", rc);
				D_GOTO(err_file, rc);
			}

			/* close src and dst */
			file_close(src_file_dfs, filename);
			file_close(dst_file_dfs, filename);
		} else if (S_ISDIR(st.st_mode)) {
			/* Check that the directory is not "src_dir"
			 * or src_dirs's parent.
			 */
			if ((strcmp(d_name, "..") != 0) &&
			    (strcmp(d_name, ".") != 0)) {
				next_path = strdup(filename);
				if (next_path == NULL) {
					D_GOTO(out, rc = -DER_NOMEM);
				}
				next_dpath = strdup(dst_filename);
				if (next_dpath == NULL) {
					D_GOTO(out, rc = -DER_NOMEM);
				}

				mode_t tmp_mode_dir = S_IRWXU;

				rc = file_mkdir(dst_file_dfs, next_dpath,
						&tmp_mode_dir);
				/* continue if directory already exists,
				 * fail otherwise
				 */
				if (rc != EEXIST && rc != 0) {
					D_GOTO(out, rc);
				}
				/* Recursively call "fs_copy"
				 * with the new path.
				 */
				rc = fs_copy(src_file_dfs, dst_file_dfs,
					     next_path, dfs_prefix_len,
					     fs_dst_prefix);
				if (rc != 0) {
					fprintf(stderr, "filesystem copy "
						"failed, %d.\n", rc);
					D_GOTO(out, rc);
				}

				/* set original source perms on directories
				 * after copying
				 */
				rc = file_chmod(dst_file_dfs, next_dpath,
						st.st_mode);
				if (rc != 0) {
					fprintf(stderr, "updating destination "
						"permissions failed on %s "
						"(%d)\n", next_dpath, rc);
					D_GOTO(out, rc);
				}
				D_FREE(next_path);
				D_FREE(next_dpath);
			} else {
				/* if this is src_dir or src_dir's parent
				 * continue to next entry if there is one
				 */
				continue;
			}
		} else {
			rc = ENOTSUP;
			fprintf(stderr, "file type is not supported (%d)\n",
				rc);
			D_GOTO(out, rc);
		}
	}
err_file:
	if (src_file_dfs->obj != NULL || src_file_dfs->fd != -1)
		file_close(src_file_dfs, filename);
	if (dst_file_dfs->obj != NULL || dst_file_dfs->fd != -1)
		file_close(dst_file_dfs, filename);
out:
	/* don't try to closedir on something that is not a directory,
	 * otherwise always close it before returning
	 */
	if (S_ISDIR(st_dir_name.st_mode) && (src_dir != NULL)) {
		rc = file_closedir(src_file_dfs, src_dir);
		if (rc != 0) {
			fprintf(stderr, "Could not close '%s': %d\n",
				dir_name, rc);
		}
	}

	if (rc != 0) {
		D_FREE(next_path);
		D_FREE(next_dpath);
	}

	D_FREE(filename);
	D_FREE(dst_filename);
	return rc;
}

inline void
set_dm_args_default(struct dm_args *dm)
{
	dm->src = NULL;
	dm->dst = NULL;
	uuid_clear(dm->src_p_uuid);
	uuid_clear(dm->src_c_uuid);
	uuid_clear(dm->dst_p_uuid);
	uuid_clear(dm->dst_c_uuid);
	dm->src_poh = DAOS_HDL_INVAL;
	dm->src_coh = DAOS_HDL_INVAL;
	dm->dst_poh = DAOS_HDL_INVAL;
	dm->dst_coh = DAOS_HDL_INVAL;
	dm->cont_prop_oid = DAOS_PROP_CO_ALLOCED_OID;
	dm->cont_prop_layout = DAOS_PROP_CO_LAYOUT_TYPE;
	dm->cont_layout = DAOS_PROP_CO_LAYOUT_UNKOWN;
	dm->cont_oid = 0;

}

static int
dm_get_cont_prop(daos_handle_t coh,
		 char *sysname,
		 daos_cont_info_t *cont_info,
		 int size,
		 uint32_t *dpe_types,
		 uint64_t *dpe_vals)
{
	int                     rc = 0;
	int                     i = 0;
	daos_prop_t		*prop = NULL;

	prop = daos_prop_alloc(size);
	if (prop == NULL) {
		fprintf(stderr, "Failed to allocate prop (%d)", rc);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	for (i = 0; i < size; i++) {
		prop->dpp_entries[i].dpe_type = dpe_types[i];
	}

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		fprintf(stderr, "daos_cont_query() failed (%d)", rc);
		daos_prop_free(prop);
		D_GOTO(out, rc);
	}

	for (i = 0; i < size; i++) {
		dpe_vals[i] = prop->dpp_entries[i].dpe_val;
	}

	daos_prop_free(prop);
out:
	return rc;
}

static int
dm_connect(bool is_posix_copy,
	   struct file_dfs *src_file_dfs,
	   struct file_dfs *dst_file_dfs,
	   struct dm_args *ca,
	   char *sysname,
	   char *path,
	   daos_cont_info_t *src_cont_info,
	   daos_cont_info_t *dst_cont_info)
{
	/* check source pool/conts */
	int			rc = 0;
	struct duns_attr_t	dattr = {0};
	daos_prop_t		*props = NULL;
	dfs_attr_t		attr;
	int			size = 2;
	uint32_t		dpe_types[size];
	uint64_t		dpe_vals[size];

	/* open src pool, src cont, and mount dfs */
	if (src_file_dfs->type == DAOS) {
		rc = daos_pool_connect(ca->src_p_uuid, sysname,
				       DAOS_PC_RW, &ca->src_poh, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to connect to destination "
				"pool: %d\n", rc);
			D_GOTO(out, rc);
		}
		rc = daos_cont_open(ca->src_poh, ca->src_c_uuid, DAOS_COO_RW,
				    &ca->src_coh, src_cont_info, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to open source "
				"container: %d\n", rc);
			D_GOTO(err_src_root, rc);
		}
		if (is_posix_copy) {
			rc = dfs_mount(ca->src_poh, ca->src_coh, O_RDWR,
				       &src_file_dfs->dfs);
			if (rc) {
				fprintf(stderr, "dfs mount on source "
					"failed: %d\n", rc);
				D_GOTO(err_src, rc);
			}
		}
	}

	/* only need to query if source is not POSIX, since
	 * this connect call is used by the filesystem and clone
	 * tools
	 */
	if (src_file_dfs->type != POSIX) {
		dpe_types[0] = ca->cont_prop_layout;
		dpe_types[1] = ca->cont_prop_oid;

		rc = dm_get_cont_prop(ca->src_coh, sysname, src_cont_info, size,
				      dpe_types, dpe_vals);
		if (rc != 0) {
			fprintf(stderr, "failed to query source "
				"container: %d\n", rc);
			D_GOTO(err_src, rc);
		}

		ca->cont_layout = dpe_vals[0];
		ca->cont_oid = dpe_vals[1];

		props = daos_prop_alloc(2);
		if (props == NULL) {
			fprintf(stderr, "Failed to allocate prop (%d)", rc);
			D_GOTO(out, rc);
		}
		props->dpp_entries[0].dpe_type = ca->cont_prop_layout;
		props->dpp_entries[0].dpe_val = ca->cont_layout;

		/* only set max oid on created dest containers
		 * if this is a non-posix copy
		 */
		if (!is_posix_copy) {
			props->dpp_entries[1].dpe_type = ca->cont_prop_oid;
			props->dpp_entries[1].dpe_val = ca->cont_oid;
		}
	}

	/* open dst pool, dst cont, and mount dfs */
	if (dst_file_dfs->type == DAOS) {
		/* only connect if destination pool wasn't already opened */
		if (!uuid_is_null(ca->dst_p_uuid)) {
			if (!daos_handle_is_valid(ca->dst_poh)) {
				rc = daos_pool_connect(ca->dst_p_uuid, sysname,
						       DAOS_PC_RW, &ca->dst_poh,
						       NULL, NULL);
				if (rc != 0) {
					fprintf(stderr, "failed to connect to "
						"destination pool: %d\n", rc);
					D_GOTO(err_src, rc);
				}
			}
		/* if the dst pool uuid is null that means that this
		 * is a UNS destination path, so we copy the source
		 * pool uuid into the destination and try to connect
		 * again
		 */
		} else {
			uuid_copy(ca->dst_p_uuid, ca->src_p_uuid);
			rc = daos_pool_connect(ca->dst_p_uuid, sysname,
					       DAOS_PC_RW, &ca->dst_poh,
					       NULL, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to connect to "
					"destination pool: %d\n", rc);
				D_GOTO(err_src, rc);
			}
			uuid_copy(dattr.da_puuid, ca->dst_p_uuid);
			uuid_copy(dattr.da_cuuid, ca->dst_c_uuid);
			dattr.da_type = ca->cont_layout;
			dattr.da_props = props;
			rc = duns_create_path(ca->dst_poh, path,
					      &dattr);
			if (rc != 0) {
				fprintf(stderr, "provide a destination "
					"pool or UNS path of the form:\n\t"
					"--dst </$pool> | </path/to/uns>\n");
					D_GOTO(err_dst_root, rc);
			}
			uuid_copy(ca->dst_c_uuid, dattr.da_cuuid);
		}
		/* try to open container if this is a filesystem copy,
		 * and if it fails try to create a destination,
		 * then attempt to open again
		 */
		rc = daos_cont_open(ca->dst_poh, ca->dst_c_uuid,
				    DAOS_COO_RW, &ca->dst_coh,
				    dst_cont_info, NULL);
		if (rc == -DER_NONEXIST) {
			if (ca->cont_layout == DAOS_PROP_CO_LAYOUT_POSIX) {
				attr.da_props = props;
				attr.da_id = 0;
				attr.da_oclass_id = OC_UNKNOWN;
				attr.da_chunk_size = 0;
				rc = dfs_cont_create(ca->dst_poh,
						     ca->dst_c_uuid,
						     &attr, NULL, NULL);
				if (rc != 0) {
					fprintf(stderr, "failed to "
						"create destination "
						"container: %d\n", rc);
						D_GOTO(err_dst_root,
						       rc);
				}
			} else {
				rc = daos_cont_create(ca->dst_poh,
						      ca->dst_c_uuid,
						      props, NULL);
				if (rc != 0) {
					fprintf(stderr, "failed to "
						"create destination "
						"container: %d\n", rc);
						D_GOTO(err_dst_root,
						       rc);
				}
			}
			rc = daos_cont_open(ca->dst_poh, ca->dst_c_uuid,
					    DAOS_COO_RW, &ca->dst_coh,
					    dst_cont_info, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to open container: "
					"%d\n", rc);
				D_GOTO(err_dst_root, rc);
			}
		} else if (rc != 0) {
			fprintf(stderr, "failed to open container: "
				"%d\n", rc);
			D_GOTO(err_dst_root, rc);

		}
		if (is_posix_copy) {
			rc = dfs_mount(ca->dst_poh, ca->dst_coh, O_RDWR,
				       &dst_file_dfs->dfs);
			if (rc != 0) {
				fprintf(stderr, "dfs mount on destination "
				"failed: %d\n", rc);
				D_GOTO(err_dst, rc);
			}
		}
	}
	D_GOTO(out, rc);
err_dst:
	rc = daos_cont_close(ca->dst_coh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to close destination "
			"container (%d)\n", rc);
	}
err_dst_root:
	rc = daos_pool_disconnect(ca->dst_poh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to disconnect from destintaion "
			"pool "DF_UUIDF ": %s (%d)\n", DP_UUID(ca->src_p_uuid),
			d_errdesc(rc), rc);
	}
err_src:
	rc = daos_cont_close(ca->src_coh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to close source container (%d)\n", rc);
	}
err_src_root:
	rc = daos_pool_disconnect(ca->src_poh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to disconnect from source pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ca->src_p_uuid),
			d_errdesc(rc), rc);
	}
out:
	if (props != NULL)
		daos_prop_free(props);
	return rc;
}

inline void
file_set_defaults_dfs(struct file_dfs *file_dfs)
{
	/* set defaults for file_dfs struct */
	file_dfs->type = DAOS;
	file_dfs->fd = -1;
	file_dfs->offset = 0;
	file_dfs->obj = NULL;
	file_dfs->dfs = NULL;
}

static int
dm_disconnect(bool is_posix_copy,
	      struct dm_args *ca,
	      struct file_dfs *src_file_dfs,
	      struct file_dfs *dst_file_dfs)
{
	int rc = 0;

	if (src_file_dfs->type == DAOS) {
		if (is_posix_copy) {
			rc = dfs_umount(src_file_dfs->dfs);
			if (rc != 0) {
				fprintf(stderr, "failed to unmount "
				"source (%d)\n", rc);
				D_GOTO(out, rc);
			}
		}
		rc = daos_cont_close(ca->src_coh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to close source "
				"container (%d)\n", rc);
			D_GOTO(err_src, rc);
		}
		rc = daos_pool_disconnect(ca->src_poh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to disconnect from source "
				"pool "DF_UUIDF ": %s (%d)\n",
				DP_UUID(ca->src_p_uuid), d_errdesc(rc), rc);
			D_GOTO(out, rc);
		}
	}
err_src:
	if (dst_file_dfs->type == DAOS) {
		if (is_posix_copy) {
			rc = dfs_umount(dst_file_dfs->dfs);
			if (rc != 0) {
				fprintf(stderr, "failed to unmount destination "
					"(%d)\n", rc);
				D_GOTO(out, rc);
			}
		}
		rc = daos_cont_close(ca->dst_coh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to close destination "
				"container (%d)\n", rc);
			D_GOTO(out, rc);
		}
		rc = daos_pool_disconnect(ca->dst_poh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to disconnect from destination "
				"pool "DF_UUIDF ": %s (%d)\n",
				DP_UUID(ca->dst_p_uuid), d_errdesc(rc), rc);
			D_GOTO(out, rc);
		}
	}
out:
	return rc;
}

/*
* Parse a path of the format:
* daos://<pool>/<cont>/<path> | <UNS path> | <POSIX path>
* Modifies "path" to be the relative container path, defaulting to "/".
* Returns 0 if a daos path was successfully parsed.
*/
static int
dm_parse_path(struct file_dfs *file, char *path, size_t path_len,
	      uuid_t *p_uuid, uuid_t *c_uuid)
{
	struct duns_attr_t	dattr = {0};
	int			rc = 0;

	rc = duns_resolve_path(path, &dattr);
	if (rc == 0) {
		uuid_copy(*p_uuid, dattr.da_puuid);
		uuid_copy(*c_uuid, dattr.da_cuuid);
		if (dattr.da_rel_path == NULL) {
			strncpy(path, "/", path_len);
		} else {
			strncpy(path, dattr.da_rel_path, path_len);
		}
	/* no prefix option is only for DAOS->DAOS (cont clone),
	 * so this is an error
	 */
	} else if (strncmp(path, "daos://", 7) == 0) {
		/* Error, since we expect a DAOS path */
		D_GOTO(out, rc = 1);
	} else {
		/* not a DAOS path, set type to POSIX,
		 * POSIX dir will be checked with stat
		 * at the beginning of fs_copy
		 */
		rc = 0;
		file->type = POSIX;
	}
out:
	D_FREE(dattr.da_rel_path);
	return rc;
}

int
fs_copy_hdlr(struct cmd_args_s *ap)
{
	/* TODO: add check to make sure all required arguments are
	 * provided
	 */
	int			rc = 0;
	char			*src_str = NULL;
	char			*dst_str = NULL;
	size_t			src_str_len = 0;
	size_t			dst_str_len = 0;
	daos_cont_info_t	src_cont_info = {0};
	daos_cont_info_t	dst_cont_info = {0};
	struct file_dfs		src_file_dfs = {0};
	struct file_dfs		dst_file_dfs = {0};
	struct dm_args		ca = {0};
	char			*name = NULL;
	char			*dname = NULL;
	char			*dst_dir = NULL;
	mode_t			tmp_mode_dir = S_IRWXU;
	bool			is_posix_copy = true;

	set_dm_args_default(&ca);
	file_set_defaults_dfs(&src_file_dfs);
	file_set_defaults_dfs(&dst_file_dfs);

	src_str_len = strlen(ap->src);
	D_STRNDUP(src_str, ap->src, src_str_len);
	if (src_str == NULL) {
		fprintf(stderr, "Unable to allocate memory for source path.");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = dm_parse_path(&src_file_dfs, src_str, src_str_len,
			   &ca.src_p_uuid, &ca.src_c_uuid);
	if (rc != 0) {
		fprintf(stderr, "failed to parse source path: %d\n", rc);
		D_GOTO(out, rc = daos_errno2der(rc));
	}

	dst_str_len = strlen(ap->dst);
	D_STRNDUP(dst_str, ap->dst, dst_str_len);
	if (dst_str == NULL) {
		fprintf(stderr,
			"Unable to allocate memory for destination path.");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = dm_parse_path(&dst_file_dfs, dst_str, dst_str_len,
			   &ca.dst_p_uuid, &ca.dst_c_uuid);
	if (rc != 0) {
		fprintf(stderr, "failed to parse destination path: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = dm_connect(is_posix_copy, &src_file_dfs, &dst_file_dfs, &ca,
			ap->sysname, ap->dst, &src_cont_info, &dst_cont_info);
	if (rc != 0) {
		fprintf(stderr, "fs copy failed to connect: %d\n", rc);
		D_GOTO(out, rc = daos_errno2der(rc));
	}

	parse_filename_dfs(src_str, &name, &dname);

	/* construct destination directory in DAOS, this needs
	 * to strip the dirname and only use the basename that is
	 * specified in the dst argument
	 */
	src_str_len = strlen(dname);
	D_ASPRINTF(dst_dir, "%s/%s", dst_str, src_str + src_str_len);
	if (dst_dir == NULL) {
		fprintf(stderr,
			"Unable to allocate memory for destination path.\n");
		D_GOTO(out_disconnect, rc = -DER_NOMEM);
	}
	/* set paths based on file type for source and destination */
	if (src_file_dfs.type == POSIX && dst_file_dfs.type == DAOS) {
		rc = file_mkdir(&dst_file_dfs, dst_dir, &tmp_mode_dir);
		if (rc != EEXIST && rc != 0)
			D_GOTO(out_disconnect, rc);
		rc = fs_copy(&src_file_dfs, &dst_file_dfs,
			     src_str, src_str_len, dst_str);
		if (rc != 0)
			D_GOTO(out_disconnect, rc);
	} else if (src_file_dfs.type == DAOS && dst_file_dfs.type == POSIX) {
		rc = file_mkdir(&dst_file_dfs, dst_dir, &tmp_mode_dir);
		if (rc != EEXIST && rc != 0)
			D_GOTO(out_disconnect, rc);
		rc = fs_copy(&src_file_dfs, &dst_file_dfs,
			     src_str, src_str_len, dst_str);
		if (rc != 0)
			D_GOTO(out_disconnect, rc);
	} else if (src_file_dfs.type == DAOS && dst_file_dfs.type == DAOS) {
		rc = file_mkdir(&dst_file_dfs, dst_dir, &tmp_mode_dir);
		if (rc != EEXIST && rc != 0)
			D_GOTO(out_disconnect, rc);
		rc = fs_copy(&src_file_dfs, &dst_file_dfs,
			     src_str, src_str_len, dst_str);
		if (rc != 0)
			D_GOTO(out_disconnect, rc);
	/* TODO: handle POSIX->POSIX case here */
	} else {
		fprintf(stderr, "Regular POSIX to POSIX copies are not "
			"supported\n");
		D_GOTO(out_disconnect, rc = EINVAL);
	}

	if (dst_file_dfs.type == DAOS) {
		fprintf(stdout, "Successfully copied to DAOS: %s\n",
			dst_str);
	} else if (dst_file_dfs.type == POSIX) {
		fprintf(stdout, "Successfully copied to POSIX: %s\n",
			dst_str);
	}
out_disconnect:
	/* umount dfs, close conts, and disconnect pools */
	rc = dm_disconnect(is_posix_copy, &ca, &src_file_dfs, &dst_file_dfs);
	if (rc != 0)
		fprintf(stderr, "failed to disconnect (%d)\n", rc);
out:
	D_FREE(name);
	D_FREE(dname);
	D_FREE(src_str);
	D_FREE(dst_str);
	D_FREE(dst_dir);
	return rc;
}

static int
cont_clone_recx_single(daos_key_t *dkey,
		       daos_handle_t *src_oh,
		       daos_handle_t *dst_oh,
		       daos_iod_t *iod)
{
	/* if iod_type is single value just fetch iod size from source
	 * and update in destination object
	 */
	daos_size_t	buf_len = (*iod).iod_size;
	char		buf[buf_len];
	d_sg_list_t	sgl;
	d_iov_t		iov;
	int		rc;

	/* set sgl values */
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;
	d_iov_set(&iov, buf, buf_len);

	rc = daos_obj_fetch(*src_oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl,
			    NULL, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to fetch source value: "
			DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = daos_obj_update(*dst_oh, DAOS_TX_NONE, 0, dkey, 1, iod,
			     &sgl, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to update destination value: "
			DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	return rc;
}

static int
cont_clone_recx_array(daos_key_t *dkey,
		      daos_key_t *akey,
		      daos_handle_t *src_oh,
		      daos_handle_t *dst_oh,
		      daos_iod_t *iod)
{
	int			rc = 0;
	int			i = 0;
	daos_size_t		buf_len = 0;
	daos_size_t		buf_len_alloc = 0;
	uint32_t		number = 5;
	daos_anchor_t		recx_anchor = {0};
	d_sg_list_t		sgl;
	d_iov_t			iov;
	daos_epoch_range_t	eprs[5];
	daos_recx_t		recxs[5];
	daos_size_t		size;
	char			*buf = NULL;
	char			*prev_buf = NULL;

	while (!daos_anchor_is_eof(&recx_anchor)) {
		/* list all recx for this dkey/akey */
		number = 5;
		rc = daos_obj_list_recx(*src_oh, DAOS_TX_NONE, dkey,
					akey, &size, &number, recxs,
					eprs, &recx_anchor,
					true, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to list recx: "
				DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		/* if no recx is returned for this dkey/akey move on */
		if (number == 0)
			continue;

		/* set iod values */
		(*iod).iod_type  = DAOS_IOD_ARRAY;
		(*iod).iod_nr    = number;
		(*iod).iod_recxs = recxs;
		(*iod).iod_size  = size;

		/* set sgl values */
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		sgl.sg_nr     = 1;

		/* allocate/reallocate a single buffer */
		buf_len = 0;
		prev_buf = buf;
		for (i = 0; i < number; i++) {
			buf_len += recxs[i].rx_nr;
		}
		buf_len *= size;
		if (buf_len > buf_len_alloc) {
			D_REALLOC(buf, prev_buf, buf_len);
			if (buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			buf_len_alloc = buf_len;
		}
		d_iov_set(&iov, buf, buf_len);

		/* fetch recx values from source */
		rc = daos_obj_fetch(*src_oh, DAOS_TX_NONE, 0, dkey,
				    1, iod, &sgl, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to fetch source recx: "
				DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		/* Sanity check that fetch returns as expected */
		if (sgl.sg_nr_out != 1) {
			fprintf(stderr, "failed to fetch source recx\n");
			D_GOTO(out, rc = -DER_INVAL);
		}

		/* update fetched recx values and place in
		 * destination object
		 */
		rc = daos_obj_update(*dst_oh, DAOS_TX_NONE, 0, dkey,
				     1, iod, &sgl, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to update destination recx: "
				DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

out:
	D_FREE(buf);
	return rc;
}

static int
cont_clone_list_akeys(daos_handle_t *src_oh,
		      daos_handle_t *dst_oh,
		     daos_key_t diov)
{
	int		rc = 0;
	int		j = 0;
	char		*ptr;
	daos_anchor_t	akey_anchor = {0};
	daos_key_desc_t	akey_kds[ENUM_DESC_NR] = {0};
	uint32_t	akey_number = ENUM_DESC_NR;
	char		*akey = NULL;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	daos_key_t	aiov;
	daos_iod_t	iod;
	char		*small_key = NULL;
	char		*large_key = NULL;
	char		*key_buf = NULL;
	daos_size_t	key_buf_len = 0;

	D_ALLOC(small_key, ENUM_DESC_BUF);
	if (small_key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC(large_key, ENUM_LARGE_KEY_BUF);
	if (large_key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* loop to enumerate akeys */
	while (!daos_anchor_is_eof(&akey_anchor)) {
		memset(akey_kds, 0, sizeof(akey_kds));
		memset(small_key, 0, ENUM_DESC_BUF);
		memset(large_key, 0, ENUM_LARGE_KEY_BUF);
		akey_number = ENUM_DESC_NR;

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;

		key_buf = small_key;
		key_buf_len = ENUM_DESC_BUF;
		d_iov_set(&iov, key_buf, key_buf_len);

		/* get akeys */
		rc = daos_obj_list_akey(*src_oh, DAOS_TX_NONE, &diov,
					&akey_number, akey_kds,
					&sgl, &akey_anchor, NULL);
		if (rc == -DER_KEY2BIG) {
			/* call list dkey again with bigger buffer */
			key_buf = large_key;
			key_buf_len = ENUM_LARGE_KEY_BUF;
			d_iov_set(&iov, key_buf, key_buf_len);
			rc = daos_obj_list_akey(*src_oh, DAOS_TX_NONE, &diov,
						&akey_number, akey_kds,
						&sgl, &akey_anchor, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to list akeys: %d\n",
					rc);
				D_GOTO(out, rc);
			}
		}

		if (rc != 0) {
			fprintf(stderr, "failed to list akeys: %d\n", rc);
			D_GOTO(out, rc);
		}

		/* if no akeys returned move on */
		if (akey_number == 0)
			continue;

		/* parse out individual akeys based on key length and
		 * number of dkeys returned
		 */
		for (ptr = key_buf, j = 0; j < akey_number; j++) {
			D_ALLOC(akey, key_buf_len);
			if (akey == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			memcpy(akey, ptr, akey_kds[j].kd_key_len);
			d_iov_set(&aiov, (void *)akey, akey_kds[j].kd_key_len);

			/* set iod values */
			iod.iod_nr    = 1;
			iod.iod_type  = DAOS_IOD_SINGLE;
			iod.iod_size  = DAOS_REC_ANY;
			iod.iod_recxs = NULL;
			iod.iod_name  = aiov;

			/* do fetch with sgl == NULL to check if iod type
			 * (ARRAY OR SINGLE VAL)
			 */
			rc = daos_obj_fetch(*src_oh, DAOS_TX_NONE, 0, &diov,
					    1, &iod, NULL, NULL, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to fetch source "
					"object: %d\n", rc);
				D_FREE(akey);
				D_GOTO(out, rc);
			}

			/* if iod_size == 0 then this is a DAOS_IOD_ARRAY
			 * type
			 */
			if ((int)iod.iod_size == 0) {
				rc = cont_clone_recx_array(&diov, &aiov,
							   src_oh, dst_oh,
							   &iod);
				if (rc != 0) {
					fprintf(stderr, "failed to copy "
						"record: %d\n", rc);
					D_FREE(akey);
					D_GOTO(out, rc);
				}
			} else {
				rc = cont_clone_recx_single(&diov, src_oh,
							    dst_oh, &iod);
				if (rc != 0) {
					fprintf(stderr, "failed to copy "
						"record: %d\n", rc);
					D_FREE(akey);
					D_GOTO(out, rc);
				}
			}
			/* advance to next akey returned */
			ptr += akey_kds[j].kd_key_len;
			D_FREE(akey);
		}
	}
out:
	D_FREE(small_key);
	D_FREE(large_key);
	return rc;
}

static int
cont_clone_list_dkeys(daos_handle_t *src_oh,
		      daos_handle_t *dst_oh)
{
	int		rc = 0;
	int		j = 0;
	char		*ptr;
	daos_anchor_t	dkey_anchor = {0};
	daos_key_desc_t	dkey_kds[ENUM_DESC_NR] = {0};
	uint32_t	dkey_number = ENUM_DESC_NR;
	char		*dkey = NULL;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	daos_key_t	diov;
	char		*small_key = NULL;
	char		*large_key = NULL;
	char		*key_buf = NULL;
	daos_size_t	key_buf_len = 0;

	D_ALLOC(small_key, ENUM_DESC_BUF);
	if (small_key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC(large_key, ENUM_LARGE_KEY_BUF);
	if (large_key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* loop to enumerate dkeys */
	while (!daos_anchor_is_eof(&dkey_anchor)) {
		memset(dkey_kds, 0, sizeof(dkey_kds));
		memset(small_key, 0, ENUM_DESC_BUF);
		memset(large_key, 0, ENUM_LARGE_KEY_BUF);
		dkey_number = ENUM_DESC_NR;

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;

		key_buf = small_key;
		key_buf_len = ENUM_DESC_BUF;
		d_iov_set(&iov, key_buf, key_buf_len);

		/* get dkeys */
		rc = daos_obj_list_dkey(*src_oh, DAOS_TX_NONE,
					&dkey_number, dkey_kds,
					&sgl, &dkey_anchor, NULL);
		if (rc == -DER_KEY2BIG) {
			/* call list dkey again with bigger buffer */
			key_buf = large_key;
			key_buf_len = ENUM_LARGE_KEY_BUF;
			d_iov_set(&iov, key_buf, key_buf_len);
			rc = daos_obj_list_dkey(*src_oh, DAOS_TX_NONE,
						&dkey_number, dkey_kds,
						&sgl, &dkey_anchor, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to list dkeys: %d\n",
					rc);
				D_GOTO(out, rc);
			}
		}

		if (rc != 0) {
			fprintf(stderr, "failed to list dkeys: %d\n", rc);
			D_GOTO(out, rc);
		}

		/* if no dkeys were returned move on */
		if (dkey_number == 0)
			continue;

		/* parse out individual dkeys based on key length and
		 * number of dkeys returned
		 */
		for (ptr = key_buf, j = 0; j < dkey_number; j++) {
			D_ALLOC(dkey, key_buf_len);
			if (dkey == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			memcpy(dkey, ptr, dkey_kds[j].kd_key_len);
			d_iov_set(&diov, (void *)dkey, dkey_kds[j].kd_key_len);

			/* enumerate and parse akeys */
			rc = cont_clone_list_akeys(src_oh, dst_oh, diov);
			if (rc != 0) {
				fprintf(stderr, "failed to list akeys: %d\n",
					rc);
				D_FREE(dkey);
				D_GOTO(out, rc);
			}
			ptr += dkey_kds[j].kd_key_len;
			D_FREE(dkey);
		}
	}
out:
	D_FREE(small_key);
	D_FREE(large_key);
	return rc;
}

int
cont_clone_hdlr(struct cmd_args_s *ap)
{
	int			rc = 0;
	int			i = 0;
	daos_cont_info_t	src_cont_info;
	daos_cont_info_t	dst_cont_info;
	daos_obj_id_t		oids[OID_ARR_SIZE];
	daos_anchor_t		anchor;
	uint32_t		oids_nr;
	daos_handle_t		toh;
	daos_epoch_t		epoch;
	struct			dm_args ca = {0};
	bool			is_posix_copy = false;
	daos_handle_t		oh;
	daos_handle_t		dst_oh;
	struct file_dfs		src_cp_type = {0};
	struct file_dfs		dst_cp_type = {0};
	char			*src_str = NULL;
	char			*dst_str = NULL;
	size_t			src_str_len = 0;
	size_t			dst_str_len = 0;
	daos_epoch_range_t	epr;
	int			uuid_len = 128;

	set_dm_args_default(&ca);
	file_set_defaults_dfs(&src_cp_type);
	file_set_defaults_dfs(&dst_cp_type);

	src_str_len = strlen(ap->src);
	D_STRNDUP(src_str, ap->src, src_str_len);
	if (src_str == NULL) {
		fprintf(stderr, "Unable to allocate memory for source path.");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = dm_parse_path(&src_cp_type, src_str, src_str_len,
			   &ca.src_p_uuid, &ca.src_c_uuid);
	if (rc != 0) {
		fprintf(stderr, "failed to parse source path: %d\n", rc);
		D_GOTO(out, rc = daos_errno2der(rc));
	}

	dst_str_len = strlen(ap->dst);
	D_STRNDUP(dst_str, ap->dst, dst_str_len);
	if (dst_str == NULL) {
		fprintf(stderr, "Unable to allocate memory for dest path.");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = dm_parse_path(&dst_cp_type, dst_str, dst_str_len,
			   &ca.dst_p_uuid, &ca.dst_c_uuid);
	/* parse pool uuid if this fails, since it is not in form that
	 * can be parsed by duns_resolve_path (i.e. dst=/$pool)
	 */
	if (rc != 0) {
		uuid_generate(ca.dst_c_uuid);
		if (strncmp(ap->dst, "/", 1) == 0) {
			ap->dst += 1;
			D_STRNDUP(ca.dst, ap->dst, uuid_len);
			if (ca.dst == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			uuid_parse(ca.dst, ca.dst_p_uuid);
			ap->dst -= 1;
			/* not considered an error yet since this could
			 * be a UNS path, dm_connect will check this
			 * when it attempts to do a uns_path_create
			 */
			rc = 0;
		} else {
			fprintf(stderr, "failed to parse destination path: "
				"%d\n", rc);
			D_GOTO(out, rc);
		}
	} else {
		rc = daos_pool_connect(ca.dst_p_uuid, ap->sysname,
				       DAOS_PC_RW, &ca.dst_poh, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to connect to destination "
				"pool: %d\n", rc);
			D_GOTO(out, rc);
		}
		/* make sure this destination container doesn't exist already,
		 * if it does, exit
		 */
		rc = daos_cont_open(ca.dst_poh, ca.dst_c_uuid, DAOS_COO_RW,
				    &ca.dst_coh, &dst_cont_info, NULL);
		if (rc == 0) {
			fprintf(stderr, "This destination container already "
				"exists, please provide a destination "
				"container uuid that does not exist already, "
				"or provide an existing pool or new UNS path "
				"of the form:\n\t--dst </$pool> | "
				"<path/to/uns>\n");
			/* disconnect from only destination and exit */
			rc = daos_cont_close(ca.dst_coh, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to close destination "
					"container (%d)\n", rc);
				D_GOTO(out, rc);
			}
			rc = daos_pool_disconnect(ca.dst_poh, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to disconnect from "
					"destination pool "DF_UUIDF ": %s "
					"(%d)\n", DP_UUID(ca.dst_p_uuid),
					d_errdesc(rc), rc);
				D_GOTO(out, rc);
			}
			D_GOTO(out, rc = 1);
		}
	}

	rc = dm_connect(is_posix_copy, &dst_cp_type, &src_cp_type,
			&ca, ap->sysname, ap->dst, &src_cont_info,
			&dst_cont_info);
	if (rc != 0) {
		D_GOTO(out_disconnect, rc);
	}
	rc = daos_cont_create_snap_opt(ca.src_coh,
				       &epoch, NULL,
			       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT, NULL);
	if (rc) {
		fprintf(stderr, "failed to create snapshot: %d\n", rc);
		D_GOTO(out_disconnect, rc);
	}
	rc = daos_oit_open(ca.src_coh, epoch, &toh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to open object iterator\n");
		D_GOTO(out_snap, rc);
	}
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(toh, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to list objects\n");
			D_GOTO(out_oit, rc);
		}

		/* list object ID's */
		for (i = 0; i < oids_nr; i++) {
			rc = daos_obj_open(ca.src_coh, oids[i],
					   0, &oh, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to open source "
					"object\n");
				D_GOTO(out_oit, rc);
			}
			rc = daos_obj_open(ca.dst_coh, oids[i], 0,
					   &dst_oh, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to open destination "
					"object\n");
				D_GOTO(err_dst, rc);
			}
			rc = cont_clone_list_dkeys(&oh, &dst_oh);
			if (rc != 0) {
				fprintf(stderr, "failed to list keys\n");
				D_GOTO(err_obj, rc);
			}
			rc = daos_obj_close(oh, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to close source "
					"object: %d\n", rc);
				D_GOTO(out_oit, rc);
			}
			rc = daos_obj_close(dst_oh, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to close destination "
				"object: %d\n", rc);
				D_GOTO(err_dst, rc);
			}
		}
	}
	D_GOTO(out_oit, rc);
err_obj:
	rc = daos_obj_close(dst_oh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to close destination object: %d\n", rc);
	}
err_dst:
	rc = daos_obj_close(oh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to close source object: %d\n", rc);
	}
out_oit:
	rc = daos_oit_close(toh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to close object iterator: %d\n", rc);
		D_GOTO(out, rc);
	}
out_snap:
	epr.epr_lo = epoch;
	epr.epr_hi = epoch;
	rc = daos_cont_destroy_snap(ca.src_coh, epr, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to destroy snapshot: %d\n", rc);
	}
out_disconnect:
	/* close src and dst pools, conts */
	rc = dm_disconnect(is_posix_copy, &ca, &src_cp_type,
			   &dst_cp_type);
	if (rc != 0) {
		fprintf(stderr, "failed to disconnect: %d\n", rc);
	}
out:
	if (rc == 0) {
		fprintf(stdout, "\n\nSuccessfully copied to destination "
			"container "DF_UUIDF "\n\n",
			DP_UUID(ca.dst_c_uuid));
	}
	D_FREE(src_str);
	D_FREE(dst_str);
	D_FREE(ca.src);
	D_FREE(ca.dst);
	return rc;
}

static int
print_acl(FILE *outstream, daos_prop_t *acl_prop, bool verbose)
{
	int			rc = 0;
	struct daos_prop_entry	*entry = {0};
	struct daos_acl		*acl = NULL;

	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_ACL);
	if (entry != NULL)
		acl = entry->dpe_val_ptr;

	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_OWNER);
	if (entry != NULL && entry->dpe_str != NULL)
		fprintf(outstream, "# Owner: %s\n", entry->dpe_str);

	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_OWNER_GROUP);
	if (entry != NULL && entry->dpe_str != NULL)
		fprintf(outstream, "# Owner-Group: %s\n", entry->dpe_str);

	rc = daos_acl_to_stream(outstream, acl, verbose);
	if (rc != 0) {
		fprintf(stderr, "failed to print ACL: %s (%d)\n",
			d_errstr(rc), rc);
	}

	return rc;
}

int
cont_get_acl_hdlr(struct cmd_args_s *ap)
{
	int		rc;
	daos_prop_t	*prop = NULL;
	FILE		*outstream = stdout;

	if (ap->outfile) {
		int fd;
		int flags = O_CREAT | O_WRONLY;

		/* Ensure we don't overwrite some existing file without the
		 * force option.
		 */
		if (!ap->force) {
			flags |= O_EXCL;
		}

		fd = open(ap->outfile, flags, 0644);
		if (fd < 0) {
			fprintf(stderr,
				"Unable to create output file: %s\n",
				strerror(errno));
			return daos_errno2der(errno);
		}

		outstream = fdopen(fd, "w");
		if (outstream == NULL) {
			fprintf(stderr, "Unable to stream to output file: %s\n",
				strerror(errno));
			return daos_errno2der(errno);
		}
	}

	rc = daos_cont_get_acl(ap->cont, &prop, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to get ACL for container "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
	} else {
		rc = print_acl(outstream, prop, ap->verbose);
		if (rc == 0 && ap->outfile)
			fprintf(stdout, "Wrote ACL to output file: %s\n",
				ap->outfile);
	}

	if (ap->outfile)
		fclose(outstream);
	daos_prop_free(prop);
	return rc;
}

/*
 * Returns a substring of the line with leading and trailing whitespace trimmed.
 * Doesn't allocate any new memory - trimmed string is just a pointer.
 */
static char *
trim_acl_file_line(char *line)
{
	char *end;

	while (isspace(*line))
		line++;
	if (line[0] == '\0')
		return line;

	end = line + strnlen(line, DAOS_ACL_MAX_ACE_STR_LEN) - 1;
	while (isspace(*end))
		end--;
	end[1] = '\0';

	return line;
}

static int
parse_acl_file(const char *path, struct daos_acl **acl)
{
	int		rc = 0;
	FILE		*instream;
	char		*line = NULL;
	size_t		line_len = 0;
	char		*trimmed;
	struct daos_ace	*ace;
	struct daos_acl	*tmp_acl;

	instream = fopen(path, "r");
	if (instream == NULL) {
		fprintf(stderr, "Unable to read ACL input file '%s': %s\n",
			path, strerror(errno));
		return daos_errno2der(errno);
	}

	tmp_acl = daos_acl_create(NULL, 0);
	if (tmp_acl == NULL) {
		fprintf(stderr, "Unable to allocate memory for ACL\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	while (getline(&line, &line_len, instream) != -1) {
		trimmed = trim_acl_file_line(line);

		/* ignore blank lines and comments */
		if (trimmed[0] == '\0' || trimmed[0] == '#') {
			D_FREE(line);
			continue;
		}

		rc = daos_ace_from_str(trimmed, &ace);
		if (rc != 0) {
			fprintf(stderr,
				"Error parsing ACE '%s' from file: %s (%d)\n",
				trimmed, d_errdesc(rc), rc);
			D_GOTO(parse_err, rc);
		}

		rc = daos_acl_add_ace(&tmp_acl, ace);
		daos_ace_free(ace);
		if (rc != 0) {
			fprintf(stderr, "Error parsing ACL file: %s (%d)\n",
				d_errdesc(rc), rc);
			D_GOTO(parse_err, rc);
		}

		D_FREE(line);
	}

	if (daos_acl_validate(tmp_acl) != 0) {
		fprintf(stderr, "Content of ACL file is invalid\n");
		D_GOTO(parse_err, rc = -DER_INVAL);
	}

	*acl = tmp_acl;
	D_GOTO(out, rc = 0);

parse_err:
	D_FREE(line);
	daos_acl_free(tmp_acl);
out:
	fclose(instream);
	return rc;
}

int
cont_overwrite_acl_hdlr(struct cmd_args_s *ap)
{
	int		rc;
	struct daos_acl	*acl = NULL;
	daos_prop_t	*prop_out;

	if (!ap->aclfile) {
		fprintf(stderr,
			"Parameter --acl-file is required\n");
		return -DER_INVAL;
	}

	rc = parse_acl_file(ap->aclfile, &acl);
	if (rc != 0)
		return rc;

	rc = daos_cont_overwrite_acl(ap->cont, acl, NULL);
	daos_acl_free(acl);
	if (rc != 0) {
		fprintf(stderr, "failed to overwrite ACL for container "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(stderr,
			"overwrite appeared to succeed, but failed to fetch ACL"
			" for confirmation: %s (%d)\n", d_errdesc(rc), rc);
		return rc;
	}

	rc = print_acl(stdout, prop_out, false);

	daos_prop_free(prop_out);
	return rc;
}

int
cont_update_acl_hdlr(struct cmd_args_s *ap)
{
	int		rc;
	struct daos_acl	*acl = NULL;
	struct daos_ace	*ace = NULL;
	daos_prop_t	*prop_out;

	/* need one or the other, not both */
	if (!ap->aclfile == !ap->entry) {
		fprintf(stderr,
			"either parameter --acl-file or --entry is required\n");
		return -DER_INVAL;
	}

	if (ap->aclfile) {
		rc = parse_acl_file(ap->aclfile, &acl);
		if (rc != 0)
			return rc;
	} else {
		rc = daos_ace_from_str(ap->entry, &ace);
		if (rc != 0) {
			fprintf(stderr, "failed to parse entry: %s (%d)\n",
				d_errdesc(rc), rc);
			return rc;
		}

		acl = daos_acl_create(&ace, 1);
		daos_ace_free(ace);
		if (acl == NULL) {
			rc = -DER_NOMEM;
			fprintf(stderr, "failed to make ACL from entry: %s "
				"(%d)\n", d_errdesc(rc), rc);
			return rc;
		}
	}

	rc = daos_cont_update_acl(ap->cont, acl, NULL);
	daos_acl_free(acl);
	if (rc != 0) {
		fprintf(stderr, "failed to update ACL for container "
			DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(stderr,
			"update appeared to succeed, but failed to fetch ACL "
			"for confirmation: %s (%d)\n", d_errdesc(rc), rc);
		return rc;
	}

	rc = print_acl(stdout, prop_out, false);

	daos_prop_free(prop_out);
	return rc;
}

int
cont_delete_acl_hdlr(struct cmd_args_s *ap)
{
	int				rc;
	enum daos_acl_principal_type	type;
	char				*name;
	daos_prop_t			*prop_out;

	if (!ap->principal) {
		fprintf(stderr,
			"parameter --principal is required\n");
		return -DER_INVAL;
	}

	rc = daos_acl_principal_from_str(ap->principal, &type, &name);
	if (rc != 0) {
		fprintf(stderr, "unable to parse principal string '%s': %s"
			"(%d)\n", ap->principal, d_errdesc(rc), rc);
		return rc;
	}

	rc = daos_cont_delete_acl(ap->cont, type, name, NULL);
	D_FREE(name);
	if (rc != 0) {
		fprintf(stderr, "failed to delete ACL for container "
			DF_UUIDF": %s (%d)\n", DP_UUID(ap->c_uuid),
			d_errdesc(rc), rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(stderr,
			"delete appeared to succeed, but failed to fetch ACL "
			"for confirmation: %s (%d)\n", d_errdesc(rc), rc);
		return rc;
	}

	rc = print_acl(stdout, prop_out, false);

	daos_prop_free(prop_out);
	return rc;
}

int
cont_set_owner_hdlr(struct cmd_args_s *ap)
{
	int	rc;

	if (!ap->user && !ap->group) {
		fprintf(stderr,
			"parameter --user or --group is required\n");
		return -DER_INVAL;
	}

	rc = daos_cont_set_owner(ap->cont, ap->user, ap->group, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to set owner for container "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		return rc;
	}

	fprintf(stdout, "successfully updated owner for container\n");
	return rc;
}

int
cont_rollback_hdlr(struct cmd_args_s *ap)
{
	int	rc;

	if (ap->epc == 0 && ap->snapname_str == NULL) {
		fprintf(stderr,
			"either parameter --epc or --snap is required\n");
		return -DER_INVAL;
	}
	if (ap->epc != 0 && ap->snapname_str != NULL) {
		fprintf(stderr,
			"both parameters --epc and --snap could not be specified\n");
		return -DER_INVAL;
	}

	if (ap->snapname_str != NULL) {
		rc = cont_list_snaps_hdlr(ap, ap->snapname_str, &ap->epc);
		if (rc != 0)
			return rc;
	}
	rc = daos_cont_rollback(ap->cont, ap->epc, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to roll back container "DF_UUIDF
			" to snapshot "DF_U64": %s (%d)\n", DP_UUID(ap->c_uuid),
			ap->epc, d_errdesc(rc), rc);
		return rc;
	}

	fprintf(stdout, "successfully rollback container\n");
	return rc;
}

int
cont_list_objs_hdlr(struct cmd_args_s *ap)
{
	daos_obj_id_t		oids[OID_ARR_SIZE];
	daos_handle_t		oit;
	daos_anchor_t		anchor = {0};
	uint32_t		oids_nr;
	int			rc, i;

	/* create a snapshot with OIT */
	rc = daos_cont_create_snap_opt(ap->cont, &ap->epc, NULL,
				       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	if (rc != 0)
		goto out;

	/* open OIT */
	rc = daos_oit_open(ap->cont, ap->epc, &oit, NULL);
	if (rc != 0) {
		fprintf(stderr, "open of container's OIT failed: "DF_RC"\n",
			DP_RC(rc));
		goto out_snap;
	}

	while (!daos_anchor_is_eof(&anchor)) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(oit, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			fprintf(stderr,
				"object IDs enumeration failed: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out_close, rc);
		}

		for (i = 0; i < oids_nr; i++)
			D_PRINT(DF_OID"\n", DP_OID(oids[i]));
	}

out_close:
	daos_oit_close(oit, NULL);
out_snap:
	cont_destroy_snap_hdlr(ap);
out:
	return rc;
}

int
obj_query_hdlr(struct cmd_args_s *ap)
{
	struct daos_obj_layout *layout;
	int			i;
	int			j;
	int			rc;

	rc = daos_obj_layout_get(ap->cont, ap->oid, &layout);
	if (rc) {
		fprintf(stderr, "failed to retrieve layout for object "DF_OID
			": %s (%d)\n", DP_OID(ap->oid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* Print the object layout */
	fprintf(stdout, "oid: "DF_OID" ver %d grp_nr: %d\n", DP_OID(ap->oid),
		layout->ol_ver, layout->ol_nr);

	for (i = 0; i < layout->ol_nr; i++) {
		struct daos_obj_shard *shard;

		shard = layout->ol_shards[i];
		fprintf(stdout, "grp: %d\n", i);
		for (j = 0; j < shard->os_replica_nr; j++)
			fprintf(stdout, "replica %d %d\n", j,
				shard->os_shard_loc[j].sd_rank);
	}

	daos_obj_layout_free(layout);

out:
	return rc;
}
