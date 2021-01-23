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
#include <hdf5.h>

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

struct fs_copy_args {
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
};

/* for oid dataset */
typedef struct {
	uint64_t oid_hi;
	uint64_t oid_low;
	uint64_t dkey_offset;
} oid_t;

/* for dkey dataset */
typedef struct {
	/* array of vlen structure */
	hvl_t dkey_val;
	uint64_t akey_offset;
} dkey_t;

/* for akey dataset */
typedef struct {
	/* array of vlen structure */
	hvl_t akey_val;
	uint64_t rec_dset_id;
} akey_t;

struct hdf5_args {
	hid_t status;
	hid_t file;
	/* OID Data */
	hid_t oid_memtype;
	hid_t oid_dspace;
	hid_t oid_dset;
	hid_t oid_dtype;
	/* DKEY Data */
	hid_t dkey_memtype;
	hid_t dkey_vtype;
	hid_t dkey_dspace;
	hid_t dkey_dset;
	/* AKEY Data */
	hid_t akey_memtype;
	hid_t akey_vtype;
	hid_t akey_dspace;
	hid_t akey_dset;
	/* recx Data */
	hid_t plist;
	hid_t rx_dspace;
	hid_t rx_memspace;
	hid_t attr_dspace;
	hid_t attr_dtype;
	hid_t rx_dset;
	hid_t single_dspace;
	hid_t single_dset;
	hid_t rx_dtype;
	hid_t usr_attr_num;
	hid_t usr_attr;
	hid_t selection_attr;
	hid_t version_attr;
	hid_t single_dtype;
	hid_t version_attr_dspace;
	hid_t version_attr_type;
	/* dims for dsets */
	hsize_t oid_dims[1];
	hsize_t dkey_dims[1];     
	hsize_t akey_dims[1];     
	hsize_t rx_dims[1];
	hsize_t	mem_dims[1];
	hsize_t	attr_dims[1];
	hsize_t rx_chunk_dims[1];
	hsize_t rx_max_dims[1];
	hsize_t single_dims[1];
	hsize_t version_attr_dims[1];
	/* data for keys */
	oid_t *oid_data;
	dkey_t *dkey_data;
	akey_t *akey_data;
	dkey_t **dk;
	akey_t **ak;
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
		if (entry->dpe_type != DAOS_PROP_CO_LABEL) {
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
	int	path_len = strlen(path) + 1;
	int	rc = 0;

	if (path == NULL || _obj_name == NULL || _cont_name == NULL)
		return -EINVAL;

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

			D_ALLOC(buf, buf_size * sizeof(char));
			if (buf == NULL)
				return ENOMEM;
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
	if (S_ISDIR(st_dir_name.st_mode)) {
		rc = file_closedir(src_file_dfs, src_dir);
		if (rc != 0) {
			fprintf(stderr, "Could not close '%s': %d\n",
				dir_name, rc);
		}
	}
	D_FREE(filename);
	D_FREE(dst_filename);
	return rc;
}

static int
fs_copy_connect(struct file_dfs *src_file_dfs,
		struct file_dfs *dst_file_dfs,
		struct fs_copy_args *fa,
		char *sysname,
		daos_cont_info_t *src_cont_info,
		daos_cont_info_t *dst_cont_info,
		struct duns_attr_t *src_dattr,
		struct duns_attr_t *dst_dattr)
{
	/* check source pool/conts */
	int rc = 0;

	/* open src pool, src cont, and mount dfs */
	if (src_file_dfs->type == DAOS) {
		rc = daos_pool_connect(fa->src_p_uuid, sysname,
				       DAOS_PC_RW, &fa->src_poh, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to connect to destination "
				"pool: %d\n", rc);
			D_GOTO(out, rc);
		}
		rc = daos_cont_open(fa->src_poh, fa->src_c_uuid, DAOS_COO_RW,
				    &fa->src_coh, src_cont_info, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to open source "
				"container: %d\n", rc);
			D_GOTO(err_src_root, rc);
		}
		rc = dfs_mount(fa->src_poh, fa->src_coh, O_RDWR,
			       &src_file_dfs->dfs);
		if (rc) {
			fprintf(stderr, "dfs mount on source failed: %d\n", rc);
			D_GOTO(err_src_dfs, rc);
		}
	}

	/* open dst pool, dst cont, and mount dfs */
	if (dst_file_dfs->type == DAOS) {
		rc = daos_pool_connect(fa->dst_p_uuid, sysname,
				       DAOS_PC_RW, &fa->dst_poh, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to connect to destination "
				"pool: %d\n", rc);
			D_GOTO(out, rc);
		}
		rc = daos_cont_open(fa->dst_poh, fa->dst_c_uuid, DAOS_COO_RW,
				    &fa->dst_coh, dst_cont_info, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to open destination "
				"container: %d\n", rc);
			D_GOTO(err_dst_root, rc);
		}
		rc = dfs_mount(fa->dst_poh, fa->dst_coh, O_RDWR,
			       &dst_file_dfs->dfs);
		if (rc != 0) {
			fprintf(stderr, "dfs mount on destination "
			"failed: %d\n", rc);
			D_GOTO(err_dst_dfs, rc);
		}
	}
	return rc;

err_dst_dfs:
	rc = daos_cont_close(fa->dst_coh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to close destination "
			"container (%d)\n", rc);
	}
err_dst_root:
	rc = daos_pool_disconnect(fa->dst_poh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to disconnect from destintaion "
			"pool "DF_UUIDF ": %s (%d)\n", DP_UUID(fa->src_p_uuid),
			d_errdesc(rc), rc);
	}
err_src_dfs:
	rc = daos_cont_close(fa->src_coh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to close source container (%d)\n", rc);
	}
err_src_root:
	rc = daos_pool_disconnect(fa->src_poh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to disconnect from source pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(fa->src_p_uuid),
			d_errdesc(rc), rc);
	}
out:
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

inline void
init_hdf5_args(struct hdf5_args *hdf5)
{
	hdf5->status = 0;
	hdf5->file = -1;
	/* OID Data */
	hdf5->oid_memtype = 0;
	hdf5->oid_dspace = 0;
	hdf5->oid_dset = 0;
	/* DKEY Data */
	hdf5->dkey_memtype = 0;
	hdf5->dkey_vtype = 0;
	hdf5->dkey_dspace = 0;
	hdf5->dkey_dset = 0;
	/* AKEY Data */
	hdf5->akey_memtype = 0;
	hdf5->akey_vtype = 0;
	hdf5->akey_dspace = 0;
	hdf5->akey_dset = 0;
	/* dims for dsets */
	hdf5->oid_dims[0] = 0;
	hdf5->dkey_dims[0] = 0;     
	hdf5->akey_dims[0] = 0;     
	/* data for keys */
	hdf5->oid_data = NULL;
	hdf5->dkey_data = NULL;
	hdf5->akey_data = NULL;
	hdf5->dk = NULL;
	hdf5->ak = NULL;
}

static int
fs_copy_disconnect(struct fs_copy_args *fa,
		   struct file_dfs *src_file_dfs,
		struct file_dfs *dst_file_dfs)
{
	int rc = 0;

	if (src_file_dfs->type == DAOS) {
		rc = dfs_umount(src_file_dfs->dfs);
		if (rc != 0) {
			fprintf(stderr, "failed to unmount source (%d)\n", rc);
			D_GOTO(err_src, rc);
		}
		rc = daos_cont_close(fa->src_coh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to close source "
				"container (%d)\n", rc);
			D_GOTO(err_src, rc);
		}
		rc = daos_pool_disconnect(fa->src_poh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to disconnect from source "
				"pool "DF_UUIDF ": %s (%d)\n",
				DP_UUID(fa->src_p_uuid), d_errdesc(rc), rc);
			D_GOTO(err_src, rc);
		}
	}
err_src:
	if (dst_file_dfs->type == DAOS) {
		rc = dfs_umount(dst_file_dfs->dfs);
		if (rc != 0) {
			fprintf(stderr, "failed to unmount destination "
				"(%d)\n", rc);
			D_GOTO(out, rc);
		}
		rc = daos_cont_close(fa->dst_coh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to close destination "
				"container (%d)\n", rc);
			D_GOTO(out, rc);
		}
		rc = daos_pool_disconnect(fa->dst_poh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to disconnect from destination "
				"pool "DF_UUIDF ": %s (%d)\n",
				DP_UUID(fa->dst_p_uuid), d_errdesc(rc), rc);
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
fs_copy_parse_path(struct file_dfs *file, char *path,
		   uuid_t *p_uuid, uuid_t *c_uuid)
{
	struct duns_attr_t	dattr = {0};
	int			rc = 0;

	rc = duns_resolve_path(path, &dattr);
	if (rc == 0) {
		uuid_copy(*p_uuid, dattr.da_puuid);
		uuid_copy(*c_uuid, dattr.da_cuuid);
		if (dattr.da_rel_path == NULL) {
			strcpy(path, "/");
		} else {
			strcpy(path, dattr.da_rel_path);
		}
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
	char			src_str[1028];
	char			dst_str[1028];
	daos_cont_info_t	src_cont_info = {0};
	daos_cont_info_t	dst_cont_info = {0};
	struct duns_attr_t	src_dattr = {0};
	struct duns_attr_t	dst_dattr = {0};
	struct file_dfs		src_file_dfs = {0};
	struct file_dfs		dst_file_dfs = {0};
	struct fs_copy_args	fa = {0};
	int			src_str_len = 0;
	char			*name = NULL;
	char			*dname = NULL;
	char			dst_dir[MAX_FILENAME];
	int			path_length = 0;
	mode_t			tmp_mode_dir = S_IRWXU;

	file_set_defaults_dfs(&src_file_dfs);
	file_set_defaults_dfs(&dst_file_dfs);
	strcpy(src_str, ap->src);
	rc = fs_copy_parse_path(&src_file_dfs, src_str, &fa.src_p_uuid,
				&fa.src_c_uuid);
	if (rc != 0) {
		fprintf(stderr, "failed to parse source path: %d\n", rc);
		D_GOTO(out, rc);
	}
	strcpy(dst_str, ap->dst);
	rc = fs_copy_parse_path(&dst_file_dfs, dst_str, &fa.dst_p_uuid,
				&fa.dst_c_uuid);
	if (rc != 0) {
		fprintf(stderr, "failed to parse destination path: %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = fs_copy_connect(&src_file_dfs, &dst_file_dfs, &fa,
			     ap->sysname, &src_cont_info, &dst_cont_info,
			     &src_dattr, &dst_dattr);
	if (rc != 0) {
		fprintf(stderr, "fs copy failed to connect: %d\n", rc);
		D_GOTO(out, rc);
	}

	parse_filename_dfs(src_str, &name, &dname);

	/* construct destination directory in DAOS, this needs
	 * to strip the dirname and only use the basename that is
	 * specified in the dst argument
	 */
	src_str_len = strlen(dname);
	path_length = snprintf(dst_dir, MAX_FILENAME, "%s/%s",
			       dst_str, src_str + src_str_len);
	if (path_length >= MAX_FILENAME) {
		rc = ENAMETOOLONG;
		fprintf(stderr, "Path length is too long.\n");
		D_GOTO(out_disconnect, rc);
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
	rc = fs_copy_disconnect(&fa, &src_file_dfs, &dst_file_dfs);
	if (rc != 0)
		fprintf(stderr, "failed to disconnect (%d)\n", rc);
out:
	return rc;
}

static int
serialize_recx_single(struct hdf5_args *hdf5, 
		      daos_key_t *dkey,
		      daos_handle_t *oh,
		      daos_iod_t *iod)
{
	/* if iod_type is single value just fetch iod size from source
	 * and update in destination object */
	int         buf_len = (int)(*iod).iod_size;
	char        buf[buf_len];
	d_sg_list_t sgl;
	d_iov_t     iov;
	int	    rc;

	/* set sgl values */
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;
	d_iov_set(&iov, buf, buf_len);
        rc = daos_obj_fetch(*oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl,
			    NULL, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to fetch object: %d\n", rc);
		D_GOTO(out, rc);
	}
	/* write single val record to dataset */
	H5Dwrite(hdf5->single_dset, hdf5->single_dtype, H5S_ALL,
		 hdf5->single_dspace, H5P_DEFAULT, sgl.sg_iovs[0].iov_buf);
	printf("\tSINGLE DSET ID WRITTEN: %d\n", (int)hdf5->single_dset);
out:
	return rc;
}


static int
serialize_recx_array(struct hdf5_args *hdf5,
		     daos_key_t *dkey,
		     daos_key_t *akey,
     		     uint64_t *ak_index,
		     daos_handle_t *oh,
		     daos_iod_t *iod)
{
	int			rc = 0;
	int			i = 0;
	int			attr_num = 0;
	int			buf_len = 0;
	int			path_len = 0;
	int			encode_buf_len;
	uint32_t		number = 5;
	size_t			nalloc;
	daos_anchor_t		recx_anchor = {0}; 
	daos_epoch_range_t	eprs[5];
	daos_recx_t		recxs[5];
	daos_size_t		size = 0;
	char			attr_name[64];
	char			number_str[16];
	char			attr_num_str[16];
	unsigned char		*encode_buf = NULL;
	d_sg_list_t		sgl;
	d_iov_t			iov;
	hid_t			status = 0;

	while (!daos_anchor_is_eof(&recx_anchor)) {
		memset(recxs, 0, sizeof(recxs));
		memset(eprs, 0, sizeof(eprs));

		/* list all recx for this dkey/akey */
	        number = 5;
		rc = daos_obj_list_recx(*oh, DAOS_TX_NONE, dkey,
			akey, &size, &number, recxs, eprs, &recx_anchor,
			true, NULL);
		printf("RECX RETURNED: %d\n", (int)number);
		printf("RECX SIZE: %d\n", (int)size);

		/* if no recx is returned for this dkey/akey move on */
		if (number == 0) 
			continue;
		printf("\n\nNUM RECX RET: %d\n\n", (int)number);
		for (i = 0; i < number; i++) {
			buf_len = recxs[i].rx_nr;
		        char        buf[buf_len];

			memset(&sgl, 0, sizeof(sgl));
			memset(&iov, 0, sizeof(iov));

			/* set iod values */
			(*iod).iod_type  = DAOS_IOD_ARRAY;
			(*iod).iod_size  = 1;
			(*iod).iod_nr    = 1;
			(*iod).iod_recxs = &recxs[i];

			/* set sgl values */
			sgl.sg_nr     = 1;
			sgl.sg_nr_out = 0;
			sgl.sg_iovs   = &iov;

			d_iov_set(&iov, buf, buf_len);	
			printf("\ti: %d iod_size: %d rx_nr:%d, rx_idx:%d\n",
				i, (int)size, (int)recxs[i].rx_nr,
				(int)recxs[i].rx_idx);
			/* fetch recx values from source */
                        rc = daos_obj_fetch(*oh, DAOS_TX_NONE, 0, dkey, 1, iod,
				&sgl, NULL, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to fetch object: %d\n",
					rc);
			}
			/* write data to record dset */
			printf("\n\nTOTAL DIMS SO FAR: %d\n\n",
			       (int)hdf5->rx_dims[0]);
			hdf5->mem_dims[0] = recxs[i].rx_nr;
			hdf5->rx_memspace = H5Screate_simple(1, hdf5->mem_dims,
							     hdf5->mem_dims);
			if (hdf5->rx_memspace < 0) {
				fprintf(stderr, "failed to create rx_memspace "
				"\n");
				D_GOTO(out, rc = 1);
			}
			/* extend dataset */
			hdf5->rx_dims[0] += recxs[i].rx_nr;
			status = H5Dset_extent(hdf5->rx_dset, hdf5->rx_dims);
			if (status < 0) {
				fprintf(stderr, "failed to extend  rx dset\n");
				D_GOTO(out, rc = 1);
			}
			printf("RX DIMS: %d\n", (int)hdf5->rx_dims[0]);
			/* retrieve extended dataspace */
			hdf5->rx_dspace = H5Dget_space(hdf5->rx_dset);
			if (hdf5->rx_dspace < 0) {
				fprintf(stderr, "failed to get rx_dspace\n");
				D_GOTO(out, rc = 1);
			}
			/* TODO: remove debugging printf's and calls */
			hsize_t dset_size = H5Sget_simple_extent_npoints(hdf5->rx_dspace);
			printf("DSET_SIZE: %d\n", (int)dset_size);
			hsize_t start = (hsize_t)recxs[i].rx_idx;
			hsize_t count = (hsize_t)recxs[i].rx_nr;

			status = H5Sselect_hyperslab(hdf5->rx_dspace,
						     H5S_SELECT_AND, &start,
						     NULL, &count, NULL);
			if (status < 0) {
				fprintf(stderr, "failed to select hyperslab\n");
				D_GOTO(out, rc = 1);
			}

			/* TODO: remove random checking/printing to make sure
			 * right number of blocks is selected
			 */
			hsize_t sel = H5Sget_select_npoints(hdf5->rx_dspace);
			printf("SEL: %d\n", (int)sel);
			hsize_t mem_sel = H5Sget_select_npoints(hdf5->rx_memspace);
			printf("MEM SEL: %d\n", (int)mem_sel);
			hssize_t nblocks = H5Sget_select_hyper_nblocks(hdf5->rx_dspace);
			printf("NUM BLOCKS SELECTED: %d\n", (int)nblocks);
			htri_t valid = H5Sselect_valid(hdf5->rx_dspace);
			printf("VALID: %d\n", (int)valid);

			hdf5->rx_dtype = H5Tcreate(H5T_OPAQUE, (*iod).iod_size);
			if (hdf5->rx_dtype < 0) {
				fprintf(stderr, "failed to create rx_dtype\n");
				D_GOTO(out, rc = 1);
			}
			/* HDF5 should not try to interpret the datatype */
			status = H5Tset_tag(hdf5->rx_dtype, "Opaque dtype");
			if (status < 0) {
				fprintf(stderr, "failed to set dtype tag\n");
				D_GOTO(out, rc = 1);
			}
			status = H5Dwrite(hdf5->rx_dset, hdf5->rx_dtype,
					  hdf5->rx_memspace, hdf5->rx_dspace,
					  H5P_DEFAULT, sgl.sg_iovs[0].iov_buf);
			if (status < 0) {
				fprintf(stderr, "failed to write to rx_dset\n");
				D_GOTO(out, rc = 1);
			}
			printf("\tRECX DSET ID WRITTEN: %d\n",
				(int)hdf5->rx_dset);
			printf("\tRECX DSPACE ID WRITTEN: %d\n",
				(int)hdf5->rx_dspace);
			/* get size of buffer needed
			 * from nalloc
			 */
			status = H5Sencode(hdf5->rx_dspace, NULL, &nalloc);
			if (status < 0) {
				fprintf(stderr, "failed to get size of buffer "
					"needed\n");
				D_GOTO(out, rc = 1);
			}
			/* encode dataspace description
			 * in buffer then store in
			 * attribute on dataset
			 */
			encode_buf = malloc(nalloc * sizeof(unsigned char));
			status = H5Sencode(hdf5->rx_dspace, encode_buf,
					   &nalloc);
			if (status < 0) {
				fprintf(stderr, "failed to encode dataspace "
					"buffer\n");
				D_GOTO(out, rc = 1);
			}
			/* created attribute in HDF5 file with encoded
			 * dataspace for this record extent */
			memset(attr_name, 64, sizeof(attr_name));
			memset(number_str, 16, sizeof(number_str));
			memset(attr_num_str, 16, sizeof(attr_num_str));
			path_len = snprintf(number_str, 10, "%d",
					    (int)(*ak_index));
			if (path_len >= 16) {
				fprintf(stderr, "number_str too long\n");
				D_GOTO(out, rc = 1);
			}
			path_len = snprintf(attr_num_str, 10, "-%d", attr_num);
			if (path_len >= 16) {
				fprintf(stderr, "attr number str too long\n");
				D_GOTO(out, rc = 1);
			}
			path_len = snprintf(attr_name, 64, "%s", "A-");
			if (path_len >= 64) {
				fprintf(stderr, "attr name too long\n");
				D_GOTO(out, rc = 1);
			}
			strcat(attr_name, number_str);
			strcat(attr_name, attr_num_str);
			printf("\n\nATTR NAME: %s\n\n", attr_name);
			encode_buf_len = nalloc * sizeof(unsigned char);
			hdf5->attr_dims[0] = encode_buf_len;
			hdf5->attr_dspace = H5Screate_simple(1, hdf5->attr_dims,
						             NULL);
			if (hdf5->attr_dspace < 0) {
				fprintf(stderr, "failed to create attribute "
					"dataspace\n");
				D_GOTO(out, rc = 1);
			}
			hdf5->selection_attr = H5Acreate2(hdf5->rx_dset,
				        	    	  attr_name,
						          hdf5->rx_dtype,
						          hdf5->attr_dspace,
						          H5P_DEFAULT,
						          H5P_DEFAULT);
			if (hdf5->selection_attr < 0) {
				fprintf(stderr, "failed to create selection "
					"attribute\n");
				D_GOTO(out, rc = 1);
			}	
			status = H5Awrite(hdf5->selection_attr, hdf5->rx_dtype,
					  encode_buf);
			if (status < 0) {
				fprintf(stderr, "failed to write attribute\n");
				D_GOTO(out, rc = 1);
			}
			status = H5Aclose(hdf5->selection_attr);
			if (status < 0) {
				fprintf(stderr, "failed to close attribute\n");
				D_GOTO(out, rc = 1);
			}
			status = H5Sclose(hdf5->rx_memspace);
			if (status < 0) {
				fprintf(stderr, "failed to close rx_memspace\n");
				D_GOTO(out, rc = 1);
			}
			if (encode_buf != NULL) 
				free(encode_buf);
			attr_num++;
		}
	}
out:
	return rc;
}

static int
init_recx_data(struct hdf5_args *hdf5)
{
	int	rc = 0;
	herr_t	err = 0;

	hdf5->single_dims[0] = 1;
	hdf5->rx_dims[0] = 0;
	hdf5->rx_max_dims[0] = H5S_UNLIMITED;
	hdf5->rx_chunk_dims[0] = 1;

	hdf5->plist = H5Pcreate(H5P_DATASET_CREATE);
	if (hdf5->plist < 0) {
		fprintf(stderr, "failed to create prop list\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->rx_dspace = H5Screate_simple(1, hdf5->rx_dims, hdf5->rx_max_dims);
	if (hdf5->rx_dspace < 0) {
		fprintf(stderr, "failed to create rx_dspace\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->single_dspace = H5Screate_simple(1, hdf5->single_dims, NULL);
	if (hdf5->single_dspace < 0) {
		fprintf(stderr, "failed to create single_dspace\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->rx_dtype = H5Tcreate(H5T_OPAQUE, 1);
	if (hdf5->rx_dtype < 0) {
		fprintf(stderr, "failed to create rx_dtype\n");
		D_GOTO(out, rc = 1);
	}
	err = H5Pset_layout(hdf5->plist, H5D_CHUNKED);
	if (err < 0) {
		fprintf(stderr, "failed to set property layout\n");
		D_GOTO(out, rc = 1);
	}
	err = H5Pset_chunk(hdf5->plist, 1, hdf5->rx_chunk_dims);
	if (err < 0) {
		fprintf(stderr, "failed to set chunk size\n");
		D_GOTO(out, rc = 1);
	}
	err = H5Tset_tag(hdf5->rx_dtype, "Opaque dtype");
	if (err < 0) {
		fprintf(stderr, "failed to set recx tag\n");
		D_GOTO(out, rc = 1);
	}
out:
	return rc;
}

static int
serialize_akeys(struct hdf5_args *hdf5,
		daos_key_t diov,
		uint64_t *dk_index,
		uint64_t *ak_index,
		uint64_t *total_akeys,
		daos_handle_t *oh)
{
	int		rc = 0;
	herr_t		err = 0;
	int		j = 0;
	daos_anchor_t	akey_anchor = {0}; 
	d_sg_list_t     akey_sgl;
	d_iov_t         akey_iov;
	daos_key_desc_t akey_kds[ENUM_DESC_NR] = {0};
	uint32_t        akey_number = ENUM_DESC_NR;
	char            akey_enum_buf[ENUM_DESC_BUF] = {0};
	char 		akey[ENUM_KEY_BUF] = {0};
	char		*akey_ptr = NULL;
	daos_key_t	aiov;
	daos_iod_t	iod;
	char		rec_name[32];
	int		path_len = 0;
	int		size = 0;
	hvl_t		*akey_val;

	while (!daos_anchor_is_eof(&akey_anchor)) {
 		memset(akey_kds, 0, sizeof(akey_kds));
 		memset(akey, 0, sizeof(akey));
 		memset(akey_enum_buf, 0, sizeof(akey_enum_buf));
		akey_number = ENUM_DESC_NR;

		akey_sgl.sg_nr     = 1;
		akey_sgl.sg_nr_out = 0;
		akey_sgl.sg_iovs   = &akey_iov;

		d_iov_set(&akey_iov, akey_enum_buf, ENUM_DESC_BUF);

		/* get akeys */
		rc = daos_obj_list_akey(*oh, DAOS_TX_NONE, &diov,
					&akey_number, akey_kds,
					&akey_sgl, &akey_anchor, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to list akeys: %d\n", rc);
			D_GOTO(out, rc);
		}

		/* if no akeys returned move on */
		if (akey_number == 0)
			continue;

		size = (akey_number + *total_akeys) * sizeof(akey_t);
		*hdf5->ak = realloc(*hdf5->ak, size);

		/* parse out individual akeys based on key length and
		 * numver of dkeys returned
		 */
		(*hdf5->dk)[*dk_index].akey_offset = *ak_index;
		printf("\n\nWRITE AKEY OFF: %lu\n\n",
			(*hdf5->dk)[*dk_index].akey_offset);
		for (akey_ptr = akey_enum_buf, j = 0; j < akey_number; j++) {
			path_len = snprintf(akey, akey_kds[j].kd_key_len + 1,
					    "%s", akey_ptr);
			if (path_len >= ENUM_KEY_BUF) {
				fprintf(stderr, "akey is too big: %d\n",
					path_len);
				D_GOTO(out, rc = 1);
			}
 			memset(&aiov, 0, sizeof(diov));
			d_iov_set(&aiov, (void*)akey,
				  akey_kds[j].kd_key_len);
			printf("\tj:%d akey:%s len:%d\n", j,
				(char*)aiov.iov_buf,
				(int)akey_kds[j].kd_key_len);
			akey_val = &(*hdf5->ak)[*ak_index].akey_val;
			size = (uint64_t)akey_kds[j].kd_key_len * sizeof(char);
			akey_val->p = malloc(size);
			memcpy(akey_val->p, (void*)akey_ptr,
				(uint64_t)akey_kds[j].kd_key_len);
			akey_val->len = (uint64_t)akey_kds[j].kd_key_len; 
			(*hdf5->ak)[*ak_index].rec_dset_id = *ak_index;

			/* set iod values */
			iod.iod_nr   = 1;
			iod.iod_type = DAOS_IOD_SINGLE;
			iod.iod_size = DAOS_REC_ANY;
			iod.iod_recxs = NULL;

			d_iov_set(&iod.iod_name, (void*)akey, strlen(akey));
			/* do a fetch (with NULL sgl) of single value type,
			* and if that returns iod_size == 0, then a single
			* value does not exist.
			*/
			rc = daos_obj_fetch(*oh, DAOS_TX_NONE, 0, &diov,
					    1, &iod, NULL, NULL, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to fetch object: %d\n",
					rc);
				D_GOTO(out, rc);
			}

			/* if iod_size == 0 then this is a
			 * DAOS_IOD_ARRAY type
			 */
			/* TODO: create a record dset for each
			 * akey
			 */
 			memset(&rec_name, 32, sizeof(rec_name));
			path_len = snprintf(rec_name, 32, "%lu", *ak_index);
			if (path_len > 32) {
				fprintf(stderr, "rec name too long: %d\n",
					path_len);
			}
			if ((int)iod.iod_size == 0) {
				hdf5->rx_dset = H5Dcreate(hdf5->file,
							  rec_name,
							  hdf5->rx_dtype,
							  hdf5->rx_dspace,
							  H5P_DEFAULT,
							  hdf5->plist,
							  H5P_DEFAULT);
				if (hdf5->rx_dset < 0) {
					fprintf(stderr, "failed to create "
						"rx_dset\n");
					D_GOTO(out, rc = 1);
				}
				printf("rx dset created: %lu\n",
					(uint64_t)hdf5->rx_dset);
				printf("rec dset id: %lu\n",
					(*hdf5->ak)[*ak_index].rec_dset_id);
				printf("dset name serialize: %s\n", rec_name);
				printf("ak index: %d\n", (int)*ak_index);
				rc = serialize_recx_array(hdf5, &diov, &aiov,
							  ak_index, oh, &iod);
				if (rc != 0) {
					fprintf(stderr, "failed to serialize "
						"recx array: %d\n", rc);
					D_GOTO(out, rc);
				}
				err = H5Dclose(hdf5->rx_dset);
				if (err < 0) {
					fprintf(stderr, "failed to close "
						"rx_dset\n");
					D_GOTO(out, rc = 1);
				}
			} else {
				hdf5->single_dtype = H5Tcreate(H5T_OPAQUE,
							       iod.iod_size);
				H5Tset_tag(hdf5->single_dtype, "Opaque dtype");
				hdf5->single_dset = H5Dcreate(hdf5->file,
							    rec_name,
							    hdf5->single_dtype,
							    hdf5->single_dspace,
							    H5P_DEFAULT,
							    H5P_DEFAULT,
							    H5P_DEFAULT);
				printf("single dset created: %lu\n",
					(uint64_t)hdf5->single_dset);
				printf("single dset id: %lu\n",
					(*hdf5->ak)[*ak_index].rec_dset_id);
				printf("dset name serialize: %s\n",
					rec_name);
				printf("ak index: %d\n",
					(int)*ak_index);
				rc = serialize_recx_single(hdf5, &diov, oh,
							   &iod);
				if (rc != 0) {
					fprintf(stderr, "failed to serialize "
						"recx single: %d\n", rc);
					D_GOTO(out, rc);
				}
				err = H5Dclose(hdf5->single_dset);
				if (err < 0) {
					fprintf(stderr, "failed to close "
						"single_dspace\n");
					D_GOTO(out, rc = 1);
				}
				err = H5Tclose(hdf5->single_dtype);
				if (err < 0) {
					fprintf(stderr, "failed to close "
						"single_dtype\n");
					D_GOTO(out, rc = 1);
				}
			}
			/* advance to next akey returned */	
			akey_ptr += akey_kds[j].kd_key_len;
			(*ak_index)++;
		}
		*total_akeys = (*total_akeys) + akey_number;
	}
out:
	return rc;
}

static int
serialize_dkeys(struct hdf5_args *hdf5,
		uint64_t *dk_index,
		uint64_t *ak_index,
		uint64_t *total_dkeys,
		uint64_t *total_akeys,
		daos_handle_t *oh,
		int *oid_index)
{
	int		rc = 0;
	herr_t		err = 0;
	int		i = 0;
	daos_anchor_t	dkey_anchor = {0}; 
	d_sg_list_t     dkey_sgl;
	d_iov_t         dkey_iov;
	daos_key_desc_t dkey_kds[ENUM_DESC_NR] = {0};
	uint32_t        dkey_number = ENUM_DESC_NR;
	char            dkey_enum_buf[ENUM_DESC_BUF] = {0};
	char 		dkey[ENUM_KEY_BUF] = {0};
	char		*dkey_ptr;
	daos_key_t	diov;
	int		path_len = 0;
	hvl_t		*dkey_val;
	int		size = 0;

	rc = init_recx_data(hdf5);
	hdf5->oid_data[*oid_index].dkey_offset = *dk_index;
	printf("\n\nWRITE DKEY OFF: %d\n\n",
		(int)hdf5->oid_data[*oid_index].dkey_offset);
	while (!daos_anchor_is_eof(&dkey_anchor)) {
 		memset(dkey_kds, 0, sizeof(dkey_kds));
 		memset(dkey, 0, sizeof(dkey));
 		memset(dkey_enum_buf, 0, sizeof(dkey_enum_buf));
		dkey_number = ENUM_DESC_NR;

                dkey_sgl.sg_nr     = 1;
	        dkey_sgl.sg_nr_out = 0;
	        dkey_sgl.sg_iovs   = &dkey_iov;

	        d_iov_set(&dkey_iov, dkey_enum_buf, ENUM_DESC_BUF);

		/* get dkeys */
		rc = daos_obj_list_dkey(*oh, DAOS_TX_NONE, &dkey_number,
					dkey_kds, &dkey_sgl, &dkey_anchor,
					NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to list dkeys: %d\n", rc);
			D_GOTO(out, rc);
		}
		/* if no dkeys were returned move on */
		if (dkey_number == 0)
			continue;
		*hdf5->dk = realloc(*hdf5->dk,
				(dkey_number + *total_dkeys) * sizeof(dkey_t));
		/* parse out individual dkeys based on key length and
		 * number of dkeys returned
		 */
               	for (dkey_ptr = dkey_enum_buf, i = 0; i < dkey_number; i++) {
			/* Print enumerated dkeys */
			path_len = snprintf(dkey, dkey_kds[i].kd_key_len + 1,
					    "%s", dkey_ptr);
			if (path_len >= ENUM_KEY_BUF) {
				fprintf(stderr, "key is too long: %d\n",
					path_len);
				D_GOTO(out, rc);
			}
 			memset(&diov, 0, sizeof(diov));
			d_iov_set(&diov, (void*)dkey, dkey_kds[i].kd_key_len);
			dkey_val = &(*hdf5->dk)[*dk_index].dkey_val;
			size = (uint64_t)dkey_kds[i].kd_key_len * sizeof(char);
			printf("i:%d dkey iov buf:%s len:%lu\n", i,
				(char*)diov.iov_buf,
				(uint64_t)dkey_kds[i].kd_key_len);
			dkey_val->p = malloc(size);
			memcpy(dkey_val->p, (void*)dkey_ptr,
				(uint64_t)dkey_kds[i].kd_key_len);
			dkey_val->len = (uint64_t)dkey_kds[i].kd_key_len; 
			rc = serialize_akeys(hdf5, diov, dk_index, ak_index,
					     total_akeys, oh); 
			if (rc != 0) {
				fprintf(stderr, "failed to list akeys: %d\n",
					rc);
				D_GOTO(out, rc);
			}
			dkey_ptr += dkey_kds[i].kd_key_len;
			(*dk_index)++;
		}
		*total_dkeys = (*total_dkeys) + dkey_number;
	}
	err = H5Sclose(hdf5->rx_dspace);
	if (err < 0) {
		fprintf(stderr, "failed to close rx_dspace\n");
		D_GOTO(out, rc = 1);
	}
	err = H5Sclose(hdf5->single_dspace);
	if (err < 0) {
		fprintf(stderr, "failed to close single_dspace\n");
		D_GOTO(out, rc = 1);
	}
	err = H5Tclose(hdf5->rx_dtype);
	if (err < 0) {
		fprintf(stderr, "failed to close rx_dtype\n");
		D_GOTO(out, rc = 1);
	}
out:
	return rc;
}

static int
init_hdf5_file(struct hdf5_args *hdf5, char *filename) {
	int rc = 0;
	hdf5->file = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT,
			      H5P_DEFAULT);
	if (hdf5->file < 0) {
		fprintf(stderr, "failed to create HDF5 file\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->oid_memtype = H5Tcreate(H5T_COMPOUND, sizeof(oid_t));
	if (hdf5->oid_memtype < 0) {
		fprintf(stderr, "failed to create oid memtype HDF5\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->status = H5Tinsert(hdf5->oid_memtype, "OID Hi",
				HOFFSET(oid_t, oid_hi), H5T_NATIVE_UINT64);
	if (hdf5->status < 0) {
		fprintf(stderr, "failed to insert oid hi\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->status = H5Tinsert(hdf5->oid_memtype, "OID Low",
				HOFFSET(oid_t, oid_low), H5T_NATIVE_UINT64);
	if (hdf5->status < 0) {
		fprintf(stderr, "failed to insert oid low\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->status = H5Tinsert(hdf5->oid_memtype, "Dkey Offset",
				HOFFSET(oid_t, dkey_offset), H5T_NATIVE_UINT64);
	if (hdf5->status < 0) {
		fprintf(stderr, "failed to insert dkey offset\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->dkey_memtype = H5Tcreate(H5T_COMPOUND, sizeof(dkey_t));
	if (hdf5->dkey_memtype < 0) {
		fprintf(stderr, "failed to create dkey memtype HDF5: %d\n", rc);
		D_GOTO(out, rc = 1);
	}
	hdf5->dkey_vtype = H5Tvlen_create(H5T_NATIVE_CHAR);
	if (hdf5->dkey_vtype < 0) {
		fprintf(stderr, "failed to create dkey vtype HDF5: %d\n", rc);
		D_GOTO(out, rc = 1);
	}
	hdf5->status = H5Tinsert(hdf5->dkey_memtype, "Akey Offset",
		  		HOFFSET(dkey_t, akey_offset),
				H5T_NATIVE_UINT64);
	if (hdf5->status < 0) {
		fprintf(stderr, "failed to insert akey offset\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->status = H5Tinsert(hdf5->dkey_memtype, "Dkey Value",
				HOFFSET(dkey_t, dkey_val), hdf5->dkey_vtype);
	if (hdf5->status < 0) {
		fprintf(stderr, "failed to insert dkey value\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->akey_memtype = H5Tcreate(H5T_COMPOUND, sizeof(akey_t));
	if (hdf5->akey_memtype < 0) {
		fprintf(stderr, "failed to create akey memtype HDF5: %d\n", rc);
		D_GOTO(out, rc = 1);
	}
	hdf5->akey_vtype = H5Tvlen_create(H5T_NATIVE_CHAR);
	if (hdf5->akey_vtype < 0) {
		fprintf(stderr, "failed to create akey vtype HDF5: %d\n", rc);
		D_GOTO(out, rc = 1);
	}
	hdf5->status = H5Tinsert(hdf5->akey_memtype, "Dataset ID",
				HOFFSET(akey_t, rec_dset_id),
				H5T_NATIVE_UINT64);
	if (hdf5->status < 0) {
		fprintf(stderr, "failed to insert record dset id\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->status = H5Tinsert(hdf5->akey_memtype, "Akey Value",
				HOFFSET(akey_t, akey_val), hdf5->akey_vtype);
	if (hdf5->status < 0) {
		fprintf(stderr, "failed to insert akey value\n");
		D_GOTO(out, rc = 1);
	}
out:
	return rc;
}

static int
cont_serialize_version(struct hdf5_args *hdf5, float version)
{
	int	rc = 0;
	hid_t	status = 0;
	char	*version_name = "Version";

	hdf5->version_attr_dims[0] = 1;
	hdf5->version_attr_type = H5Tcopy(H5T_NATIVE_FLOAT);
	status = H5Tset_size(hdf5->version_attr_type, 4);
	if (status < 0) {
		fprintf(stderr, "failed to create version dtype\n");
		D_GOTO(out, rc = 1);
	}
	if (hdf5->version_attr_type < 0) {
		fprintf(stderr, "failed to create version attr type\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->version_attr_dspace = H5Screate_simple(1, hdf5->version_attr_dims,
				                     NULL);
	if (hdf5->version_attr_dspace < 0) {
		fprintf(stderr, "failed to create version attribute "
			"dataspace\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->version_attr = H5Acreate2(hdf5->file,
		            	       version_name,
				       hdf5->version_attr_type,
				       hdf5->version_attr_dspace,
				       H5P_DEFAULT,
				       H5P_DEFAULT);
	if (hdf5->version_attr < 0) {
		fprintf(stderr, "failed to create version "
			"attribute\n");
		D_GOTO(out, rc = 1);
	}	
	status = H5Awrite(hdf5->version_attr, hdf5->version_attr_type,
			  &version);
	if (status < 0) {
		fprintf(stderr, "failed to write attribute\n");
		D_GOTO(out, rc = 1);
	}
	status = H5Aclose(hdf5->version_attr);
	if (status < 0) {
		fprintf(stderr, "failed to close attribute\n");
		D_GOTO(out, rc = 1);
	}
out:
	return rc;
}

static int
cont_serialize_num_usr_attrs(struct hdf5_args *hdf5,
			     daos_handle_t cont,
			     uint64_t *total_size)
{
	int		rc = 0;
	hid_t		status = 0;
	char		*total_usr_attrs = "Total User Attributes";

	/* record total number of user defined attributes, if it is 0,
	 * then we don't write any more attrs */
	rc = daos_cont_list_attr(cont, NULL, total_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to retrieve number of attributes for "
			"container\n");
		D_GOTO(out, rc);
	}

	hdf5->attr_dims[0] = 1;
	hdf5->attr_dtype = H5Tcopy(H5T_NATIVE_INT);
	status = H5Tset_size(hdf5->attr_dtype, 8);
	if (status < 0) {
		fprintf(stderr, "failed to create version dtype\n");
		D_GOTO(out, rc = 1);
	}
	if (hdf5->attr_dtype < 0) {
		fprintf(stderr, "failed to create usr attr type\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->attr_dspace = H5Screate_simple(1, hdf5->attr_dims,
				             NULL);
	if (hdf5->attr_dspace < 0) {
		fprintf(stderr, "failed to create version attribute "
			"dataspace\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->usr_attr_num = H5Acreate2(hdf5->file,
		            	       total_usr_attrs,
				       hdf5->attr_dtype,
				       hdf5->attr_dspace,
				       H5P_DEFAULT,
				       H5P_DEFAULT);
	if (hdf5->usr_attr_num < 0) {
		fprintf(stderr, "failed to create version "
			"attribute\n");
		D_GOTO(out, rc = 1);
	}	
	status = H5Awrite(hdf5->usr_attr_num, hdf5->attr_dtype,
			  total_size);
	if (status < 0) {
		fprintf(stderr, "failed to write attribute\n");
		D_GOTO(out, rc = 1);
	}
	status = H5Aclose(hdf5->usr_attr_num);
	if (status < 0) {
		fprintf(stderr, "failed to close attribute\n");
		D_GOTO(out, rc = 1);
	}
out:
	return rc;
}

static int
cont_serialize_usr_attrs(struct hdf5_args *hdf5, daos_handle_t cont)
{
	int		rc = 0;
	int		i = 0;
	int		j = 0;
	//hid_t		status = 0;
	uint64_t	total_size = 0;
	uint64_t	size = 0;
	uint64_t	expected_size = 0;
	uint64_t	cur = 0;
	uint64_t	len = 0;
	char		*buf = NULL;
	char		**names = NULL;
	int		num_attrs = 0;

	rc = cont_serialize_num_usr_attrs(hdf5, cont, &total_size);
	if (rc != 0) {
		fprintf(stderr, "failed to serialize number of user attrs\n");
		//D_GOTO(out, rc);
	}

	/* skip serializing attrs if total_size is zero */
	if (total_size == 0) {
		D_PRINT("No attributes\n");
		//D_GOTO(out, rc);
	}

	D_ALLOC(buf, total_size);
	if (buf == NULL) {
		//D_GOTO(out, rc = -DER_NOMEM);
	}

	expected_size = total_size;
	rc = daos_cont_list_attr(cont, buf, &total_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to list attributes for container\n");
		//D_GOTO(out, rc);
	}

	if (expected_size < total_size)
		fprintf(stderr, "size required to gather all attributes has "
			"raised, list has been truncated\n");
	size = min(expected_size, total_size);
	while (cur < size) {
		len = strnlen(buf + cur, size - cur);
		if (len == size - cur) {
			fprintf(stderr,
				"end of buf reached but no end of string "
				"encountered, ignoring\n");
			break;
		}
		num_attrs++;
		cur += len + 1;
	}

	/* allocate array of null-terminated attribute names */
	names = malloc(num_attrs * sizeof(char *));
	if (names == NULL) {
		//D_GOTO(out, rc = -DER_NOMEM);
	}

	cur = 0;
	/* create array of attribute names to pass to daos_cont_get_attr */
	for (i = 0; i < num_attrs; i++) {
		len = strnlen(buf + cur, size - cur);
		if (len == size - cur) {
			fprintf(stderr,
				"end of buf reached but no end of "
				"string encountered, ignoring\n");
			break;
		}
		names[i] = strdup(buf + cur);
		cur += len + 1;
	}

	size_t	sizes[num_attrs];
	void	*val_buf[num_attrs];

	rc = daos_cont_get_attr(cont, num_attrs,
				(const char* const*)names,
				NULL, sizes, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to get attr size\n");
		D_GOTO(out, rc);
	}
	for (j = 0; j < num_attrs; j++) {
		D_ALLOC(val_buf[j], 256);
	}
	rc = daos_cont_get_attr(cont, num_attrs,
				(const char* const*)names,
				(void * const*)&val_buf, sizes,
				NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to get attr value\n");
		D_GOTO(out, rc);
	}
	for (i = 0; i < num_attrs; i++) {
		printf("attr size: %d\n", (int)sizes[i]);
		char str[256];
		snprintf(str, sizes[i] + 1, "%s", (char *)val_buf[i]);
		printf("attr val: %s\n", str);
	}
	/* TODO: write user attrs to hdf5 file */
out:
	if (buf != NULL)
		D_FREE(buf);
	for (j = 0; j < num_attrs; j++) {
		D_FREE(val_buf[j]);
	}
	if (names != NULL) 
		D_FREE(names);
	return rc;
}

int
cont_serialize_hdlr(struct cmd_args_s *ap)
{
	int		rc = 0;
	int		i = 0;
	char		*ftype = ".h5";
	/* TODO: update this  to PATH_MAX, currently using too much
	 * static memory to use it */
	int		PMAX = 64;
	char		filename[PMAX];
	int		path_len = 0;
	struct		hdf5_args hdf5;
	uint64_t	total_dkeys = 0;
	uint64_t	total_akeys = 0;
	uint64_t	dk_index = 0;
	uint64_t	ak_index = 0;
 	daos_obj_id_t	oids[OID_ARR_SIZE];
 	daos_anchor_t	anchor;
 	uint32_t	oids_nr;
 	int		num_oids;
 	daos_handle_t	toh;
 	daos_epoch_t	epoch;
	uint32_t	total = 0;
	daos_handle_t	oh;
	float		version = 0.0;

	/* init HDF5 args */
	init_hdf5_args(&hdf5);

	path_len = snprintf(filename, PMAX, "%s", DP_UUID(ap->c_uuid));
	if (path_len >= PMAX) {
		fprintf(stderr, "filename is too big: %d\n", path_len);
		D_GOTO(out, rc = 1);
	}
	strcat(filename, ftype);
	printf("Serializing Container "DF_UUIDF" to "DF_UUIDF".h5\n",
		DP_UUID(ap->c_uuid), DP_UUID(ap->c_uuid));

	/* init HDF5 datatypes in HDF5 file */
	rc = init_hdf5_file(&hdf5, filename);
	if (rc != 0) {
		fprintf(stderr, "failed to init hdf5 file\n");
		D_GOTO(out, rc);
	}
	rc = daos_cont_create_snap_opt(ap->cont, &epoch, NULL,
					DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
					NULL);
	if (rc) {
		fprintf(stderr, "failed to create snapshot: %d\n", rc);	
		D_GOTO(out, rc);
	}

 	rc = daos_oit_open(ap->cont, epoch, &toh, NULL);
	if (rc) {
		fprintf(stderr, "failed to open oit: %d\n", rc);	
		D_GOTO(out, rc);
	}
 	memset(&anchor, 0, sizeof(anchor));
	while (1) {
 		oids_nr = OID_ARR_SIZE;
 		rc = daos_oit_list(toh, oids, &oids_nr, &anchor, NULL);
		if (rc) {
			fprintf(stderr, "failed to open oit: %d\n", rc);	
			D_GOTO(out, rc);
		}
		num_oids = (int)oids_nr;
 		D_PRINT("returned %d oids\n", num_oids);
		hdf5.oid_dims[0] = num_oids;
		hdf5.oid_dspace = H5Screate_simple(1, hdf5.oid_dims, NULL);
		if (hdf5.oid_dspace < 0) {
			fprintf(stderr, "failed to create oid dspace: %d\n",
				rc);
			D_GOTO(out, rc = 1);
		}
		hdf5.oid_dset = H5Dcreate(hdf5.file, "Oid Data",
					  hdf5.oid_memtype, hdf5.oid_dspace,
					  H5P_DEFAULT, H5P_DEFAULT,
					  H5P_DEFAULT);
		if (hdf5.oid_dset < 0) {
			fprintf(stderr, "failed to create oid dset: %d\n", rc);
			D_GOTO(out, rc = 1);
		}

		total_dkeys = 0;
		total_akeys = 0;
		hdf5.oid_data = malloc(num_oids * sizeof(oid_t));
		hdf5.dkey_data = malloc(sizeof(dkey_t));
		hdf5.akey_data = malloc(sizeof(akey_t));
		dk_index = 0;
		ak_index = 0;
		hdf5.dk = &(hdf5.dkey_data);
		hdf5.ak = &(hdf5.akey_data);

		/* list object ID's */
 		for (i = 0; i < num_oids; i++) {
 			D_PRINT("oid[%d] ="DF_OID"\n", total, DP_OID(oids[i]));
			/* open DAOS object based on oid[i] to get obj
			 * handle
			 */
			hdf5.oid_data[i].oid_hi = oids[i].hi;
			hdf5.oid_data[i].oid_low = oids[i].lo;
			rc = daos_obj_open(ap->cont, oids[i], 0, &oh, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to open object: %d\n",
					rc);
					D_GOTO(out, rc);
			}
			rc = serialize_dkeys(&hdf5, &dk_index, &ak_index,
						 &total_dkeys, &total_akeys,
						 &oh, &i);
			if (rc != 0) {
				fprintf(stderr, "failed to serialize keys: %d\n",
					rc);
					D_GOTO(out, rc);
			}
			/* close source and destination object */
                       	rc = daos_obj_close(oh, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to close obj: %d\n",
					rc);
					D_GOTO(out, rc);
			}
 			total++;
	        }

	        printf("total dkeys: %lu\n", total_dkeys);
	        printf("total akeys: %lu\n", total_akeys);

		/* write container version as attribute */
		rc = cont_serialize_version(&hdf5, version);
		if (rc != 0) {
			fprintf(stderr, "failed to serialize version: %d\n",
				rc);
			D_GOTO(out, rc);
		}

		rc = cont_serialize_usr_attrs(&hdf5, ap->cont);
		if (rc != 0) {
			fprintf(stderr, "failed to serialize usser attributes: "
				"%d\n", rc);
			D_GOTO(out, rc);
		}

		hdf5.dkey_dims[0] = total_dkeys;     
		hdf5.dkey_dspace = H5Screate_simple(1, hdf5.dkey_dims, NULL);
		if (hdf5.dkey_dspace < 0) {
			fprintf(stderr, "failed to create dkey dspace: %d\n",
				rc);
			D_GOTO(out, rc = 1);
		}
		hdf5.dkey_dset = H5Dcreate(hdf5.file, "Dkey Data",
					   hdf5.dkey_memtype, hdf5.dkey_dspace,
					   H5P_DEFAULT, H5P_DEFAULT,
					   H5P_DEFAULT);
		if (hdf5.dkey_dset < 0) {
			fprintf(stderr, "failed to create dkey dset: %d\n",
				rc);
			D_GOTO(out, rc = 1);
		}
		hdf5.akey_dims[0] = total_akeys;     
		hdf5.akey_dspace = H5Screate_simple(1, hdf5.akey_dims, NULL);
		if (hdf5.akey_dspace < 0) {
			fprintf(stderr, "failed to create akey dspace: %d\n",
				rc);
			D_GOTO(out, rc = 1);
		}
		hdf5.akey_dset = H5Dcreate(hdf5.file, "Akey Data",
					   hdf5.akey_memtype, hdf5.akey_dspace,
					   H5P_DEFAULT, H5P_DEFAULT,
					   H5P_DEFAULT);
		if (hdf5.akey_dset < 0) {
			fprintf(stderr, "failed to create akey dspace: %d\n",
				rc);
			D_GOTO(out, rc = 1);
		}
		hdf5.status = H5Dwrite(hdf5.oid_dset, hdf5.oid_memtype, H5S_ALL,
					H5S_ALL, H5P_DEFAULT, hdf5.oid_data);
		if (hdf5.status < 0) {
			rc = 1;
			fprintf(stderr, "failed to write oid dset: %d\n", rc);
			D_GOTO(out, rc = 1);
		}
		hdf5.status = H5Dwrite(hdf5.dkey_dset, hdf5.dkey_memtype,
				       H5S_ALL, H5S_ALL, H5P_DEFAULT,
				       (*(hdf5.dk)));
		if (hdf5.status < 0) {
			rc = 1;
			fprintf(stderr, "failed to write dkey dset: %d\n", rc);
			D_GOTO(out, rc = 1);
		}
		hdf5.status = H5Dwrite(hdf5.akey_dset, hdf5.akey_memtype,
				       H5S_ALL, H5S_ALL, H5P_DEFAULT,
				       (*(hdf5.ak)));
		if (hdf5.status < 0) {
			rc = 1;
			fprintf(stderr, "failed to write akey dset: %d\n", rc);
			D_GOTO(out, rc = 1);
		}
 
 		if (daos_anchor_is_eof(&anchor)) {
 			break;
 		}
	}

	/* close object iterator */
 	rc = daos_oit_close(toh, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to close oit: %d\n", rc);
		D_GOTO(out, rc);
	}
	daos_epoch_range_t epr;
	epr.epr_lo = epoch;
	epr.epr_hi = epoch;
	rc = daos_cont_destroy_snap(ap->cont, epr, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to destroy snap: %d\n", rc);
		D_GOTO(out, rc);
	}
out:
	if (hdf5.oid_dset > 0)
		H5Dclose(hdf5.oid_dset);
	if (hdf5.dkey_dset > 0)
		H5Dclose(hdf5.dkey_dset);
	if (hdf5.akey_dset > 0)
		H5Dclose(hdf5.akey_dset);
	if (hdf5.oid_dspace > 0)
		H5Sclose(hdf5.oid_dspace);
	if (hdf5.dkey_dspace > 0)
		H5Sclose(hdf5.dkey_dspace);
	if (hdf5.akey_dspace > 0)
		H5Sclose(hdf5.akey_dspace);
	if (hdf5.oid_memtype > 0)
		H5Tclose(hdf5.oid_memtype);
	if (hdf5.dkey_memtype > 0)
		H5Tclose(hdf5.dkey_memtype);
	if (hdf5.akey_memtype > 0)
		H5Tclose(hdf5.akey_memtype);
	if (hdf5.oid_data != NULL)
		free(hdf5.oid_data);
	if (hdf5.dkey_data != NULL)
		free(hdf5.dkey_data);
	if (hdf5.akey_data != NULL)
		free(hdf5.akey_data);
	return rc;
}

static int
hdf5_read_key_data(struct hdf5_args *hdf5)
{
	int	rc = 0;
	hid_t	status = 0;
	int	oid_ndims = 0;
	int	dkey_ndims = 0;
	int	akey_ndims = 0;

	/* read oid data */
	hdf5->oid_dset = H5Dopen(hdf5->file, "Oid Data", H5P_DEFAULT);
	if (hdf5->oid_dset < 0) {
		fprintf(stderr, "failed to open OID dset\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->oid_dspace = H5Dget_space(hdf5->oid_dset);
	if (hdf5->oid_dspace < 0) {
		fprintf(stderr, "failed to get oid dspace\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->oid_dtype = H5Dget_type(hdf5->oid_dset);
	if (hdf5->oid_dtype < 0) {
		fprintf(stderr, "failed to get oid dtype\n");
		D_GOTO(out, rc = 1);
	}
	oid_ndims = H5Sget_simple_extent_dims(hdf5->oid_dspace, hdf5->oid_dims,
					      NULL);
	if (oid_ndims < 0) {
		fprintf(stderr, "failed get oid dimensions\n");
		D_GOTO(out, rc = 1);
	}
	printf("oid_dims: %d\n", (int)oid_ndims);
	hdf5->oid_data = malloc(hdf5->oid_dims[0] * sizeof(oid_t));
	if (hdf5->oid_data == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	status = H5Dread(hdf5->oid_dset, hdf5->oid_dtype, H5S_ALL, H5S_ALL,
			H5P_DEFAULT, hdf5->oid_data);
	if (status < 0) {
		fprintf(stderr, "failed to read OID data\n");
		D_GOTO(out, rc = 1);
	}

	/* read dkey data */
	hdf5->dkey_dset = H5Dopen(hdf5->file, "Dkey Data", H5P_DEFAULT);
	if (hdf5->dkey_dset < 0) {
		fprintf(stderr, "failed to open dkey dset\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->dkey_dspace = H5Dget_space(hdf5->dkey_dset);
	if (hdf5->dkey_dspace < 0) {
		fprintf(stderr, "failed to get dkey dspace\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->dkey_vtype = H5Dget_type(hdf5->dkey_dset);
	if (hdf5->dkey_vtype < 0) {
		fprintf(stderr, "failed to get dkey vtype\n");
		D_GOTO(out, rc = 1);
	}
	dkey_ndims = H5Sget_simple_extent_dims(hdf5->dkey_dspace,
						       hdf5->dkey_dims, NULL);
	if (dkey_ndims < 0) {
		fprintf(stderr, "failed to get dkey dimensions\n");
		D_GOTO(out, rc = 1);
	}
	printf("dkey_dims: %d\n", (int)dkey_ndims);
	hdf5->dkey_data = malloc(hdf5->dkey_dims[0] * sizeof(dkey_t));
	if (hdf5->dkey_data == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	status = H5Dread(hdf5->dkey_dset, hdf5->dkey_vtype, H5S_ALL, H5S_ALL,
			 H5P_DEFAULT, hdf5->dkey_data);
	if (status < 0) {
		fprintf(stderr, "failed to read Dkey data\n");
		D_GOTO(out, rc = 1);
	}

	/* read akey data */
	hdf5->akey_dset = H5Dopen(hdf5->file, "Akey Data", H5P_DEFAULT);
	if (hdf5->akey_dset < 0) {
		fprintf(stderr, "failed to open akey dset\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->akey_dspace = H5Dget_space(hdf5->akey_dset);
	if (hdf5->akey_dspace < 0) {
		fprintf(stderr, "failed to get akey dspace\n");
		D_GOTO(out, rc = 1);
	}
	hdf5->akey_vtype = H5Dget_type(hdf5->akey_dset);
	if (hdf5->akey_vtype < 0) {
		fprintf(stderr, "failed to get akey vtype\n");
		D_GOTO(out, rc = 1);
	}
	akey_ndims = H5Sget_simple_extent_dims(hdf5->akey_dspace,
					       hdf5->akey_dims, NULL);
	if (akey_ndims < 0) {
		fprintf(stderr, "failed to get akey dimensions\n");
		D_GOTO(out, rc = 1);
	}
	printf("akey_dims: %d\n", (int)akey_ndims);
	hdf5->akey_data = malloc(hdf5->akey_dims[0] * sizeof(akey_t));
	if (hdf5->akey_data == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	status = H5Dread(hdf5->akey_dset, hdf5->akey_vtype, H5S_ALL, H5S_ALL,
			 H5P_DEFAULT, hdf5->akey_data);
	if (status < 0) {
		fprintf(stderr, "failed to read Akey data\n");
		D_GOTO(out, rc = 1);
	}
out:
	return rc;
}

static int
cont_deserialize_recx(struct hdf5_args *hdf5,
		      daos_handle_t *oh,
		      daos_key_t diov,
		      int num_attrs,
		      uint64_t ak_off,
		      int k)
{
	int			rc = 0;
	hid_t			status = 0;
	int			i = 0;
	ssize_t			attr_len = 0;
	char			attr_name_buf[124]; 
	hsize_t			attr_space;
	hid_t			attr_type;
	size_t			type_size;
	unsigned char		*decode_buf;
	hid_t			rx_range_id;
	hsize_t			rx_range[64] = {0};
	uint64_t		recx_len = 0;
	void			*recx_data = NULL;
	hssize_t		nblocks_sel;
	hssize_t		nblocks = 0;
	d_sg_list_t		sgl;
	d_iov_t			iov;
	daos_iod_t		iod;
	daos_recx_t		recxs;
	hid_t			aid;

	for (i = 0; i < num_attrs; i++) {
 		memset(attr_name_buf, 0, sizeof(attr_name_buf));
		aid = H5Aopen_idx(hdf5->rx_dset, (unsigned int)i);
		if (aid < 0) {
			fprintf(stderr, "failed to get open attr\n");
			D_GOTO(out, rc = 1);
		}
		attr_len = H5Aget_name(aid, 124, attr_name_buf);
		if (attr_len < 0) {
			fprintf(stderr, "failed to get attr name\n");
			D_GOTO(out, rc = 1);
		}
		printf("\t\t\t    Attribute Name : %s\n", attr_name_buf);
		printf("\t\t\t    Attribute Len : %d\n",(int)attr_len);
		attr_space = H5Aget_storage_size(aid);
		if (attr_len < 0) {
			fprintf(stderr, "failed to attr space\n");
			D_GOTO(out, rc = 1);
		}
		attr_type = H5Aget_type(aid);
		if (attr_type < 0) {
			fprintf(stderr, "failed to get attr type\n");
			D_GOTO(out, rc = 1);
		}
		type_size = H5Tget_size(attr_type);
		if (type_size < 0) {
			fprintf(stderr, "failed to get type size\n");
			D_GOTO(out, rc = 1);
		}
		printf("\t\t\ttype size: %d\n", (int)type_size);
		printf("\t\t\tattr id: %lu\n", (uint64_t)aid);
		printf("\t\t\tattr space: %d\n", (int)attr_space);
		
		decode_buf = malloc(type_size * attr_space);
		if (decode_buf == NULL) {
			D_GOTO(out, -DER_NOMEM);
		}
		status = H5Aread(aid, attr_type, decode_buf);
		if (status < 0) {
			fprintf(stderr, "failed to read attribute\n");
			D_GOTO(out, rc = 1);
		}
		rx_range_id = H5Sdecode(decode_buf);
		if (rx_range_id < 0) {
			fprintf(stderr, "failed to decode attribute buffer\n");
			D_GOTO(out, rc = 1);
		}
 		memset(rx_range, 0, sizeof(rx_range));
		nblocks = H5Sget_select_hyper_nblocks(rx_range_id);
		if (nblocks < 0) {
			fprintf(stderr, "failed to get hyperslab blocks\n");
			D_GOTO(out, rc = 1);
		}
		status = H5Sget_select_hyper_blocklist(rx_range_id, 0,
						       nblocks, rx_range);
		if (status < 0) {
			fprintf(stderr, "failed to get blocklist\n");
			D_GOTO(out, rc = 1);
		}
		printf("\t\t\tRX IDX: %d\n", (int)rx_range[0]);
		printf("\t\t\tRX NR: %d\n", (int)rx_range[1]);

		/* read recx data then update */
		hdf5->rx_dspace = H5Dget_space(hdf5->rx_dset);
		if (hdf5->rx_dspace < 0) {
			fprintf(stderr, "failed to get rx_dspace\n");
			D_GOTO(out, rc = 1);
		}

		/* TODO: remove these debugging calls */
		hsize_t dset_size = H5Sget_simple_extent_npoints(hdf5->rx_dspace);
		printf("DSET_SIZE RX DSPACE: %d\n", (int)dset_size);
		hsize_t dset_size2 = H5Sget_simple_extent_npoints(rx_range_id);
		printf("DSET_SIZE RX ID: %d\n", (int)dset_size2);

		hsize_t start = rx_range[0];
		hsize_t count = (rx_range[1] - rx_range[0]) + 1;
		status = H5Sselect_hyperslab(hdf5->rx_dspace,
				    	     H5S_SELECT_AND,
					     &start, NULL,
					     &count, NULL);
		if (status < 0) {
			fprintf(stderr, "failed to select hyperslab\n");
			D_GOTO(out, rc = 1);
		}
		recx_len = count * 1;
		recx_data = malloc(recx_len);
		printf("\t\t\tRECX LEN: %d\n", (int)recx_len);
		nblocks_sel = H5Sget_select_hyper_nblocks(hdf5->rx_dspace);
		printf("NUM BLOCKS SELECTED: %d\n", (int)nblocks_sel);
		hdf5->mem_dims[0] = count;
		hdf5->rx_memspace = H5Screate_simple(1, hdf5->mem_dims,
						     hdf5->mem_dims);
		status = H5Dread(hdf5->rx_dset,
				 hdf5->rx_dtype,
				 hdf5->rx_memspace,
				 hdf5->rx_dspace,
				 H5P_DEFAULT,
				 recx_data);
		if (status < 0) {
			fprintf(stderr, "failed to read record extent\n");
				D_GOTO(out, rc = 1);
		}
 		memset(&sgl, 0, sizeof(sgl));
 		memset(&iov, 0, sizeof(iov));
 		memset(&iod, 0, sizeof(iod));
 		memset(&recxs, 0, sizeof(recxs));
		d_iov_set(&iod.iod_name,
			  (void*)hdf5->akey_data[ak_off + k].akey_val.p,
			  hdf5->akey_data[ak_off + k].akey_val.len);
		/* set iod values */
		iod.iod_type  = DAOS_IOD_ARRAY;
		iod.iod_size  = 1;
		iod.iod_nr    = 1;

		printf("START TO WRITE: %d\n", (int)start);
		printf("COUNT TO WRITE: %d\n", (int)count);
		recxs.rx_nr = recx_len;
		recxs.rx_idx = start;
		iod.iod_recxs = &recxs;

		/* set sgl values */
		sgl.sg_nr     = 1;
		sgl.sg_iovs   = &iov;

		d_iov_set(&iov, recx_data, recx_len);	
		/* update fetched recx values and place in destination object */
             	rc = daos_obj_update(*oh, DAOS_TX_NONE, 0, &diov, 1, &iod,
				     &sgl, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to update object: %d\n", rc);
			D_GOTO(out, rc);
		}
		H5Aclose(aid);
		D_FREE(recx_data);
		D_FREE(decode_buf);
	}
out:
	return rc;
}

static int
cont_deserialize_keys(struct hdf5_args *hdf5,
		      uint64_t *total_dkeys_this_oid,
		      uint64_t *dk_off,
		      daos_handle_t *oh)
{
	int			rc = 0;
	hid_t			status = 0;
	int			j = 0;
        daos_key_t		diov;
      	char			dkey[ENUM_KEY_BUF] = {0};
	uint64_t		ak_off = 0;
	uint64_t		ak_next = 0;
	uint64_t		total_akeys_this_dkey = 0;
	int			k = 0;
        daos_key_t		aiov;
        char			akey[ENUM_KEY_BUF] = {0};
	int			rx_ndims;
	uint64_t		index = 0;
	int			len = 0;
	int			num_attrs;
	size_t			single_tsize;
	char			*single_data = NULL;
	d_sg_list_t		sgl;
	d_iov_t			iov;
	daos_iod_t		iod;
	
	for(j = 0; j < *total_dkeys_this_oid; j++) {
		memset(&diov, 0, sizeof(diov));
		memset(dkey, 0, sizeof(dkey));
		snprintf(dkey,
			 hdf5->dkey_data[*dk_off + j].dkey_val.len + 1,
			 "%s",
			 (char*)(hdf5->dkey_data[*dk_off + j].dkey_val.p));
		d_iov_set(&diov,
			  (void*)hdf5->dkey_data[*dk_off + j].dkey_val.p,
			  hdf5->dkey_data[*dk_off + j].dkey_val.len);
		printf("\tDKEY VAL: %s\n", (char*)dkey);
		printf("\tDKEY VAL LEN: %lu\n",
			hdf5->dkey_data[*dk_off + j].dkey_val.len);
		ak_off = hdf5->dkey_data[*dk_off + j].akey_offset;
		ak_next = 0;
		total_akeys_this_dkey = 0;
		if (*dk_off + j + 1 < (int)hdf5->dkey_dims[0]) {
			ak_next = hdf5->dkey_data[(*dk_off + j) + 1].akey_offset;
			total_akeys_this_dkey = ak_next - ak_off;
		} else if (*dk_off + j == ((int)hdf5->dkey_dims[0] - 1)) {
			total_akeys_this_dkey = ((int)hdf5->akey_dims[0]) - ak_off;
		}
		printf("\nTOTAL AK THIS DK: %lu\n",
			total_akeys_this_dkey);
		for(k = 0; k < total_akeys_this_dkey; k++) {
			memset(&aiov, 0, sizeof(aiov));
			memset(akey, 0, sizeof(akey));
			snprintf(akey,
				 hdf5->akey_data[ak_off + k].akey_val.len + 1,
				 "%s",
				(char*)hdf5->akey_data[ak_off + k].akey_val.p);
			d_iov_set(&aiov,
				  (void*)hdf5->akey_data[ak_off + k].akey_val.p,
				  hdf5->akey_data[ak_off + k].akey_val.len);
			printf("\t\tAKEY VAL: %s\n", (char*)akey);
			printf("\t\tAKEY VAL LEN: %lu\n",
			       hdf5->akey_data[ak_off + k].akey_val.len);

			/* read record data for each akey */
			index = ak_off + k;
			len = snprintf(NULL, 0, "%lu", index);
			char dset_name[len + 1];	
			snprintf(dset_name, len + 1, "%lu", index);
			printf("\t\t\tdset name: %s\n", dset_name);
			hdf5->rx_dset = H5Dopen(hdf5->file, dset_name,
						H5P_DEFAULT);
			if (hdf5->rx_dset < 0) {
				fprintf(stderr,
					"failed to open rx_dset\n");
				D_GOTO(out, rc = 1);
			}
			printf("\t\t\trx_dset: %lu\n", (uint64_t)hdf5->rx_dset);
			hdf5->rx_dspace = H5Dget_space(hdf5->rx_dset);
			if (hdf5->rx_dspace < 0) {
				fprintf(stderr,
					"failed to get rx_dspace\n");
				D_GOTO(out, rc = 1);
			}
			printf("\t\t\trx_dspace id: %d\n", (int)hdf5->rx_dspace);
			hdf5->rx_dtype = H5Dget_type(hdf5->rx_dset);
			if (hdf5->rx_dtype < 0) {
				fprintf(stderr,
					"failed to get rx_dtype\n");
				D_GOTO(out, rc = 1);
			}
			hdf5->plist = H5Dget_create_plist(hdf5->rx_dset);
			if (hdf5->plist < 0) {
				fprintf(stderr,
					"failed to get plist\n");
				D_GOTO(out, rc = 1);
			}
			rx_ndims = H5Sget_simple_extent_dims(hdf5->rx_dspace,
							     hdf5->rx_dims,
							     NULL);
			if (rx_ndims < 0) {
				fprintf(stderr,
					"failed to get rx_ndims\n");
				D_GOTO(out, rc = 1);
			}
			printf("\t\t\trx_dims: %d\n", (int)hdf5->rx_dims[0]);
			num_attrs = H5Aget_num_attrs(hdf5->rx_dset);
			if (num_attrs < 0) {
				fprintf(stderr,
					"failed to get num attrs\n");
				D_GOTO(out, rc = 1);
			}
			printf("\t\t\tnum attrs: %d\n", num_attrs);
			if (num_attrs > 0) {
				rc = cont_deserialize_recx(hdf5, oh, diov,
							   num_attrs, ak_off,
							   k);
				if (rc != 0) {
					fprintf(stderr, "failed to deserialize "
						"recx\n");
					D_GOTO(out, rc);
				}
			} else {
				memset(&sgl, 0, sizeof(sgl));
 				memset(&iov, 0, sizeof(iov));
 				memset(&iod, 0, sizeof(iod));
				single_tsize = H5Tget_size(hdf5->rx_dtype);
				single_data = malloc(single_tsize);
				printf("\t\t\tSINGLE LEN: %d\n", (int)single_tsize);
				status = H5Dread(hdf5->rx_dset,
						    hdf5->rx_dtype,
						    H5S_ALL, hdf5->rx_dspace,
						    H5P_DEFAULT, single_data);
				if (status < 0) {
					fprintf(stderr, "failed to read record\n");
					D_GOTO(out, rc = 1);
				}
				d_iov_set(&iod.iod_name,
					  (void*)hdf5->akey_data[ak_off + k].akey_val.p,
					  hdf5->akey_data[ak_off + k].akey_val.len);

				/* set iod values */
				iod.iod_type  = DAOS_IOD_SINGLE;
				iod.iod_size  = single_tsize;
				iod.iod_nr    = 1;
				iod.iod_recxs = NULL;

				/* set sgl values */
				sgl.sg_nr     = 1;
				sgl.sg_iovs   = &iov;

				d_iov_set(&iov, single_data, single_tsize);	
				/* update fetched recx values and place in destination object */
                    		rc = daos_obj_update(*oh, DAOS_TX_NONE, 0, &diov, 1, &iod,
					&sgl, NULL);
				if (rc != 0) {
					fprintf(stderr, "failed to update object: %d\n", rc);
					D_GOTO(out, rc);
				}

			}
		 	H5Pclose(hdf5->plist);	
			H5Tclose(hdf5->rx_dtype);	
			H5Sclose(hdf5->rx_dspace);	
			H5Dclose(hdf5->rx_dset);	
		}
	}
out:
	D_FREE(single_data);
	return rc;
}

int
cont_deserialize_hdlr(struct cmd_args_s *ap)
{
	int			rc = 0;
	int			i = 0;
	daos_cont_info_t	cont_info;
	struct			hdf5_args hdf5;
	daos_obj_id_t		oid;
	daos_handle_t		oh;
	uint64_t		dk_off = 0;
	uint64_t		dk_next = 0;
	uint64_t		total_dkeys_this_oid = 0;

	/* init HDF5 args */
	init_hdf5_args(&hdf5);

	printf("\tpool UUID: "DF_UUIDF"\n", DP_UUID(ap->p_uuid));
	printf("\tcont UUID: "DF_UUIDF"\n", DP_UUID(ap->c_uuid));
	printf("\th5filename: %s\n", ap->h5filename);

 	/* check if this is a POSIX container,
	 * if it is, then the new container will
	 * need to be created with DFS
	 */	
	/* TODO: remove requiring of --type=POSIX for
	 * POSIX containers, and instead write all 
	 * container attributes to HDF5 file. That way,
	 * I can read the cont type when it was serialized
	 * instead.
	 */
	if (ap->type == DAOS_PROP_CO_LAYOUT_POSIX) {
		dfs_attr_t attr;
		attr.da_id = 0;
		attr.da_oclass_id = ap->oclass;
		attr.da_chunk_size = ap->chunk_size;
		attr.da_props = ap->props;
		rc = dfs_cont_create(ap->pool, ap->c_uuid, &attr, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to create posix "
			"container: %d\n", rc);
			D_GOTO(out, rc);
		}
	} else {
		rc = daos_cont_create(ap->pool, ap->c_uuid, ap->props, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to create container: %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	/* print out created cont uuid */
	fprintf(stdout, "Successfully created container "DF_UUIDF"\n",
		DP_UUID(ap->c_uuid));
	rc = daos_cont_open(ap->pool, ap->c_uuid, DAOS_COO_RW, &ap->cont,
			    &cont_info, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to open container: %d\n", rc);
		D_GOTO(out, rc);
	}

	/* open passed in HDF5 file */
	hdf5.file = H5Fopen(ap->h5filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (hdf5.file < 0) {
		fprintf(stderr, "failed to open HDF5 file\n");
		D_GOTO(out, rc = 1);
	}

	rc = hdf5_read_key_data(&hdf5);
	if (rc != 0) {
		fprintf(stderr, "failed to read hdf5 key data: %d\n", rc);
		D_GOTO(out, rc);
	}
	for(i = 0; i < (int)hdf5.oid_dims[0]; i++) {
		oid.lo = hdf5.oid_data[i].oid_low;
		oid.hi = hdf5.oid_data[i].oid_hi;
		printf("oid_data[i].oid_low: %lu\n", hdf5.oid_data[i].oid_low);
		printf("oid_data[i].oid_hi: %lu\n", hdf5.oid_data[i].oid_hi);
		rc = daos_obj_open(ap->cont, oid, 0, &oh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to open object: %d\n", rc);
			D_GOTO(out, rc);
		}
		dk_off = hdf5.oid_data[i].dkey_offset;
		dk_next = 0;
		total_dkeys_this_oid = 0;
		if (i + 1 < (int)hdf5.oid_dims[0]) {
			dk_next = hdf5.oid_data[i + 1].dkey_offset;
			total_dkeys_this_oid = dk_next - dk_off;
		} else if (i == ((int)hdf5.oid_dims[0] - 1)){
			printf("LAST OID: i: %d\n", i);
			total_dkeys_this_oid = (int)hdf5.dkey_dims[0] - (dk_off);
		} 
		printf("\nTOTAL DK THIS OID: %lu\n", total_dkeys_this_oid);
		rc = cont_deserialize_keys(&hdf5, &total_dkeys_this_oid, &dk_off,
					   &oh);
		if (rc != 0) {
			fprintf(stderr, "failed to deserialize keys: %d\n", rc);
			D_GOTO(out, rc);
		}
		rc = daos_obj_close(oh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to close object: %d\n", rc);
			D_GOTO(out, rc);
		}
	}
out:
	H5Dclose(hdf5.oid_dset);
	H5Dclose(hdf5.dkey_dset);
	H5Dclose(hdf5.akey_dset);
	H5Sclose(hdf5.oid_dspace);
	H5Sclose(hdf5.dkey_dspace);
	H5Sclose(hdf5.akey_dspace);
	H5Tclose(hdf5.oid_dtype);
	H5Tclose(hdf5.dkey_vtype);
	H5Tclose(hdf5.akey_vtype);
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
	struct stat	sb;
	FILE		*outstream = stdout;

	if (ap->outfile) {
		if (!ap->force && (stat(ap->outfile, &sb) == 0)) {
			fprintf(stderr,
				"Unable to create output file: File already "
				"exists\n");
			return -DER_EXIST;
		}

		outstream = fopen(ap->outfile, "w");
		if (outstream == NULL) {
			fprintf(stderr, "Unable to create output file: %s\n",
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
