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
#define LIBSERIALIZE		"libdaos_serialize.so"
#define NUM_SERIALIZE_PROPS	15

#include <stdio.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
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
#include "daos_fs_sys.h"

#include "daos_hdlr.h"

#define OID_ARR_SIZE 8

struct file_dfs {
	enum {POSIX, DAOS} type;
	int fd;
	daos_off_t offset;
	dfs_obj_t *obj;
	dfs_sys_t *dfs_sys;
};

struct dm_args {
	char		*src;
	char		*dst;
	char		src_pool[DAOS_PROP_LABEL_MAX_LEN + 1];
	char		src_cont[DAOS_PROP_LABEL_MAX_LEN + 1];
	char		dst_pool[DAOS_PROP_LABEL_MAX_LEN + 1];
	char		dst_cont[DAOS_PROP_LABEL_MAX_LEN + 1];
	daos_handle_t	src_poh;
	daos_handle_t	src_coh;
	daos_handle_t	dst_poh;
	daos_handle_t	dst_coh;
	uint32_t	cont_prop_oid;
	uint32_t	cont_prop_layout;
	uint64_t	cont_layout;
	uint64_t	cont_oid;

};

/* Report an error with a system error number using a standard output format */
#define DH_PERROR_SYS(AP, RC, STR, ...)					\
	fprintf((AP)->errstream, STR ": %s (%d)\n", ## __VA_ARGS__, strerror(RC), (RC))

/* Report an error with a daos error number using a standard output format */
#define DH_PERROR_DER(AP, RC, STR, ...)					\
	fprintf((AP)->errstream, STR ": %s (%d)\n", ## __VA_ARGS__, d_errdesc(RC), (RC))

static int
parse_acl_file(struct cmd_args_s *ap, const char *path, struct daos_acl **acl);

/* TODO: implement these pool op functions
 * int pool_stat_hdlr(struct cmd_args_s *ap);
 */

static int
pool_decode_props(struct cmd_args_s *ap, daos_prop_t *props)
{
	struct daos_prop_entry		*entry;
	int				rc = 0;

	/* unset properties should get default value */

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_LABEL);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(ap->errstream, "label property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("label:\t\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_SPACE_RB);
	if (entry == NULL) {
		fprintf(ap->errstream,
			"rebuild space ratio property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("rebuild space ratio:\t"DF_U64"%%\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_SELF_HEAL);
	if (entry == NULL) {
		fprintf(ap->errstream, "self-healing property not found\n");
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
			D_PRINT("unknown bits set in self-healing property ("
				DF_X64")\n", entry->dpe_val);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_RECLAIM);
	if (entry == NULL) {
		fprintf(ap->errstream, "reclaim property not found\n");
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

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_EC_CELL_SZ);
	if (entry == NULL) {
		fprintf(ap->errstream, "EC cell size not found\n");
		rc = -DER_INVAL;
	} else {
		if (!daos_ec_cs_valid(entry->dpe_val)) {
			D_PRINT("Invalid EC cell size: %u\n",
				(uint32_t)entry->dpe_val);
		} else {
			D_PRINT("EC cell size = %u\n",
				(uint32_t)entry->dpe_val);
		}
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(ap->errstream, "owner property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("owner:\t\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(ap->errstream, "owner-group property not found\n");
		rc = -DER_INVAL;
	} else {
		D_PRINT("owner-group:\t\t%s\n", entry->dpe_str);
	}

	entry = daos_prop_entry_get(props, DAOS_PROP_PO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL) {
		fprintf(ap->errstream, "acl property not found\n");
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

	rc = daos_pool_connect(ap->p_uuid, ap->sysname, DAOS_PC_RO, &ap->pool, NULL, NULL);
	if (rc != 0) {
		fprintf(ap->errstream, "failed to connect to pool "DF_UUIDF": %s (%d)\n",
			DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	prop_query = daos_prop_alloc(0);
	if (prop_query == NULL)
		D_GOTO(out_disconnect, rc = -DER_NOMEM);

	rc = daos_pool_query(ap->pool, NULL, NULL, prop_query, NULL);
	if (rc != 0) {
		fprintf(ap->errstream, "failed to query properties for pool "DF_UUIDF": %s (%d)\n",
			DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	D_PRINT("Pool properties for "DF_UUIDF" :\n", DP_UUID(ap->p_uuid));

	rc = pool_decode_props(ap, prop_query);

out_disconnect:
	daos_prop_free(prop_query);

	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to disconnect from pool "DF_UUIDF
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
		fprintf(ap->errstream,
			"both attribute name and value must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RW, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(ap->errstream, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	value_size = strlen(ap->value_str);
	rc = daos_pool_set_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str,
				(const void * const*)&ap->value_str,
				(const size_t *)&value_size, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to set attribute '%s' for pool "DF_UUIDF
			": %s (%d)\n", ap->attrname_str, DP_UUID(ap->p_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to disconnect from pool "DF_UUIDF
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
		fprintf(ap->errstream, "attribute name must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RW, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(ap->errstream, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	rc = daos_pool_del_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to delete attribute '%s' for pool "
			DF_UUIDF": %s (%d)\n", ap->attrname_str,
			DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to disconnect from pool "DF_UUIDF
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
		fprintf(ap->errstream, "attribute name must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(ap->errstream, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* evaluate required size to get attr */
	attr_size = 0;
	rc = daos_pool_get_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str, NULL,
				&attr_size, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to retrieve size of attribute '%s' for "
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
		fprintf(ap->errstream,
			"failed to get attribute '%s' for pool "DF_UUIDF
			": %s (%d)\n", ap->attrname_str, DP_UUID(ap->p_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	if (expected_size < attr_size)
		fprintf(ap->errstream,
			"size required to get attributes has raised, "
			"value has been truncated\n");
	D_PRINT("%s\n", buf);

out_disconnect:
	D_FREE(buf);

	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to disconnect from pool "DF_UUIDF
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
		fprintf(ap->errstream, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* evaluate required size to get all attrs */
	total_size = 0;
	rc = daos_pool_list_attr(ap->pool, NULL, &total_size, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to list attribute for pool "DF_UUIDF
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
		fprintf(ap->errstream,
			"failed to list attribute for pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	if (expected_size < total_size)
		fprintf(ap->errstream,
			"size required to gather all attributes has raised,"
			" list has been truncated\n");
	while (cur < total_size) {
		len = strnlen(buf + cur, total_size - cur);
		if (len == total_size - cur) {
			fprintf(ap->errstream,
				"end of buf reached but no end of string"
				" encountered, ignoring\n");
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
		fprintf(ap->errstream, "failed to disconnect from pool "DF_UUIDF
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
		fprintf(ap->errstream, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* Issue first API call to get current number of containers */
	rc = daos_pool_list_cont(ap->pool, &ncont, NULL /* cbuf */,
				 NULL /* ev */);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to retrieve number of containers for "
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
		fprintf(ap->errstream, "failed to allocate memory for "
			"pool "DF_UUIDF": %s (%d)\n", DP_UUID(ap->p_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out_disconnect, 0);
	}

	rc = daos_pool_list_cont(ap->pool, &ncont, conts, NULL /* ev */);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to list containers for pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out_free, rc);
	}

	for (i = 0; i < ncont; i++) {
		D_PRINT(DF_UUIDF" %s\n", DP_UUID(conts[i].pci_uuid),
			conts[i].pci_label);
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
		fprintf(ap->errstream, "failed to disconnect from pool "DF_UUIDF
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
		fprintf(ap->errstream, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	pinfo.pi_bits = DPI_ALL;
	rc = daos_pool_query(ap->pool, NULL, &pinfo, NULL, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to query pool "DF_UUIDF": %s (%d)\n",
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
		fprintf(ap->errstream, "failed to disconnect from pool "DF_UUIDF
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
	time_t			begin;
	time_t			end;
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
		fprintf(ap->errstream,
			"open of container's OIT failed: "DF_RC"\n",
			DP_RC(rc));
		goto out_snap;
	}

	begin = time(NULL);

	fprintf(ap->outstream, "check container %s started at: %s\n", ap->cont_str, ctime(&begin));

	while (!daos_anchor_is_eof(&anchor)) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(oit, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			fprintf(ap->errstream,
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
				fprintf(ap->errstream,
					"found data inconsistency for object: "
					DF_OID"\n", DP_OID(oids[i]));
				inconsistent++;
				continue;
			}

			if (rc < 0) {
				fprintf(ap->errstream,
					"check object "DF_OID" failed: "
					DF_RC"\n", DP_OID(oids[i]), DP_RC(rc));
				D_GOTO(out_close, rc);
			}
		}
	}

	end = time(NULL);
	duration = end - begin;
	if (duration == 0)
		duration = 1;

	if (rc == 0 || rc == -DER_NOSYS || rc == -DER_MISMATCH) {
		fprintf(ap->outstream,
			"check container %s completed at: %s\n"
			"checked: %lu\n"
			"skipped: %lu\n"
			"inconsistent: %lu\n"
			"run_time: %lu seconds\n"
			"scan_speed: %lu objs/sec\n",
			ap->cont_str, ctime(&end), checked, skipped,
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
cont_list_snaps_hdlr(struct cmd_args_s *ap)
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
	rc = daos_cont_list_snap(ap->cont, &snaps_count, NULL, NULL, &anchor, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to retrieve number of snapshots for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	if (ap->snapname_str == NULL)
		D_PRINT("Container's snapshots :\n");

	if (!daos_anchor_is_eof(&anchor)) {
		fprintf(ap->errstream, "too many snapshots returned\n");
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
		fprintf(ap->errstream, "failed to list snapshots for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}
	if (expected_count < snaps_count)
		fprintf(ap->errstream,
			"snapshot list has been truncated (size changed)\n");

	if (ap->snapname_str == NULL && ap->epc == 0) {
		for (i = 0; i < min(expected_count, snaps_count); i++)
			D_PRINT("0x"DF_X64" %s\n", epochs[i], names[i]);
	} else {
		for (i = 0; i < min(expected_count, snaps_count); i++)
			if (ap->snapname_str != NULL &&
			    strcmp(ap->snapname_str, names[i]) == 0) {
				ap->epc = epochs[i];
				break;
			} else if (ap->epc == epochs[i]) {
				break;
			}
		if (i == min(expected_count, snaps_count)) {
			if (ap->snapname_str != NULL)
				fprintf(ap->errstream,
					"%s not found in snapshots list\n",
				ap->snapname_str);
			else
				fprintf(ap->errstream, "0x"DF_X64" not found in snapshots list\n",
					ap->epc);
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
		fprintf(ap->errstream,
			"failed to create snapshot for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	fprintf(ap->outstream, "snapshot/epoch 0x"DF_X64" has been created\n", ap->epc);
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
		fprintf(ap->errstream,
			"a single epoch or a range must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (ap->epc != 0 &&
	    (ap->epcrange_begin != 0 || ap->epcrange_end != 0)) {
		fprintf(ap->errstream,
			"both a single epoch and a range not allowed\n");
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
		fprintf(ap->errstream,
			"failed to destroy snapshots for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
cont_set_prop_hdlr(struct cmd_args_s *ap)
{
	int			 rc;
	struct daos_prop_entry	*entry;
	uint32_t		 i;

	if (ap->props == NULL || ap->props->dpp_nr == 0) {
		fprintf(ap->errstream,
			"at least one property must be requested\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}

	/* Validate the properties are supported for set */
	for (i = 0; i < ap->props->dpp_nr; i++) {
		entry = &ap->props->dpp_entries[i];
		if (entry->dpe_type != DAOS_PROP_CO_LABEL &&
		    entry->dpe_type != DAOS_PROP_CO_STATUS) {
			fprintf(ap->errstream,
				"property not supported for set\n");
			D_GOTO(err_out, rc = -DER_INVAL);
		}
	}

	rc = daos_cont_set_prop(ap->cont, ap->props, NULL);
	if (rc) {
		fprintf(ap->errstream, "failed to set properties for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
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
	if (ap->type != DAOS_PROP_CO_LAYOUT_POSIX &&
	    ap->type != DAOS_PROP_CO_LAYOUT_UNKNOWN)
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
			fprintf(ap->errstream,
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
		fprintf(ap->errstream,
			"too many properties supplied. Try again with "
			"fewer props set.\n");
		return -DER_INVAL;
	}

	return 0;
}

static int
update_props_for_create(struct cmd_args_s *ap)
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
		rc = parse_acl_file(ap, ap->aclfile, &acl);
		if (rc != 0)
			return rc;

		entry->dpe_type = DAOS_PROP_CO_ACL;
		entry->dpe_val_ptr = acl;
		acl = NULL; /* acl will be freed with the prop now */

		entry++;
	}

	if (ap->user) {
		if (!daos_acl_principal_is_valid(ap->user)) {
			fprintf(ap->errstream,
				"invalid user name.\n");
			return -DER_INVAL;
		}

		entry->dpe_type = DAOS_PROP_CO_OWNER;
		D_STRNDUP(entry->dpe_str, ap->user, DAOS_ACL_MAX_PRINCIPAL_LEN);
		if (entry->dpe_str == NULL) {
			fprintf(ap->errstream,
				"failed to allocate memory for user name.\n");
			return -DER_NOMEM;
		}

		entry++;
	}

	if (ap->group) {
		if (!daos_acl_principal_is_valid(ap->group)) {
			fprintf(ap->errstream,
				"invalid group name.\n");
			return -DER_INVAL;
		}

		entry->dpe_type = DAOS_PROP_CO_OWNER_GROUP;
		D_STRNDUP(entry->dpe_str, ap->group,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);
		if (entry->dpe_str == NULL) {
			fprintf(ap->errstream,
				"failed to allocate memory for group name.\n");
			return -DER_NOMEM;
		}

		entry++;
	}

	if (ap->type != DAOS_PROP_CO_LAYOUT_POSIX &&
	    ap->type != DAOS_PROP_CO_LAYOUT_UNKNOWN) {
		entry->dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
		entry->dpe_val = ap->type;

		entry++;
	}

	return 0;
}

static void
cmd_args_print(struct cmd_args_s *ap)
{
	char	oclass[10] = {}, type[10] = {};

	if (ap == NULL)
		return;

	daos_oclass_id2name(ap->oclass, oclass);
	daos_unparse_ctype(ap->type, type);

	D_INFO("\tDAOS system name: %s\n", ap->sysname);
	D_INFO("\tpool UUID: "DF_UUIDF"\n", DP_UUID(ap->p_uuid));
	D_INFO("\tcont UUID: "DF_UUIDF"\n", DP_UUID(ap->c_uuid));

	D_INFO("\tattr: name=%s, value=%s\n",
		ap->attrname_str ? ap->attrname_str : "NULL",
		ap->value_str ? ap->value_str : "NULL");

	D_INFO("\tpath=%s, type=%s, oclass=%s, chunk_size="DF_U64"\n",
		ap->path ? ap->path : "NULL",
		type, oclass, ap->chunk_size);
	D_INFO("\tsnapshot: name=%s, epoch=0x"DF_X64", epoch range=%s "
		"(0x"DF_X64"-0x"DF_X64")\n",
		ap->snapname_str ? ap->snapname_str : "NULL",
		ap->epc,
		ap->epcrange_str ? ap->epcrange_str : "NULL",
		ap->epcrange_begin, ap->epcrange_end);
	D_INFO("\toid: "DF_OID"\n", DP_OID(ap->oid));
}

/* cont_create_hdlr() - create container by UUID */
int
cont_create_hdlr(struct cmd_args_s *ap)
{
	int rc;

	rc = update_props_for_create(ap);
	if (rc != 0)
		return rc;

	cmd_args_print(ap);

	if (ap->type == DAOS_PROP_CO_LAYOUT_POSIX) {
		dfs_attr_t attr;

		attr.da_id = 0;
		attr.da_oclass_id = ap->oclass;
		attr.da_chunk_size = ap->chunk_size;
		attr.da_props = ap->props;
		attr.da_mode = ap->mode;

		if (uuid_is_null(ap->c_uuid))
			rc = dfs_cont_create(ap->pool, &ap->c_uuid, &attr, NULL, NULL);
		else
			rc = dfs_cont_create(ap->pool, ap->c_uuid, &attr, NULL, NULL);
		if (rc)
			rc = daos_errno2der(rc);
	} else {
		if (uuid_is_null(ap->c_uuid))
			rc = daos_cont_create(ap->pool, &ap->c_uuid, ap->props, NULL);
		else
			rc = daos_cont_create(ap->pool, ap->c_uuid, ap->props, NULL);
	}

	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to create container");
		return rc;
	}

	fprintf(ap->outstream, "Successfully created container "DF_UUIDF"\n", DP_UUID(ap->c_uuid));

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

	/* Required: pool handle, container type, obj class, chunk_size.
	 * Optional: uuid of pool handle, user-specified container UUID.
	 */
	ARGS_VERIFY_PATH_CREATE(ap, err_rc, rc = -DER_INVAL);

	rc = update_props_for_create(ap);
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
		DH_PERROR_SYS(ap, rc, "duns_create_path() failed");
		D_GOTO(err_rc, rc = daos_errno2der(rc));
	}

	snprintf(ap->cont_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_cont);
	if (uuid_is_null(ap->c_uuid))
		uuid_parse(ap->cont_str, ap->c_uuid);
	daos_unparse_ctype(ap->type, type);
	fprintf(ap->outstream, "Successfully created container %s type %s\n", ap->cont_str, type);

	return 0;

err_rc:
	return rc;
}

int
parse_filename_dfs(const char *path, char **_obj_name, char **_cont_name)
{
	char	*f1 = NULL;
	char	*f2 = NULL;
	char	*fname = NULL;
	char	*cont_name = NULL;
	int	path_len;
	int	rc = 0;

	if (path == NULL || _obj_name == NULL || _cont_name == NULL)
		return -DER_INVAL;
	path_len = strlen(path) + 1;

	if (strcmp(path, "/") == 0) {
		D_STRNDUP_S(*_cont_name, "/");
		if (*_cont_name == NULL)
			return -DER_NOMEM;
		*_obj_name = NULL;
		return 0;
	}
	D_STRNDUP(f1, path, path_len);
	if (f1 == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_STRNDUP(f2, path, path_len);
	if (f2 == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	fname = basename(f1);
	cont_name = dirname(f2);

	if (cont_name[0] != '/') {
		char cwd[1024];

		if (getcwd(cwd, 1024) == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		if (strcmp(cont_name, ".") == 0) {
			D_STRNDUP(cont_name, cwd, 1024);
			if (cont_name == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		} else {
			char *new_dir = calloc(strlen(cwd) + strlen(cont_name)
						+ 1, sizeof(char));

			if (new_dir == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

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
		if (*_cont_name == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}
	D_STRNDUP(*_obj_name, fname, strlen(fname) + 1);
	if (*_obj_name == NULL) {
		D_FREE(*_cont_name);
		D_GOTO(out, rc = -DER_NOMEM);
	}
out:
	D_FREE(f1);
	D_FREE(f2);
	return rc;
}

static int
file_write(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	   const char *file, void *buf, ssize_t *size)
{
	int rc = 0;
	/* posix write returns -1 on error so wrapper uses ssize_t, but
	 * dfs_sys_read takes daos_size_t for size argument
	 */
	daos_size_t tmp_size = *size;

	if (file_dfs->type == POSIX) {
		*size = write(file_dfs->fd, buf, *size);
		if (*size < 0)
			rc = errno;
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_write(file_dfs->dfs_sys, file_dfs->obj, buf, file_dfs->offset,
				   &tmp_size, NULL);
		*size = tmp_size;
		if (rc == 0)
			/* update file pointer with number of bytes written */
			file_dfs->offset += *size;
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", file, file_dfs->type);
	}
	return rc;
}

int
file_open(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	  const char *file, int flags, ...)
{
	/* extract the mode */
	int	rc = 0;
	int	mode_set = 0;
	mode_t	mode = 0;

	if (flags & O_CREAT) {
		va_list vap;

		va_start(vap, flags);
		mode = va_arg(vap, mode_t);
		va_end(vap);
		mode_set = 1;
	}

	if (file_dfs->type == POSIX) {
		if (mode_set) {
			file_dfs->fd = open(file, flags, mode);
		} else {
			file_dfs->fd = open(file, flags);
		}
		if (file_dfs->fd < 0) {
			rc = errno;
			DH_PERROR_SYS(ap, rc, "file_open failed on '%s'", file);
		}
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_open(file_dfs->dfs_sys, file, mode, flags, 0, 0, NULL,
				  &file_dfs->obj);
		if (rc != 0) {
			DH_PERROR_SYS(ap, rc, "file_open failed on '%s'", file);
		}
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", file, file_dfs->type);
	}
	return rc;
}

static int
file_mkdir(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	   const char *dir, mode_t *mode)
{
	int rc = 0;

	/* continue if directory already exists */
	if (file_dfs->type == POSIX) {
		rc = mkdir(dir, *mode);
		if (rc != 0) {
			/* return error code for POSIX mkdir */
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_mkdir(file_dfs->dfs_sys, dir, *mode, 0);
		if (rc != 0) {
			D_GOTO(out, rc);
		}
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", dir, file_dfs->type);
	}
out:
	return rc;
}

static int
file_opendir(struct cmd_args_s *ap, struct file_dfs *file_dfs, const char *dir, DIR **_dirp)
{
	DIR *dirp = NULL;
	int rc	  = 0;

	if (file_dfs->type == POSIX) {
		dirp = opendir(dir);
		if (dirp == NULL)
			rc = errno;
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_opendir(file_dfs->dfs_sys, dir, 0, &dirp);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", dir, file_dfs->type);
	}
	*_dirp = dirp;
	return rc;
}

static int
file_readdir(struct cmd_args_s *ap, struct file_dfs *file_dfs, DIR *dirp, struct dirent **_entry)
{
	struct dirent *entry = NULL;
	int rc		     = 0;

	if (file_dfs->type == POSIX) {
		do {
			/* errno set to zero before calling readdir to distinguish error from
			 * end of stream per readdir documentation
			 */
			errno = 0;
			entry = readdir(dirp);
			if (entry == NULL)
				rc = errno;
		} while (errno == EAGAIN);
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_readdir(file_dfs->dfs_sys, dirp, &entry);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known type=%d", file_dfs->type);
	}
	*_entry = entry;
	return rc;
}

static int
file_lstat(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	   const char *path, struct stat *buf)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = lstat(path, buf);
		/* POSIX returns -1 on error and sets errno
		 * to the error code
		 */
		if (rc != 0)
			rc = errno;
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_stat(file_dfs->dfs_sys, path, O_NOFOLLOW, buf);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", path, file_dfs->type);
	}
	return rc;
}

static int
file_read(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	  const char *file, void *buf, ssize_t *size)
{
	int rc = 0;
	/* posix read returns -1 on error so wrapper uses ssize_t, but
	 * dfs_sys_read takes daos_size_t for size argument
	 */
	daos_size_t tmp_size = *size;

	if (file_dfs->type == POSIX) {
		*size = read(file_dfs->fd, buf, *size);
		if (*size < 0)
			rc = errno;
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_read(file_dfs->dfs_sys, file_dfs->obj, buf, file_dfs->offset,
				  &tmp_size, NULL);
		*size = tmp_size;
		if (rc == 0)
			/* update file pointer with number of bytes read */
			file_dfs->offset += (daos_off_t)*size;
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", file, file_dfs->type);
	}
	return rc;
}

static int
file_closedir(struct cmd_args_s *ap, struct file_dfs *file_dfs, DIR *dirp)
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
		rc = dfs_sys_closedir(dirp);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known type=%d", file_dfs->type);
	}
	return rc;
}

static int
file_close(struct cmd_args_s *ap, struct file_dfs *file_dfs, const char *file)
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
		rc = dfs_sys_close(file_dfs->obj);
		if (rc == 0)
			file_dfs->obj = NULL;
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", file, file_dfs->type);
	}
	return rc;
}

static int
file_chmod(struct cmd_args_s *ap, struct file_dfs *file_dfs, const char *path,
	   mode_t mode)
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
		rc = dfs_sys_chmod(file_dfs->dfs_sys, path, mode);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", path, file_dfs->type);
	}
	return rc;
}

static int
fs_copy_file(struct cmd_args_s *ap,
	     struct file_dfs *src_file_dfs,
	     struct file_dfs *dst_file_dfs,
	     struct stat *src_stat,
	     const char *src_path,
	     const char *dst_path)
{
	int src_flags		= O_RDONLY;
	int dst_flags		= O_CREAT | O_WRONLY;
	mode_t tmp_mode_file	= S_IRUSR | S_IWUSR;
	int rc;
	uint64_t file_length	= src_stat->st_size;
	uint64_t total_bytes	= 0;
	uint64_t buf_size	= 64 * 1024 * 1024;
	void *buf		= NULL;

	/* Open source file */
	rc = file_open(ap, src_file_dfs, src_path, src_flags);
	if (rc != 0)
		D_GOTO(out, rc = daos_errno2der(rc));

	/* Open destination file */
	rc = file_open(ap, dst_file_dfs, dst_path, dst_flags, tmp_mode_file);
	if (rc != 0)
		D_GOTO(out_src_file, rc = daos_errno2der(rc));

	/* Allocate read/write buffer */
	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		D_GOTO(out_dst_file, rc = -DER_NOMEM);

	/* read from source file, then write to dest file */
	while (total_bytes < file_length) {
		ssize_t left_to_read = buf_size;
		uint64_t bytes_left = file_length - total_bytes;

		if (bytes_left < buf_size)
			left_to_read = (size_t)bytes_left;
		rc = file_read(ap, src_file_dfs, src_path, buf, &left_to_read);
		if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc, "File read failed");
			D_GOTO(out_buf, rc);
		}
		ssize_t bytes_to_write = left_to_read;

		rc = file_write(ap, dst_file_dfs, dst_path, buf, &bytes_to_write);
		if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc, "File write failed");
			D_GOTO(out_buf, rc);
		}
		total_bytes += left_to_read;
	}

	/* set perms on destination to original source perms */
	rc = file_chmod(ap, dst_file_dfs, dst_path, src_stat->st_mode);
	if (rc != 0) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "updating dst file permissions failed");
		D_GOTO(out_buf, rc);
	}

out_buf:
	D_FREE(buf);
out_dst_file:
	file_close(ap, dst_file_dfs, dst_path);
out_src_file:
	file_close(ap, src_file_dfs, src_path);
out:
	/* reset offsets if there is another file to copy */
	src_file_dfs->offset = 0;
	dst_file_dfs->offset = 0;
	return rc;
}

static int
fs_copy_dir(struct cmd_args_s *ap,
	    struct file_dfs *src_file_dfs,
	    struct file_dfs *dst_file_dfs,
	    struct stat *src_stat,
	    const char *src_path,
	    const char *dst_path,
	    uint64_t *num_dirs,
	    uint64_t *num_files)
{
	DIR			*src_dir = NULL;
	struct dirent		*entry = NULL;
	char			*next_src_path = NULL;
	char			*next_dst_path = NULL;
	struct stat		next_src_stat;
	mode_t			tmp_mode_dir = S_IRWXU;
	int			rc = 0;

	/* begin by opening source directory */
	rc = file_opendir(ap, src_file_dfs, src_path, &src_dir);
	if (rc != 0) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "Cannot open directory '%s'", src_path);
		D_GOTO(out, rc);
	}

	/* create the destination directory if it does not exist */
	rc = file_mkdir(ap, dst_file_dfs, dst_path, &tmp_mode_dir);
	if (rc != EEXIST && rc != 0)
		D_GOTO(out, rc = daos_errno2der(rc));

	/* copy all directory entries */
	while (1) {
		const char *d_name;

		/* walk source directory */
		rc = file_readdir(ap, src_file_dfs, src_dir, &entry);
		if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc, "Cannot read directory");
			D_GOTO(out, rc);
		}

		/* end of stream when entry is NULL and rc == 0 */
		if (!entry) {
			/* There are no more entries in this directory,
			 * so break out of the while loop.
			 */
			break;
		}

		/* Check that the entry is not "src_path"
		 * or src_path's parent.
		 */
		d_name = entry->d_name;
		if ((strcmp(d_name, "..") == 0) ||
		    (strcmp(d_name, ".")) == 0)
			continue;

		/* build the next source path */
		D_ASPRINTF(next_src_path, "%s/%s", src_path, d_name);
		if (next_src_path == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		/* stat the next source path */
		rc = file_lstat(ap, src_file_dfs, next_src_path, &next_src_stat);
		if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc, "Cannot stat path '%s'", next_src_path);
			D_GOTO(out, rc);
		}

		/* build the next destination path */
		D_ASPRINTF(next_dst_path, "%s/%s", dst_path, d_name);
		if (next_dst_path == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		switch (next_src_stat.st_mode & S_IFMT) {
		case S_IFREG:
			rc = fs_copy_file(ap, src_file_dfs, dst_file_dfs,
					  &next_src_stat, next_src_path,
					  next_dst_path);
			if (rc != 0)
				D_GOTO(out, rc);
			(*num_files)++;
			break;
		case S_IFDIR:
			rc = fs_copy_dir(ap, src_file_dfs, dst_file_dfs,
					 &next_src_stat, next_src_path,
					 next_dst_path, num_dirs, num_files);
			if (rc != 0)
				D_GOTO(out, rc);
			(*num_dirs)++;
			break;
		default:
			rc = -DER_INVAL;
			DH_PERROR_DER(ap, rc, "Only files and directories are supported");
		}
		D_FREE(next_src_path);
		D_FREE(next_dst_path);
	}

	/* set original source perms on directories after copying */
	rc = file_chmod(ap, dst_file_dfs, dst_path, src_stat->st_mode);
	if (rc != 0) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "updating destination permissions failed on '%s'", dst_path);
		D_GOTO(out, rc);
	}
out:
	if (rc != 0) {
		D_FREE(next_src_path);
		D_FREE(next_dst_path);
	}

	if (src_dir != NULL) {
		int close_rc;

		close_rc = file_closedir(ap, src_file_dfs, src_dir);
		if (close_rc != 0) {
			close_rc = daos_errno2der(close_rc);
			DH_PERROR_DER(ap, close_rc, "Could not close '%s'", src_path);
			if (rc == 0)
				rc = close_rc;
		}
	}
	return rc;
}

static int
fs_copy(struct cmd_args_s *ap,
	struct file_dfs *src_file_dfs,
	struct file_dfs *dst_file_dfs,
	const char *src_path,
	const char *dst_path,
	uint64_t *num_dirs,
	uint64_t *num_files)
{
	int		rc = 0;
	struct stat	src_stat;
	struct stat	dst_stat;
	bool		copy_into_dst = false;
	char		*tmp_path = NULL;
	char		*tmp_dir = NULL;
	char		*tmp_name = NULL;

	/* Make sure the source exists. */
	rc = file_lstat(ap, src_file_dfs, src_path, &src_stat);
	if (rc != 0) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "Failed to stat '%s'", src_path);
		D_GOTO(out, rc);
	}

	/* If the destination exists and is a directory,
	 * copy INTO the directory instead of TO it.
	 */
	rc = file_lstat(ap, dst_file_dfs, dst_path, &dst_stat);
	if (rc == 0) {
		if (S_ISDIR(dst_stat.st_mode)) {
			copy_into_dst = true;
		} else if S_ISDIR(src_stat.st_mode) {
			rc = -DER_INVAL;
			DH_PERROR_DER(ap, rc, "Destination is not a directory");
			D_GOTO(out, rc);
		}
	}

	if (copy_into_dst) {
		/* Get the dirname and basename */
		rc = parse_filename_dfs(src_path, &tmp_name, &tmp_dir);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to parse path '%s'", src_path);
			D_GOTO(out, rc);
		}

		/* Build the destination path */
		if (tmp_name != NULL) {
			D_ASPRINTF(tmp_path, "%s/%s", dst_path, tmp_name);
			if (tmp_path == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			dst_path = tmp_path;
		}
	}

	switch (src_stat.st_mode & S_IFMT) {
	case S_IFREG:
		rc = fs_copy_file(ap, src_file_dfs, dst_file_dfs, &src_stat, src_path, dst_path);
		if (rc == 0)
			(*num_files)++;
		break;
	case S_IFDIR:
		rc = fs_copy_dir(ap, src_file_dfs, dst_file_dfs, &src_stat, src_path, dst_path,
				 num_dirs, num_files);
		if (rc == 0)
			(*num_dirs)++;
		break;
	default:
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Only files and directories are supported");
		D_GOTO(out, rc);
	}

out:
	if (copy_into_dst) {
		D_FREE(tmp_path);
		D_FREE(tmp_dir);
		D_FREE(tmp_name);
	}
	return rc;
}

static inline void
set_dm_args_default(struct dm_args *dm)
{
	dm->src = NULL;
	dm->dst = NULL;
	dm->src_poh = DAOS_HDL_INVAL;
	dm->src_coh = DAOS_HDL_INVAL;
	dm->dst_poh = DAOS_HDL_INVAL;
	dm->dst_coh = DAOS_HDL_INVAL;
	dm->cont_prop_oid = DAOS_PROP_CO_ALLOCED_OID;
	dm->cont_prop_layout = DAOS_PROP_CO_LAYOUT_TYPE;
	dm->cont_layout = DAOS_PROP_CO_LAYOUT_UNKNOWN;
	dm->cont_oid = 0;
}

/*
 * Free the user attribute buffers created by dm_cont_get_usr_attrs.
 */
void
dm_cont_free_usr_attrs(int n, char ***_names, void ***_buffers, size_t **_sizes)
{
	char	**names = *_names;
	void	**buffers = *_buffers;
	size_t	i;

	if (names != NULL) {
		for (i = 0; i < n; i++) {
			D_FREE(names[i]);
		}
		D_FREE(*_names);
	}
	if (buffers != NULL) {
		for (i = 0; i < n; i++) {
			D_FREE(buffers[i]);
		}
		D_FREE(*_buffers);
	}
	D_FREE(*_sizes);
}

/*
 * Get the user attributes for a container in a format similar
 * to what daos_cont_set_attr expects.
 * cont_free_usr_attrs should be called to free the allocations.
 */
int
dm_cont_get_usr_attrs(struct cmd_args_s *ap, daos_handle_t coh, int *_n, char ***_names,
		      void ***_buffers, size_t **_sizes)
{
	int		rc = 0;
	uint64_t	total_size = 0;
	uint64_t	cur_size = 0;
	uint64_t	num_attrs = 0;
	uint64_t	name_len = 0;
	char		*name_buf = NULL;
	char		**names = NULL;
	void		**buffers = NULL;
	size_t		*sizes = NULL;
	uint64_t	i;

	/* Get the total size needed to store all names */
	rc = daos_cont_list_attr(coh, NULL, &total_size, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed list user attributes");
		D_GOTO(out, rc);
	}

	/* no attributes found */
	if (total_size == 0) {
		*_n = 0;
		D_GOTO(out, rc);
	}

	/* Allocate a buffer to hold all attribute names */
	D_ALLOC(name_buf, total_size);
	if (name_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Get the attribute names */
	rc = daos_cont_list_attr(coh, name_buf, &total_size, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to list user attributes");
		D_GOTO(out, rc);
	}

	/* Figure out the number of attributes */
	while (cur_size < total_size) {
		name_len = strnlen(name_buf + cur_size, total_size - cur_size);
		if (name_len == total_size - cur_size) {
			/* end of buf reached but no end of string, ignoring */
			break;
		}
		num_attrs++;
		cur_size += name_len + 1;
	}

	/* Sanity check */
	if (num_attrs == 0) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to parse user attributes");
		D_GOTO(out, rc);
	}

	/* Allocate arrays for attribute names, buffers, and sizes */
	D_ALLOC_ARRAY(names, num_attrs);
	if (names == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(sizes, num_attrs);
	if (sizes == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(buffers, num_attrs);
	if (buffers == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Create the array of names */
	cur_size = 0;
	for (i = 0; i < num_attrs; i++) {
		name_len = strnlen(name_buf + cur_size, total_size - cur_size);
		if (name_len == total_size - cur_size) {
			/* end of buf reached but no end of string, ignoring */
			break;
		}
		D_STRNDUP(names[i], name_buf + cur_size, name_len + 1);
		if (names[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		cur_size += name_len + 1;
	}

	/* Get the buffer sizes */
	rc = daos_cont_get_attr(coh, num_attrs, (const char * const*)names, NULL, sizes, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attribute sizes");
		D_GOTO(out, rc);
	}

	/* Allocate space for each value */
	for (i = 0; i < num_attrs; i++) {
		D_ALLOC(buffers[i], sizes[i] * sizeof(size_t));
		if (buffers[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	/* Get the attribute values */
	rc = daos_cont_get_attr(coh, num_attrs, (const char * const*)names,
				(void * const*)buffers, sizes, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attribute values");
		D_GOTO(out, rc);
	}

	/* Return values to the caller */
	*_n = num_attrs;
	*_names = names;
	*_buffers = buffers;
	*_sizes = sizes;
out:
	if (rc != 0)
		dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	D_FREE(name_buf);
	return rc;
}

/* Copy all user attributes from one container to another. */
int
dm_copy_usr_attrs(struct cmd_args_s *ap, daos_handle_t src_coh, daos_handle_t dst_coh)
{
	int	num_attrs = 0;
	char	**names = NULL;
	void	**buffers = NULL;
	size_t	*sizes = NULL;
	int	rc;

	/* Get all user attributes */
	rc = dm_cont_get_usr_attrs(ap, src_coh, &num_attrs, &names, &buffers, &sizes);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attributes");
		D_GOTO(out, rc);
	}

	/* no attributes to copy */
	if (num_attrs == 0)
		D_GOTO(out, rc = 0);

	rc = daos_cont_set_attr(dst_coh, num_attrs, (char const * const*) names,
				(void const * const*) buffers, sizes, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to set user attributes");
		D_GOTO(out, rc);
	}
out:
	dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	return rc;
}

/*
 * Get the container properties for a container in a format similar
 * to what daos_cont_set_prop expects.
 * The last entry is the ACL and is conditionally set only if
 * the user has permissions.
 */
int
dm_cont_get_all_props(struct cmd_args_s *ap, daos_handle_t coh, daos_prop_t **_props,
		      bool get_oid, bool get_label, bool get_roots)
{
	int		rc;
	daos_prop_t	*props = NULL;
	daos_prop_t	*prop_acl = NULL;
	daos_prop_t	*props_merged = NULL;
	uint32_t        total_props = NUM_SERIALIZE_PROPS;
	/* minimum number of properties that are always allocated/used to start count */
	int             prop_index = NUM_SERIALIZE_PROPS;

	if (get_oid)
		total_props++;

	/* container label is required to be unique, so do not retrieve it for copies.
	 * The label is retrieved for serialization, but only deserialized if the label
	 * no longer exists in the pool
	 */
	if (get_label)
		total_props++;

	if (get_roots)
		total_props++;

	/* Allocate space for all props except ACL. */
	props = daos_prop_alloc(total_props);
	if (props == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* The order of properties MUST match the order expected by serialization  */
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
	props->dpp_entries[2].dpe_type = DAOS_PROP_CO_LAYOUT_VER;
	props->dpp_entries[3].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[4].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	props->dpp_entries[5].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	props->dpp_entries[6].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	props->dpp_entries[7].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	props->dpp_entries[8].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	props->dpp_entries[9].dpe_type = DAOS_PROP_CO_COMPRESS;
	props->dpp_entries[10].dpe_type = DAOS_PROP_CO_ENCRYPT;
	props->dpp_entries[11].dpe_type = DAOS_PROP_CO_OWNER;
	props->dpp_entries[12].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
	props->dpp_entries[13].dpe_type = DAOS_PROP_CO_DEDUP;
	props->dpp_entries[14].dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;

	/* Conditionally get the OID. Should always be true for serialization. */
	if (get_oid) {
		props->dpp_entries[prop_index].dpe_type = DAOS_PROP_CO_ALLOCED_OID;
		prop_index++;
	}

	if (get_label) {
		props->dpp_entries[prop_index].dpe_type = DAOS_PROP_CO_LABEL;
		prop_index++;
	}

	if (get_roots) {
		props->dpp_entries[prop_index].dpe_type = DAOS_PROP_CO_ROOTS;
	}

	/* Get all props except ACL first. */
	rc = daos_cont_query(coh, NULL, props, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to query container");
		D_GOTO(out, rc);
	}

	/* Fetch the ACL separately in case user doesn't have access */
	rc = daos_cont_get_acl(coh, &prop_acl, NULL);
	if (rc == 0) {
		/* ACL will be appended to the end */
		props_merged = daos_prop_merge(props, prop_acl);
		if (props_merged == NULL) {
			rc = -DER_INVAL;
			DH_PERROR_DER(ap, rc, "Failed set container ACL");
			D_GOTO(out, rc);
		}
		daos_prop_free(props);
		props = props_merged;
	} else if (rc != -DER_NO_PERM) {
		DH_PERROR_DER(ap, rc, "Failed to query container ACL");
		D_GOTO(out, rc);
	}
	rc = 0;
	*_props = props;
out:
	daos_prop_free(prop_acl);
	if (rc != 0)
		daos_prop_free(props);
	return rc;
}

/* check if cont status is unhealthy */
static int
dm_check_cont_status(struct cmd_args_s *ap, daos_handle_t coh, bool *status_healthy)
{
	daos_prop_t		*prop;
	struct daos_prop_entry	*entry;
	struct daos_co_status	stat = {0};
	int			rc = 0;

	prop = daos_prop_alloc(1);
	if (prop == NULL)
		return -DER_NOMEM;

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_STATUS;

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		DH_PERROR_DER(ap, rc, "daos container query failed");
		D_GOTO(out, rc);
	}

	entry = &prop->dpp_entries[0];
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	if (stat.dcs_status == DAOS_PROP_CO_HEALTHY) {
		*status_healthy = true;
	} else {
		*status_healthy = false;
	}
out:
	daos_prop_free(prop);
	return rc;
}

static int
dm_serialize_cont_md(struct cmd_args_s *ap, struct dm_args *ca, daos_prop_t *props,
		     char *preserve_props)
{
	int	rc = 0;
	int	num_attrs = 0;
	char	**names = NULL;
	void	**buffers = NULL;
	size_t	*sizes = NULL;
	void	*handle;
	int (*daos_cont_serialize_md)(char *, daos_prop_t *props, int, char **, char **, size_t *);

	/* Get all user attributes if any exist */
	rc = dm_cont_get_usr_attrs(ap, ca->src_coh, &num_attrs, &names, &buffers, &sizes);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attributes");
		D_GOTO(out, rc);
	}
	handle = dlopen(LIBSERIALIZE, RTLD_NOW);
	if (handle == NULL) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "libdaos_serialize.so not found");
		D_GOTO(out, rc);
	}
	daos_cont_serialize_md = dlsym(handle, "daos_cont_serialize_md");
	if (daos_cont_serialize_md == NULL)  {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to lookup daos_cont_serialize_md");
		D_GOTO(out, rc);
	}
	(*daos_cont_serialize_md)(preserve_props, props, num_attrs, names, (char **)buffers, sizes);
out:
	if (num_attrs > 0) {
		dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	}
	return rc;
}

static int
dm_deserialize_cont_md(struct cmd_args_s *ap, struct dm_args *ca, char *preserve_props,
		       daos_prop_t **props)
{
	int		rc = 0;
	void		*handle;
	int (*daos_cont_deserialize_props)(daos_handle_t, char *, daos_prop_t **props, uint64_t *);

	handle = dlopen(LIBSERIALIZE, RTLD_NOW);
	if (handle == NULL) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "libdaos_serialize.so not found");
		D_GOTO(out, rc);
	}
	daos_cont_deserialize_props = dlsym(handle, "daos_cont_deserialize_props");
	if (daos_cont_deserialize_props == NULL)  {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to lookup daos_cont_deserialize_props");
		D_GOTO(out, rc);
	}
	(*daos_cont_deserialize_props)(ca->dst_poh, preserve_props, props, &ca->cont_layout);
out:
	return rc;
}

static int
dm_deserialize_cont_attrs(struct cmd_args_s *ap, struct dm_args *ca, char *preserve_props)
{
	int		rc = 0;
	uint64_t	num_attrs = 0;
	char		**names = NULL;
	void		**buffers = NULL;
	size_t		*sizes = NULL;
	void		*handle;
	int (*daos_cont_deserialize_attrs)(char *, uint64_t *, char ***, void ***, size_t **);

	handle = dlopen(LIBSERIALIZE, RTLD_NOW);
	if (handle == NULL) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "libdaos_serialize.so not found");
		D_GOTO(out, rc);
	}
	daos_cont_deserialize_attrs = dlsym(handle, "daos_cont_deserialize_attrs");
	if (daos_cont_deserialize_attrs == NULL)  {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to lookup daos_cont_deserialize_attrs");
		D_GOTO(out, rc);
	}
	(*daos_cont_deserialize_attrs)(preserve_props, &num_attrs, &names, &buffers, &sizes);
	if (num_attrs > 0) {
		rc = daos_cont_set_attr(ca->dst_coh, num_attrs, (const char * const*) names,
					(const void * const*) buffers, sizes, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to set user attributes");
			D_GOTO(out, rc);
		}
		dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	}
out:
	return rc;
}

static int
dm_connect(struct cmd_args_s *ap,
	   bool is_posix_copy,
	   struct file_dfs *src_file_dfs,
	   struct file_dfs *dst_file_dfs,
	   struct dm_args *ca,
	   char *sysname,
	   char *path,
	   daos_cont_info_t *src_cont_info,
	   daos_cont_info_t *dst_cont_info)
{
	/* check source pool/conts */
	int				rc = 0;
	struct duns_attr_t		dattr = {0};
	dfs_attr_t			attr = {0};
	daos_prop_t			*props = NULL;
	int				rc2;
	bool				status_healthy;

	/* open src pool, src cont, and mount dfs */
	if (src_file_dfs->type == DAOS) {
		rc = daos_pool_connect(ca->src_pool, sysname, DAOS_PC_RW, &ca->src_poh, NULL, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to connect to source pool");
			D_GOTO(out, rc);
		}
		rc = daos_cont_open(ca->src_poh, ca->src_cont, DAOS_COO_RW, &ca->src_coh,
				    src_cont_info, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to open source container\n");
			D_GOTO(err, rc);
		}
		if (is_posix_copy) {
			rc = dfs_sys_mount(ca->src_poh, ca->src_coh, O_RDWR,
					   DFS_SYS_NO_LOCK, &src_file_dfs->dfs_sys);
			if (rc != 0) {
				rc = daos_errno2der(rc);
				DH_PERROR_DER(ap, rc, "Failed to mount DFS filesystem on source");
				dfs_sys_umount(src_file_dfs->dfs_sys);
				D_GOTO(err, rc);
			}
		}

		/* do not copy a container that has unhealthy container status */
		rc = dm_check_cont_status(ap, ca->src_coh, &status_healthy);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to check container status");
			D_GOTO(err, rc);
		} else if (!status_healthy) {
			rc = -DER_INVAL;
			DH_PERROR_DER(ap, rc, "Container status is unhealthy, stopping");
			D_GOTO(err, rc);
		}
	}

	/* set cont_layout to POSIX type if the source is not in DAOS, if the
	 * destination is DAOS, and no destination container exists yet,
	 * then it knows to create a POSIX container
	 */
	if (src_file_dfs->type == POSIX)
		ca->cont_layout = DAOS_PROP_CO_LAYOUT_POSIX;

	/* Retrieve source container properties */
	if (src_file_dfs->type != POSIX) {
		/* if moving data from POSIX to DAOS and preserve_props option is on,
		 * then write container properties to the provided hdf5 filename
		 */
		if (ap->preserve_props != NULL && dst_file_dfs->type == POSIX) {
			/* preserve_props option is for filesystem copy (which uses DFS API),
			 * so do not retrieve roots or max oid property.
			 */
			rc = dm_cont_get_all_props(ap, ca->src_coh, &props, false,  true, false);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to get container properties");
				D_GOTO(out, rc);
			}
			rc = dm_serialize_cont_md(ap, ca, props, ap->preserve_props);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to serialize metadata");
				D_GOTO(out, rc);
			}

		}
		/* if DAOS -> DAOS copy container properties from src to dst */
		if (dst_file_dfs->type == DAOS) {
			/* src to dst copies never copy label, and filesystem copies use DFS
			 * so do not copy roots or max oid prop
			 */
			if (is_posix_copy)
				rc = dm_cont_get_all_props(ap, ca->src_coh, &props,
							   false, false, false);
			else
				rc = dm_cont_get_all_props(ap, ca->src_coh, &props,
							   true, false, true);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to get container properties");
				D_GOTO(out, rc);
			}
			ca->cont_layout = props->dpp_entries[1].dpe_val;
		}
	}

	/* open dst pool, dst cont, and mount dfs */
	if (dst_file_dfs->type == DAOS) {
		bool dst_cont_passed = strlen(ca->dst_cont) ? true : false;

		/* only connect if destination pool wasn't already opened */
		if (strlen(ca->dst_pool) != 0) {
			if (!daos_handle_is_valid(ca->dst_poh)) {
				rc = daos_pool_connect(ca->dst_pool, sysname, DAOS_PC_RW,
						       &ca->dst_poh, NULL, NULL);
				if (rc != 0) {
					DH_PERROR_DER(ap, rc,
						      "failed to connect to destination pool");
					D_GOTO(err, rc);
				}
			}
		/* if the dst pool uuid is null that means that this is a UNS destination path, so
		 * we copy the source pool uuid into the destination and try to connect again
		 */
		} else {
			strcpy(ca->dst_pool, ca->src_pool);
			rc = daos_pool_connect(ca->dst_pool, sysname, DAOS_PC_RW, &ca->dst_poh,
					       NULL, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "failed to connect to destination pool");
				D_GOTO(err, rc);
			}
			if (src_file_dfs->type == POSIX)
				dattr.da_type = DAOS_PROP_CO_LAYOUT_POSIX;
			else
				dattr.da_type = ca->cont_layout;
			if (props != NULL)
				dattr.da_props = props;
			rc = duns_create_path(ca->dst_poh, path, &dattr);
			if (rc != 0) {
				rc = daos_errno2der(rc);
				DH_PERROR_DER(ap, rc, "provide a destination pool or UNS path "
					      "of the form:\n\t --dst </$pool> | </path/to/uns>");
				D_GOTO(err, rc);
			}
			snprintf(ca->dst_cont, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_cont);
		}

		/* check preserve_props, if source is from POSIX and destination is DAOS we need
		 * to read container properties from the file that is specified before the DAOS
		 * destination container is created
		 */
		if (ap->preserve_props != NULL && src_file_dfs->type == POSIX) {
			rc = dm_deserialize_cont_md(ap, ca, ap->preserve_props, &props);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to deserialize metadata");
				D_GOTO(out, rc);
			}
		}

		/* try to open container if this is a filesystem copy, and if it fails try to create
		 * a destination, then attempt to open again
		 */
		if (dst_cont_passed) {
			rc = daos_cont_open(ca->dst_poh, ca->dst_cont, DAOS_COO_RW, &ca->dst_coh,
					    dst_cont_info, NULL);
			if (rc != 0 && rc != -DER_NONEXIST)
				D_GOTO(err, rc);
		} else {
			rc = -DER_NONEXIST;
		}
		if (rc == -DER_NONEXIST) {
			uuid_t cuuid;

			if (ca->cont_layout == DAOS_PROP_CO_LAYOUT_POSIX) {
				attr.da_props = props;
				if (dst_cont_passed) {
					rc = uuid_parse(ca->dst_cont, cuuid);
					if (rc)
						D_GOTO(err, rc);
					rc = dfs_cont_create(ca->dst_poh, cuuid, &attr, NULL, NULL);
				} else {
					rc = dfs_cont_create(ca->dst_poh, &cuuid, &attr,
							     NULL, NULL);
					uuid_unparse(cuuid, ca->dst_cont);
				}
				if (rc != 0) {
					rc = daos_errno2der(rc);
					DH_PERROR_DER(ap, rc,
						      "failed to create destination container");
					D_GOTO(err, rc);
				}
			} else {
				if (dst_cont_passed) {
					rc = uuid_parse(ca->dst_cont, cuuid);
					if (rc == 0)
						rc = daos_cont_create(ca->dst_poh, cuuid, props,
								      NULL);
					else
						rc = daos_cont_create_with_label(ca->dst_poh,
										 ca->dst_cont,
										 props, NULL, NULL);
				} else {
					rc = daos_cont_create(ca->dst_poh, &cuuid, props, NULL);
					uuid_unparse(cuuid, ca->dst_cont);
				}
				if (rc != 0) {
					DH_PERROR_DER(ap, rc,
						      "failed to create destination container");
					D_GOTO(err, rc);
				}
			}
			rc = daos_cont_open(ca->dst_poh, ca->dst_cont, DAOS_COO_RW, &ca->dst_coh,
					    dst_cont_info, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "failed to open container");
				D_GOTO(err, rc);
			}
			fprintf(ap->outstream, "Successfully created container %s\n", ca->dst_cont);
		}
		if (is_posix_copy) {
			rc = dfs_sys_mount(ca->dst_poh, ca->dst_coh, O_RDWR, DFS_SYS_NO_LOCK,
					   &dst_file_dfs->dfs_sys);
			if (rc != 0) {
				rc = daos_errno2der(rc);
				DH_PERROR_DER(ap, rc, "dfs_mount on destination failed");
				dfs_sys_umount(dst_file_dfs->dfs_sys);
				D_GOTO(err, rc);
			}
		}

		/* check preserve_props, if source is from POSIX and destination is DAOS we
		 * need to read user attributes from the file that is specified, and set them
		 * in the destination container
		 */
		if (ap->preserve_props != NULL && src_file_dfs->type == POSIX) {
			rc = dm_deserialize_cont_attrs(ap, ca, ap->preserve_props);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to deserialize user attributes");
				D_GOTO(err, rc);
			}
		}
	}
	/* get source container user attributes and copy them to the DAOS destination container */
	if (src_file_dfs->type == DAOS && dst_file_dfs->type == DAOS) {
		rc = dm_copy_usr_attrs(ap, ca->src_coh, ca->dst_coh);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Copying user attributes failed");
			D_GOTO(err, rc);
		}
	}
	D_GOTO(out, rc);
err:
	if (daos_handle_is_valid(ca->dst_coh)) {
		rc2 = daos_cont_close(ca->dst_coh, NULL);
		if (rc2 != 0)
			DH_PERROR_DER(ap, rc2, "failed to close destination container");
	}
	if (daos_handle_is_valid(ca->src_coh)) {
		rc2 = daos_cont_close(ca->src_coh, NULL);
		if (rc2 != 0)
			DH_PERROR_DER(ap, rc2, "failed to close source container");
	}
	if (daos_handle_is_valid(ca->dst_poh)) {
		rc2 = daos_pool_disconnect(ca->dst_poh, NULL);
		if (rc2 != 0)
			DH_PERROR_DER(ap, rc2, "failed to disconnect from destination pool %s",
				      ca->dst_pool);
	}
	if (daos_handle_is_valid(ca->src_poh)) {
		rc2 = daos_pool_disconnect(ca->src_poh, NULL);
		if (rc2 != 0)
			DH_PERROR_DER(ap, rc2, "Failed to disconnect from source pool %s",
				      ca->src_pool);
	}
out:
	if (props != NULL)
		daos_prop_free(props);
	return rc;
}

static inline void
file_set_defaults_dfs(struct file_dfs *file_dfs)
{
	/* set defaults for file_dfs struct */
	file_dfs->type = DAOS;
	file_dfs->fd = -1;
	file_dfs->offset = 0;
	file_dfs->obj = NULL;
	file_dfs->dfs_sys = NULL;
}

static int
dm_disconnect(struct cmd_args_s *ap,
	      bool is_posix_copy,
	      struct dm_args *ca,
	      struct file_dfs *src_file_dfs,
	      struct file_dfs *dst_file_dfs)
{
	 /* The fault injection tests expect no memory leaks but inject faults that
	 * block umount/close/disconnect calls, etc. So, if I use GOTO and return the error
	 * code immediately after a failure to umount/close/disconnect then fault injection
	 * will always report a memory leak. Is it better to immediately return if one of
	 * these fails? This will cause memory leaks in fault injection tests for fs copy,
	 * so not sure what is the best thing to do here.
	 */
	int rc = 0;

	if (src_file_dfs->type == DAOS) {
		if (is_posix_copy) {
			rc = dfs_sys_umount(src_file_dfs->dfs_sys);
			if (rc != 0) {
				rc = daos_errno2der(rc);
				DH_PERROR_DER(ap, rc, "failed to unmount source");
				dfs_sys_umount(src_file_dfs->dfs_sys);
			}
			src_file_dfs->dfs_sys = NULL;
		}
		rc = daos_cont_close(ca->src_coh, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to close source container");
			daos_cont_close(ca->src_coh, NULL);
		}
		rc = daos_pool_disconnect(ca->src_poh, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to disconnect source pool");
			daos_pool_disconnect(ca->src_poh, NULL);
		}
	}
	if (dst_file_dfs->type == DAOS) {
		if (is_posix_copy) {
			rc = dfs_sys_umount(dst_file_dfs->dfs_sys);
			if (rc != 0) {
				rc = daos_errno2der(rc);
				DH_PERROR_DER(ap, rc, "failed to unmount source");
				dfs_sys_umount(dst_file_dfs->dfs_sys);
			}
			dst_file_dfs->dfs_sys = NULL;
		}
		rc = daos_cont_close(ca->dst_coh, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to close destination container");
			daos_cont_close(ca->dst_coh, NULL);
		}
		rc = daos_pool_disconnect(ca->dst_poh, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to disconnect destination pool");
			daos_pool_disconnect(ca->dst_poh, NULL);
		}
	}
	return rc;
}

/*
* Parse a path of the format:
* daos://<pool>/<cont>/<path> | <UNS path> | <POSIX path>
* Modifies "path" to be the relative container path, defaulting to "/".
* Returns 0 if a daos path was successfully parsed, a error number if not.
*/
static int
dm_parse_path(struct file_dfs *file, char *path, size_t path_len, char (*pool_str)[],
	      char (*cont_str)[])
{
	struct duns_attr_t	dattr = {0};
	int			rc = 0;
	char			*tmp_path1 = NULL;
	char			*path_dirname = NULL;
	char			*tmp_path2 = NULL;
	char			*path_basename = NULL;

	rc = duns_resolve_path(path, &dattr);
	if (rc == 0) {
		snprintf(*pool_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_pool);
		snprintf(*cont_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_cont);
		if (dattr.da_rel_path == NULL)
			strncpy(path, "/", path_len);
		else
			strncpy(path, dattr.da_rel_path, path_len);
	} else {
		/* If basename does not exist yet then duns_resolve_path will fail even if
		 * dirname is a UNS path
		 */

		/* get dirname */
		D_STRNDUP(tmp_path1, path, path_len);
		if (tmp_path1 == NULL)
			D_GOTO(out, rc = ENOMEM);
		path_dirname = dirname(tmp_path1);
		/* reset before calling duns_resolve_path with new string */
		memset(&dattr, 0, sizeof(struct duns_attr_t));

		/* Check if this path represents a daos pool and/or container. */
		rc = duns_resolve_path(path_dirname, &dattr);
		if (rc == 0) {
			/* if duns_resolve_path succeeds then concat basename to da_rel_path */
			D_STRNDUP(tmp_path2, path, path_len);
			if (tmp_path2 == NULL)
				D_GOTO(out, rc = ENOMEM);
			path_basename = basename(tmp_path2);

			/* dirname might be root uns path, if that is the case,
			 * then da_rel_path might be NULL
			 */
			if (dattr.da_rel_path == NULL)
				snprintf(path, path_len, "/%s", path_basename);
			else
				snprintf(path, path_len, "%s/%s", dattr.da_rel_path, path_basename);
			snprintf(*pool_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_pool);
			snprintf(*cont_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_cont);
		} else if (rc == ENOMEM) {
			/* TODO: Take this path of rc != ENOENT? */
			D_GOTO(out, rc);
		} else if (strncmp(path, "daos://", 7) == 0) {
			/* Error, since we expect a DAOS path */
			D_GOTO(out, rc);
		} else {
			/* not a DAOS path, set type to POSIX,
			 * POSIX dir will be checked with stat
			 * at the beginning of fs_copy
			 */
			rc = 0;
			file->type = POSIX;
		}
	}
out:
	D_FREE(tmp_path1);
	D_FREE(tmp_path2);
	duns_destroy_attr(&dattr);
	return daos_errno2der(rc);
}

int
fs_copy_hdlr(struct cmd_args_s *ap)
{
	int			rc = 0;
	int			rc2 = 0;
	char			*src_str = NULL;
	char			*dst_str = NULL;
	size_t			src_str_len = 0;
	size_t			dst_str_len = 0;
	daos_cont_info_t	src_cont_info = {0};
	daos_cont_info_t	dst_cont_info = {0};
	struct file_dfs		src_file_dfs = {0};
	struct file_dfs		dst_file_dfs = {0};
	struct dm_args		ca = {0};
	bool			is_posix_copy = true;
	uint64_t		num_dirs = 0;
	uint64_t		num_files = 0;

	set_dm_args_default(&ca);
	file_set_defaults_dfs(&src_file_dfs);
	file_set_defaults_dfs(&dst_file_dfs);

	src_str_len = strlen(ap->src);
	if (src_str_len == 0) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Source path required");
		D_GOTO(out, rc);
	}
	D_STRNDUP(src_str, ap->src, src_str_len);
	if (src_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for source path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(&src_file_dfs, src_str, src_str_len, &ca.src_pool, &ca.src_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to parse source path");
		D_GOTO(out, rc);
	}

	dst_str_len = strlen(ap->dst);
	if (dst_str_len == 0) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Destination path required");
		D_GOTO(out, rc);
	}
	D_STRNDUP(dst_str, ap->dst, dst_str_len);
	if (dst_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for destination path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(&dst_file_dfs, dst_str, dst_str_len, &ca.dst_pool, &ca.dst_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to parse destination path");
		D_GOTO(out, rc);
	}

	rc = dm_connect(ap, is_posix_copy, &src_file_dfs, &dst_file_dfs, &ca,
			ap->sysname, ap->dst, &src_cont_info, &dst_cont_info);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "fs copy failed to connect");
		D_GOTO(out, rc);
	}

	rc = fs_copy(ap, &src_file_dfs, &dst_file_dfs, src_str, dst_str, &num_dirs, &num_files);
	if (rc != 0)
		D_GOTO(out_disconnect, rc);

	if (dst_file_dfs.type == DAOS) {
		fprintf(ap->outstream, "Successfully copied to DAOS: %s\n", dst_str);
	} else if (dst_file_dfs.type == POSIX) {
		fprintf(ap->outstream, "Successfully copied to POSIX: %s\n", dst_str);
	}
	fprintf(ap->outstream, "    Directories: %lu\n", num_dirs);
	fprintf(ap->outstream, "    Files:       %lu\n", num_files);

out_disconnect:
	/* umount dfs, close conts, and disconnect pools */
	rc2 = dm_disconnect(ap, is_posix_copy, &ca, &src_file_dfs, &dst_file_dfs);
	if (rc2 != 0)
		DH_PERROR_DER(ap, rc2, "failed to disconnect");
out:
	D_FREE(src_str);
	D_FREE(dst_str);
	return rc;
}

static int
cont_clone_recx_single(struct cmd_args_s *ap,
		       daos_key_t *dkey,
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
		DH_PERROR_DER(ap, rc, "Failed to fetch source value");
		D_GOTO(out, rc);
	}
	rc = daos_obj_update(*dst_oh, DAOS_TX_NONE, 0, dkey, 1, iod,
			     &sgl, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to update destination value");
		D_GOTO(out, rc);
	}
out:
	return rc;
}

static int
cont_clone_recx_array(struct cmd_args_s *ap,
		      daos_key_t *dkey,
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
		rc = daos_obj_list_recx(*src_oh, DAOS_TX_NONE, dkey, akey, &size, &number, recxs,
					eprs, &recx_anchor, true, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to list recx");
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
			D_REALLOC_NZ(buf, prev_buf, buf_len);
			if (buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			buf_len_alloc = buf_len;
		}
		d_iov_set(&iov, buf, buf_len);

		/* fetch recx values from source */
		rc = daos_obj_fetch(*src_oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl, NULL, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to fetch source recx");
			D_GOTO(out, rc);
		}

		/* Sanity check that fetch returns as expected */
		if (sgl.sg_nr_out != 1) {
			DH_PERROR_DER(ap, rc, "Failed to fetch source recx");
			D_GOTO(out, rc = -DER_INVAL);
		}

		/* update fetched recx values and place in
		 * destination object
		 */
		rc = daos_obj_update(*dst_oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to update destination recx");
			D_GOTO(out, rc);
		}
	}
out:
	D_FREE(buf);
	return rc;
}

static int
cont_clone_list_akeys(struct cmd_args_s *ap,
		      daos_handle_t *src_oh,
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
		rc = daos_obj_list_akey(*src_oh, DAOS_TX_NONE, &diov, &akey_number, akey_kds,
					&sgl, &akey_anchor, NULL);
		if (rc == -DER_KEY2BIG) {
			/* call list dkey again with bigger buffer */
			key_buf = large_key;
			key_buf_len = ENUM_LARGE_KEY_BUF;
			d_iov_set(&iov, key_buf, key_buf_len);
			rc = daos_obj_list_akey(*src_oh, DAOS_TX_NONE, &diov, &akey_number,
						akey_kds, &sgl, &akey_anchor, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to list akeys");
				D_GOTO(out, rc);
			}
		}

		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to list akeys");
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
				DH_PERROR_DER(ap, rc, "Failed to fetch source object");
				D_FREE(akey);
				D_GOTO(out, rc);
			}

			/* if iod_size == 0 then this is a DAOS_IOD_ARRAY
			 * type
			 */
			if ((int)iod.iod_size == 0) {
				rc = cont_clone_recx_array(ap, &diov, &aiov, src_oh, dst_oh, &iod);
				if (rc != 0) {
					DH_PERROR_DER(ap, rc, "Failed to copy record");
					D_FREE(akey);
					D_GOTO(out, rc);
				}
			} else {
				rc = cont_clone_recx_single(ap, &diov, src_oh, dst_oh, &iod);
				if (rc != 0) {
					DH_PERROR_DER(ap, rc, "Failed to copy record");
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
cont_clone_list_dkeys(struct cmd_args_s *ap,
		      daos_handle_t *src_oh,
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
		rc = daos_obj_list_dkey(*src_oh, DAOS_TX_NONE, &dkey_number, dkey_kds,
					&sgl, &dkey_anchor, NULL);
		if (rc == -DER_KEY2BIG) {
			/* call list dkey again with bigger buffer */
			key_buf = large_key;
			key_buf_len = ENUM_LARGE_KEY_BUF;
			d_iov_set(&iov, key_buf, key_buf_len);
			rc = daos_obj_list_dkey(*src_oh, DAOS_TX_NONE, &dkey_number, dkey_kds,
						&sgl, &dkey_anchor, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to list dkeys");
				D_GOTO(out, rc);
			}
		}

		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to list dkeys");
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
			rc = cont_clone_list_akeys(ap, src_oh, dst_oh, diov);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to list akeys");
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
	int			rc2 = 0;
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

	set_dm_args_default(&ca);
	file_set_defaults_dfs(&src_cp_type);
	file_set_defaults_dfs(&dst_cp_type);

	src_str_len = strlen(ap->src);
	D_STRNDUP(src_str, ap->src, src_str_len);
	if (src_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for source path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(&src_cp_type, src_str, src_str_len, &ca.src_pool, &ca.src_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to parse source path");
		D_GOTO(out, rc);
	}

	dst_str_len = strlen(ap->dst);
	D_STRNDUP(dst_str, ap->dst, dst_str_len);
	if (dst_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for destination path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(&dst_cp_type, dst_str, dst_str_len, &ca.dst_pool, &ca.dst_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to parse destination path");
		D_GOTO(out, rc);
	}

	if (strlen(ca.dst_cont) != 0) {
		/* make sure destination container does not already exist for object level copies
		 */
		rc = daos_pool_connect(ca.dst_pool, ap->sysname, DAOS_PC_RW, &ca.dst_poh,
				       NULL, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to connect to destination pool");
			D_GOTO(out, rc);
		}
		/* make sure this destination container doesn't exist already,
		 * if it does, exit
		 */
		rc = daos_cont_open(ca.dst_poh, ca.dst_cont, DAOS_COO_RW, &ca.dst_coh,
				    &dst_cont_info, NULL);
		if (rc == 0) {
			fprintf(ap->errstream,
				"This destination container already exists. Please provide a "
				"destination container uuid that does not exist already, or "
				"provide an existing pool or new UNS path of the "
				"form:\n\t--dst </$pool> | <path/to/uns>\n");
			/* disconnect from only destination and exit */
			rc = daos_cont_close(ca.dst_coh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to close destination container");
				D_GOTO(out, rc);
			}
			rc = daos_pool_disconnect(ca.dst_poh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc2,
					      "failed to disconnect from destination pool %s",
					      ca.dst_pool);
				D_GOTO(out, rc);
			}
			D_GOTO(out, rc = 1);
		}
	}

	rc = dm_connect(ap, is_posix_copy, &dst_cp_type, &src_cp_type, &ca, ap->sysname,
			ap->dst, &src_cont_info, &dst_cont_info);
	if (rc != 0) {
		D_GOTO(out_disconnect, rc);
	}
	rc = daos_cont_create_snap_opt(ca.src_coh, &epoch, NULL,
				       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT, NULL);
	if (rc) {
		DH_PERROR_DER(ap, rc, "Failed to create snapshot");
		D_GOTO(out_disconnect, rc);
	}
	rc = daos_oit_open(ca.src_coh, epoch, &toh, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to open object iterator");
		D_GOTO(out_snap, rc);
	}
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(toh, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to list objects");
			D_GOTO(out_oit, rc);
		}

		/* list object ID's */
		for (i = 0; i < oids_nr; i++) {
			rc = daos_obj_open(ca.src_coh, oids[i], 0, &oh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to open source object");
				D_GOTO(out_oit, rc);
			}
			rc = daos_obj_open(ca.dst_coh, oids[i], 0, &dst_oh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to open destination object");
				D_GOTO(err_dst, rc);
			}
			rc = cont_clone_list_dkeys(ap, &oh, &dst_oh);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to list keys");
				D_GOTO(err_obj, rc);
			}
			rc = daos_obj_close(oh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to close source object");
				D_GOTO(out_oit, rc);
			}
			rc = daos_obj_close(dst_oh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to close destination object");
				D_GOTO(err_dst, rc);
			}
		}
	}
	D_GOTO(out_oit, rc);
err_obj:
	rc2 = daos_obj_close(dst_oh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to close destination object");
	}
err_dst:
	rc2 = daos_obj_close(oh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to close source object");
	}
out_oit:
	rc2 = daos_oit_close(toh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to close object iterator");
		D_GOTO(out, rc2);
	}
out_snap:
	epr.epr_lo = epoch;
	epr.epr_hi = epoch;
	rc2 = daos_cont_destroy_snap(ca.src_coh, epr, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to destroy snapshot");
	}
out_disconnect:
	/* close src and dst pools, conts */
	rc2 = dm_disconnect(ap, is_posix_copy, &ca, &src_cp_type, &dst_cp_type);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to disconnect");
	}
out:
	if (rc == 0) {
		fprintf(ap->outstream, "Successfully copied to destination container %s\n",
			ca.dst_cont);
	}
	D_FREE(src_str);
	D_FREE(dst_str);
	D_FREE(ca.src);
	D_FREE(ca.dst);
	return rc;
}

static int
print_acl(struct cmd_args_s *ap, FILE *outstream, daos_prop_t *acl_prop,
	  bool verbose)
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
		fprintf(ap->errstream, "failed to print ACL: %s (%d)\n",
			d_errstr(rc), rc);
	}

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
parse_acl_file(struct cmd_args_s *ap, const char *path, struct daos_acl **acl)
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
		fprintf(ap->errstream,
			"Unable to read ACL input file '%s': %s\n",
			path, strerror(errno));
		return daos_errno2der(errno);
	}

	tmp_acl = daos_acl_create(NULL, 0);
	if (tmp_acl == NULL) {
		fprintf(ap->errstream, "Unable to allocate memory for ACL\n");
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
			fprintf(ap->errstream,
				"Error parsing ACE '%s' from file: %s (%d)\n",
				trimmed, d_errdesc(rc), rc);
			D_GOTO(parse_err, rc);
		}

		rc = daos_acl_add_ace(&tmp_acl, ace);
		daos_ace_free(ace);
		if (rc != 0) {
			fprintf(ap->errstream,
				"Error parsing ACL file: %s (%d)\n",
				d_errdesc(rc), rc);
			D_GOTO(parse_err, rc);
		}

		D_FREE(line);
	}

	if (daos_acl_validate(tmp_acl) != 0) {
		fprintf(ap->errstream, "Content of ACL file is invalid\n");
		D_GOTO(parse_err, rc = -DER_INVAL);
	}

	*acl = tmp_acl;
	D_GOTO(out, rc = 0);

parse_err:
	daos_acl_free(tmp_acl);
out:
	D_FREE(line);
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
		fprintf(ap->errstream,
			"Parameter --acl-file is required\n");
		return -DER_INVAL;
	}

	rc = parse_acl_file(ap, ap->aclfile, &acl);
	if (rc != 0)
		return rc;

	rc = daos_cont_overwrite_acl(ap->cont, acl, NULL);
	daos_acl_free(acl);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to overwrite ACL for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"overwrite appeared to succeed, but failed to fetch ACL"
			" for confirmation: %s (%d)\n", d_errdesc(rc), rc);
		return rc;
	}

	rc = print_acl(ap, ap->outstream, prop_out, false);

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
		fprintf(ap->errstream,
			"either parameter --acl-file or --entry is required\n");
		return -DER_INVAL;
	}

	if (ap->aclfile) {
		rc = parse_acl_file(ap, ap->aclfile, &acl);
		if (rc != 0)
			return rc;
	} else {
		rc = daos_ace_from_str(ap->entry, &ace);
		if (rc != 0) {
			fprintf(ap->errstream,
				"failed to parse entry: %s (%d)\n",
				d_errdesc(rc), rc);
			return rc;
		}

		acl = daos_acl_create(&ace, 1);
		daos_ace_free(ace);
		if (acl == NULL) {
			rc = -DER_NOMEM;
			fprintf(ap->errstream,
				"failed to make ACL from entry: %s "
				"(%d)\n", d_errdesc(rc), rc);
			return rc;
		}
	}

	rc = daos_cont_update_acl(ap->cont, acl, NULL);
	daos_acl_free(acl);
	if (rc != 0) {
		fprintf(ap->errstream, "failed to update ACL for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"update appeared to succeed, but failed to fetch ACL "
			"for confirmation: %s (%d)\n", d_errdesc(rc), rc);
		return rc;
	}

	rc = print_acl(ap, ap->outstream, prop_out, false);

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
		fprintf(ap->errstream,
			"parameter --principal is required\n");
		return -DER_INVAL;
	}

	rc = daos_acl_principal_from_str(ap->principal, &type, &name);
	if (rc != 0) {
		fprintf(ap->errstream,
			"unable to parse principal string '%s': %s"
			"(%d)\n", ap->principal, d_errdesc(rc), rc);
		return rc;
	}

	rc = daos_cont_delete_acl(ap->cont, type, name, NULL);
	D_FREE(name);
	if (rc != 0) {
		fprintf(ap->errstream, "failed to delete ACL for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"delete appeared to succeed, but failed to fetch ACL "
			"for confirmation: %s (%d)\n", d_errdesc(rc), rc);
		return rc;
	}

	rc = print_acl(ap, ap->outstream, prop_out, false);

	daos_prop_free(prop_out);
	return rc;
}

int
cont_set_owner_hdlr(struct cmd_args_s *ap)
{
	int	rc;

	if (!ap->user && !ap->group) {
		fprintf(ap->errstream,
			"parameter --user or --group is required\n");
		return -DER_INVAL;
	}

	rc = daos_cont_set_owner(ap->cont, ap->user, ap->group, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to set owner for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		return rc;
	}

	fprintf(ap->outstream, "successfully updated owner for container\n");
	return rc;
}

int
cont_rollback_hdlr(struct cmd_args_s *ap)
{
	int	rc;

	if (ap->epc == 0 && ap->snapname_str == NULL) {
		fprintf(ap->errstream,
			"either parameter --epc or --snap is required\n");
		return -DER_INVAL;
	}
	if (ap->epc != 0 && ap->snapname_str != NULL) {
		fprintf(ap->errstream,
			"both parameters --epc and --snap could not be specified\n");
		return -DER_INVAL;
	}

	if (ap->snapname_str != NULL) {
		rc = cont_list_snaps_hdlr(ap);
		if (rc != 0)
			return rc;
	}
	rc = daos_cont_rollback(ap->cont, ap->epc, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to roll back container %s to snapshot 0x"DF_X64": %s (%d)\n",
			ap->cont_str, ap->epc, d_errdesc(rc), rc);
		return rc;
	}

	fprintf(ap->outstream, "successfully rollback container\n");
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
		fprintf(ap->errstream,
			"open of container's OIT failed: "DF_RC"\n", DP_RC(rc));
		goto out_snap;
	}

	while (!daos_anchor_is_eof(&anchor)) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(oit, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			fprintf(ap->errstream,
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
		fprintf(ap->errstream,
			"failed to retrieve layout for object "DF_OID
			": %s (%d)\n", DP_OID(ap->oid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* Print the object layout */
	fprintf(ap->outstream,
		"oid: "DF_OID" ver %d grp_nr: %d\n", DP_OID(ap->oid),
		layout->ol_ver, layout->ol_nr);

	for (i = 0; i < layout->ol_nr; i++) {
		struct daos_obj_shard *shard;

		shard = layout->ol_shards[i];
		fprintf(ap->outstream, "grp: %d\n", i);
		for (j = 0; j < shard->os_replica_nr; j++)
			fprintf(ap->outstream, "replica %d %d\n", j,
				shard->os_shard_loc[j].sd_rank);
	}

	daos_obj_layout_free(layout);

out:
	return rc;
}
