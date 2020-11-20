/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/* daos_hdlr.c - resource and operation-specific handler functions
 * invoked by daos(8) utility
 */

#define D_LOGFAC	DD_FAC(client)

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
	daos_prop_t			*prop_query;
	int				rc = 0;
	int				rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_GET_PROP);

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       ap->mdsrv, DAOS_PC_RO, &ap->pool,
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
	size_t				value_size;
	int				rc = 0;
	int				rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_SET_ATTR);

	if (ap->attrname_str == NULL || ap->value_str == NULL) {
		fprintf(stderr, "both attribute name and value must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       ap->mdsrv, DAOS_PC_RW, &ap->pool,
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
	int				rc = 0;
	int				rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_DEL_ATTR);

	if (ap->attrname_str == NULL) {
		fprintf(stderr, "attribute name must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       ap->mdsrv, DAOS_PC_RW, &ap->pool,
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
			       ap->mdsrv, DAOS_PC_RO, &ap->pool,
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
	if (buf != NULL)
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
			       ap->mdsrv, DAOS_PC_RO, &ap->pool,
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
	if (buf != NULL)
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
			       ap->mdsrv, DAOS_PC_RO, &ap->pool,
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
			       ap->mdsrv, DAOS_PC_RO, &ap->pool,
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

/* TODO implement the following container op functions
 * all with signatures similar to this:
 * int cont_FN_hdlr(struct cmd_args_s *ap)
 *
 * cont_list_objs_hdlr()
 * int cont_stat_hdlr()
 */

/* this routine can be used to list all snapshots or to map a snapshot name
 * to its epoch number.
 */
int
cont_list_snaps_hdlr(struct cmd_args_s *ap, char *snapname, daos_epoch_t *epoch)
{
	daos_epoch_t *epochs = NULL;
	char **names = NULL;
	daos_anchor_t anchor;
	int rc, i, snaps_count, expected_count;

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
	size_t				value_size;
	int				rc = 0;

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
	int				rc = 0;

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
	char	*buf= NULL;
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
	if (buf != NULL)
		D_FREE(buf);

	return rc;

}

int
cont_list_attrs_hdlr(struct cmd_args_s *ap)
{
	size_t				 size, total_size, expected_size,
					 cur = 0, len;
	char				*buf = NULL;
	int				rc = 0;

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
	if (buf != NULL)
		D_FREE(buf);

	return rc;

}

static int
cont_decode_props(daos_prop_t *props)
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

	return rc;
}

/* cont_get_prop_hdlr() - get container properties */
int
cont_get_prop_hdlr(struct cmd_args_s *ap)
{
	daos_prop_t		*prop_query;
	int			rc = 0;
	uint32_t		i;
	uint32_t		entry_type;

	/*
	 * Get all props except the ACL
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

	D_PRINT("Container properties for "DF_UUIDF" :\n", DP_UUID(ap->c_uuid));

	rc = cont_decode_props(prop_query);

err_out:
	daos_prop_free(prop_query);
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
		entry = &(ap->props->dpp_entries[i]);
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
		*entry = &(ap->props->dpp_entries[0]);
	} else {
		*entry = &(ap->props->dpp_entries[ap->props->dpp_nr]);
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
					DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
				D_GOTO(err_out, rc);
			}

			dfs_query(dfs, &attr);
			daos_oclass_id2name(attr.da_oclass_id, oclass);
			printf("Object Class:\t%s\n", oclass);
			printf("Chunk Size:\t%zu\n", attr.da_chunk_size);

			rc = dfs_umount(dfs);
			if (rc) {
				fprintf(stderr, "failed to unmount container "
					DF_UUIDF": %s (%d)\n",
					DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
				D_GOTO(err_out, rc);
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
parse_filename(const char* path, char** _obj_name, char** _cont_name)
{
	char *f1 = NULL;
	char *f2 = NULL;
	char *fname = NULL;
	char *cont_name = NULL;
	int rc = 0;
	if (path == NULL || _obj_name == NULL || _cont_name == NULL)
			return -EINVAL;

	if (strcmp(path, "/") == 0) {
		*_cont_name = strdup("/");
		if (*_cont_name == NULL)
			return -ENOMEM;
		*_obj_name = NULL;
		return 0;
	}
	f1 = strdup(path);
	if (f1 == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	f2 = strdup(path);
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
			cont_name = strdup(cwd);
			if (cont_name == NULL) {
				rc = -ENOMEM;
				goto out;
			}
		} else {
			char *new_dir = calloc(strlen(cwd) + strlen(cont_name) + 1, sizeof(char));
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
		*_cont_name = strdup(cont_name);
		if (*_cont_name == NULL) {
			rc = -ENOMEM;
			goto out;
		}
	}
	*_obj_name = strdup(fname);
	if (*_obj_name == NULL) {
		free(*_cont_name);
		*_cont_name = NULL;
		rc = -ENOMEM;
		goto out;
	}
	out:
		if (f1)
			free(f1);
		if (f2)
			free(f2);
	return rc;
}

static ssize_t
daos_write(struct daos_file_t *daos_file, char *file, void *buf, size_t size)
{
	int rc;
	/* record address and size of user buffer in io vector */
	d_iov_t iov;
	d_iov_set(&iov, buf, size);

	/* define scatter-gather list for dfs_write */
	d_sg_list_t sgl;
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	sgl.sg_nr_out = 1;

	rc = dfs_write(daos_file->dfs, daos_file->obj, &sgl, daos_file->offset, NULL); 

	if (rc) {
		fprintf(stderr, "dfs_write %s failed (%d %s)\n",
			file, rc, strerror(rc));
		errno = rc;
		size = -1;
	}

	daos_file->offset += (daos_off_t)size;
	return (ssize_t) size;
}

static ssize_t
daos_file_write(daos_file_t *daos_file, char* file,
		void* buf, size_t size)
{
	ssize_t num_bytes_written = 0;
	if (daos_file->type == POSIX) {
		num_bytes_written = write(daos_file->fd, buf, size);
	} else if (daos_file->type == DAOS) {
		num_bytes_written = daos_write(daos_file, file, buf, size);
	} else {
		fprintf(stderr, "File type not known: %s type=%d\n",
			file, daos_file->type);
	}
	if (num_bytes_written < 0) {
		fprintf(stderr, "write error on %s type=%d\n",
			file, daos_file->type);
	}
	return num_bytes_written;
}

static void 
daos_open(daos_file_t *daos_file, char *file, int flags, mode_t mode)
{

	int rc; 
	dfs_obj_t *parent = NULL;
	char *name = NULL, *dir_name = NULL;

	parse_filename(file, &name, &dir_name);

	assert(dir_name);

	rc = dfs_lookup(daos_file->dfs, dir_name, O_RDWR, &parent, NULL, NULL);
	if (parent == NULL) {
		fprintf(stderr, "dfs_lookup %s failed \n", dir_name);
	}

	rc = dfs_open(daos_file->dfs, parent, name, mode | S_IFREG,
		flags, 0, 0, NULL, &(daos_file->obj));
	if (rc)
		fprintf(stderr, "dfs_open %s failed (%d)\n", name, rc);
}

void
daos_file_open(daos_file_t *daos_file, char* file, int flags, ...)
{
	/* extract the mode */
	int mode_set = 0;
	mode_t mode  = 0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
		mode_set = 1;
	}

	if (daos_file->type == POSIX) {
		if (mode_set) {
			daos_file->fd = open(file, flags, mode);
		} else {
			daos_file->fd = open(file, flags);
		}
	} else if (daos_file->type == DAOS) {
		daos_open(daos_file, file, flags, mode);
	} else {
		fprintf(stderr, "File type not known: %s type=%d\n",
			file, daos_file->type);
	}
}

static int
daos_mkdir(daos_file_t *daos_file, const char *path)
{
	int rc = 0;
	dfs_obj_t *parent = NULL;
	char *name = NULL;
	char *dname = NULL;
	parse_filename(path, &name, &dname);
    
	assert(dname);
	if (name && strcmp(name, "/") != 0) {
		rc = dfs_lookup(daos_file->dfs, dname, O_RDWR, &parent, NULL, NULL);
		if (parent == NULL)
			fprintf(stderr, "dfs_lookup %s failed \n", dname);
		rc = dfs_mkdir(daos_file->dfs, parent, name, S_IRWXU, 0);
		if (rc == EEXIST) {
			fprintf(stdout, "dfs_mkdir %s already exists, %d \n", name, rc);
		} else if (rc) {	
			fprintf(stderr, "dfs_mkdir %s failed, %d \n", name, rc);
		}
	}
	return rc;
}

static int 
daos_file_mkdir(struct daos_file_t *daos_file, const char* dir, mode_t mode)
{
	int rc = 0;
	if (daos_file->type == POSIX) {
		rc = mkdir(dir, mode);
	} else if (daos_file->type == DAOS) {
		rc = daos_mkdir(daos_file, dir);
	} else {
		fprintf(stderr, "File type not known: %s type=%d\n", dir, daos_file->type);
	}
	return rc;
}

static DIR*
daos_opendir(daos_file_t *daos_file, const char* dir)
{
	struct dfs_daos_t* dirp = calloc(1, sizeof(*dirp));
	if (dirp == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	dfs_lookup(daos_file->dfs, dir, O_RDWR, &dirp->dir, NULL, NULL);
	if (dirp->dir == NULL) {
		fprintf(stderr, "dfs_lookup %s failed\n", dir);
		errno = ENOENT;
		free(dirp);
		dirp = NULL;
	}
	return (DIR *)dirp;
}

static DIR*
daos_file_opendir(daos_file_t *daos_file, const char* dir)
{
	DIR* dirp = NULL;
	if (daos_file->type == POSIX) {
		dirp = opendir(dir);
	} else if (daos_file->type == DAOS) {
		dirp = daos_opendir(daos_file, dir);
	} else {
		fprintf(stderr, "File type not known: %s type=%d\n",
			dir, daos_file->type);
	}
	return dirp;
}

static struct dirent*
daos_readdir(daos_file_t *daos_file, DIR* _dirp) 
{
	struct dfs_daos_t *dirp = (struct dfs_daos_t *)_dirp;
	if (dirp->num_ents) {
		goto ret;
	}
	dirp->num_ents = NUM_DIRENTS;
	int rc;
	while (!daos_anchor_is_eof(&dirp->anchor)) {
		rc = dfs_readdir(daos_file->dfs, dirp->dir,
				&dirp->anchor, &dirp->num_ents,
				dirp->ents);
		if (rc) {
			fprintf(stderr, "dfs_readdir failed (%d %s)\n", rc, strerror(rc));
			dirp->num_ents = 0;
			errno = ENOENT;
			return NULL;
		}
		if (dirp->num_ents == 0) {
			continue;
		}
		goto ret;
	}
	assert(daos_anchor_is_eof(&dirp->anchor));
	return NULL;
ret:
	dirp->num_ents--;
	return &dirp->ents[dirp->num_ents];
}

static struct dirent*
daos_file_readdir(daos_file_t *daos_file, DIR* dirp)
{
	struct dirent* entry = NULL;
	if (daos_file->type == POSIX) {
		entry = readdir(dirp);
	} else if (daos_file->type == DAOS) {
		entry = daos_readdir(daos_file, dirp);
	} else {
		fprintf(stderr, "File type not known, type=%d\n",
			daos_file->type);
	}
	return entry;
}

static int
daos_stat(daos_file_t *daos_file, const char *path, struct stat* buf)
{
	dfs_obj_t *parent = NULL;
	char* name        = NULL;
	char* dir_name    = NULL;
	parse_filename(path, &name, &dir_name);
	assert(dir_name);

	int rc = 0;

	/* Lookup the parent directory */
	rc = dfs_lookup(daos_file->dfs, dir_name, O_RDWR, &parent, NULL, NULL);
	if (parent == NULL) {
		fprintf(stderr, "dfs_lookup %s failed \n", dir_name);
		errno = ENOENT;
		rc = -1;
	} else {
		/* Stat the path */
		rc = dfs_stat(daos_file->dfs, parent, name, buf);
		if (rc) {
			fprintf(stderr, "dfs_stat %s failed (%d %s)\n",
				name, rc, strerror(rc));
			errno = rc;
			rc = -1;
		}
	}
	if (name != NULL)
		free(name);
	if (dir_name != NULL)
		free(dir_name);
	return rc;
}

static int
daos_file_lstat(daos_file_t *daos_file, const char *path, struct stat *buf)
{
	int rc = 0;
	if (daos_file->type == POSIX) {
		rc = lstat(path, buf);
	} else if (daos_file->type == DAOS) {
		rc = daos_stat(daos_file, path, buf);
	} else {
		fprintf(stderr, "File type not known, file=%s, type=%d\n",
			path, daos_file->type);
	}
	return rc;
}

static ssize_t
daos_read(daos_file_t *daos_file,
	const char* file,
	void* buf,
	size_t size)
{
	d_iov_t iov;
	d_iov_set(&iov, buf, size);

	/* define scatter-gather list for dfs_write */
	d_sg_list_t sgl;
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	sgl.sg_nr_out = 1;

	/* execute read operation */
	daos_size_t got_size;
	int rc = dfs_read(daos_file->dfs, daos_file->obj, &sgl,
			daos_file->offset, &got_size, NULL); 
	if (rc) {
		fprintf(stderr, "dfs_read %s failed (%d %s)\n",
			file, rc, strerror(rc));
		errno = rc;
		return -1;
	}
	/* update file pointer with number of bytes read */
	daos_file->offset += (daos_off_t)got_size;

	return (ssize_t)got_size;
}

static ssize_t
daos_file_read(struct daos_file_t *daos_file, char* file,
		void* buf, size_t size)
{
	ssize_t got_size = 0;
	if (daos_file->type == POSIX) {
		got_size = read(daos_file->fd, buf, size);
	} else if (daos_file->type == DAOS) {
		got_size = daos_read(daos_file, file, buf, size);
	} else {
		fprintf(stderr, "File type not known: %s type=%d\n", file, daos_file->type);
	}
	return got_size;
}

static int
daos_closedir(DIR* _dirp)
{
	struct dfs_daos_t *dirp = (struct dfs_daos_t *)_dirp;
	int rc = dfs_release(dirp->dir);
	if (rc) {
		fprintf(stderr, "dfs_release failed (%d %s)\n",
			rc, strerror(rc));
		errno = rc;
		rc = -1;
	}
	free(dirp);
	return rc;
}

static int
daos_file_closedir(daos_file_t *daos_file, DIR* dirp)
{
	int rc = 0;
	if (daos_file->type == POSIX) {
		rc = closedir(dirp);
	} else if (daos_file->type == DAOS) {
		rc = daos_closedir(dirp);
	} else {
		fprintf(stderr, "File type not known, type=%d\n", daos_file->type);
	}
	return rc;
}

static int
daos_close(daos_file_t *daos_file, const char* file)
{
	int rc = dfs_release(daos_file->obj);
	if (rc) {
		fprintf(stderr, "dfs_close %s failed (%d %s)\n",
			file, rc, strerror(rc));
		errno = rc;
		rc = -1;
	}
	return rc;
}

static int
daos_file_close(daos_file_t *daos_file, const char *file)
{
	int rc = 0;
	if (daos_file->type == POSIX) {
		rc = close(daos_file->fd);
		if (rc == 0)
			daos_file->fd = -1;
	} else if (daos_file->type == DAOS) {
		rc = daos_close(daos_file, file);
		if (rc == 0)
			daos_file->obj = NULL;
	} else {
		fprintf(stderr, "File type not known, file=%s, type=%d\n",
			file, daos_file->type);
	}
	return rc;
}

static int 
fs_copy(daos_file_t *daos_src_file,
		daos_file_t *daos_dst_file,
		const char* dir_name,
		const char* fs_dst_prefix)
{
	DIR *src_dir;
	int rc = 0;

	/* stat the source, and make sure it is a directory  */
	struct stat st_dir_name;
	rc = daos_file_lstat(daos_src_file, dir_name, &st_dir_name);
	if (!S_ISDIR(st_dir_name.st_mode)) {
		fprintf(stderr, "Source is not a directory: %s\n", dir_name);
		D_GOTO(out, rc);
	}

	/* begin by opening source directory */
	src_dir = daos_file_opendir(daos_src_file, dir_name);

	 /* check it was opened. */
	if (!src_dir)
		fprintf(stderr, "Cannot open directory '%s': %s\n",
			dir_name, strerror (errno));

	while (1) {
		struct dirent *entry;
		const char *d_name;

		/* walk source directory */
		entry = daos_file_readdir(daos_src_file, src_dir);
		if (!entry) {
			/* There are no more entries in this directory, so break
			 * out of the while loop. */
			break;
		}

		d_name = entry->d_name;
		char filename[MAX_FILENAME];
		char dst_filename[MAX_FILENAME];
		snprintf(filename, MAX_FILENAME, "%s/%s", dir_name, d_name);

		int path_length = 0;
		if (daos_src_file->type == DAOS && daos_dst_file->type == POSIX)
			path_length = snprintf(dst_filename, MAX_FILENAME, "%s/%s", fs_dst_prefix, filename);
			if (path_length >= MAX_FILENAME) 
				fprintf(stderr, "Path length is too long.\n");

		/* stat the source file */
		struct stat st;
		rc = daos_file_lstat(daos_src_file, filename, &st);
		if (rc)
			fprintf(stderr, "Cannot stat path %s, %s\n",
				d_name, strerror(errno));

		if (S_ISREG(st.st_mode)) {
			if (daos_src_file->type == DAOS && daos_dst_file->type == POSIX) {
				daos_file_open(daos_src_file, filename, O_CREAT | O_RDWR, st.st_mode);
				daos_file_open(daos_dst_file, dst_filename, O_CREAT | O_RDWR, st.st_mode);
			} else {
				daos_file_open(daos_src_file, filename, O_CREAT | O_RDWR, st.st_mode);
				daos_file_open(daos_dst_file, filename, O_CREAT | O_RDWR, st.st_mode);
			}

			/* read from source file, then write to destination file */
			uint64_t total_bytes = st.st_size;
			uint64_t left_to_read = 0;
			uint64_t buf_size = 64*1024*1024;
			void *buf;
			D_ALLOC(buf, buf_size * sizeof(char));
				if (buf == NULL)

					return ENOMEM;
			while (left_to_read < total_bytes) {
				left_to_read = total_bytes - left_to_read;
				if (left_to_read > buf_size)
					left_to_read = buf_size;
				ssize_t bytes_read = daos_file_read(daos_src_file, filename,
								    buf, left_to_read);
				if (bytes_read < 0) {
					fprintf(stderr, "read failed on %s\n", filename);
				}
				size_t bytes_to_write = (size_t) bytes_read;
				ssize_t bytes_written;
				if (daos_src_file->type == DAOS && daos_dst_file->type == POSIX) {
					bytes_written = daos_file_write(daos_dst_file, dst_filename, buf, bytes_to_write);
				} else {
					bytes_written = daos_file_write(daos_dst_file, filename, buf, bytes_to_write);
				}
				if (bytes_written < 0) {
					fprintf(stderr, "write failed on %s\n", filename);
				}
				left_to_read += (uint64_t) bytes_read;
			}
			if (buf != NULL)
				D_FREE(buf);
			daos_file_close(daos_src_file, filename);
			daos_file_close(daos_dst_file, filename);
		} else if (S_ISDIR(st.st_mode)) {
			/* Check that the directory is not "d" or d's parent. */
			if ((strcmp(d_name, "..") != 0) && (strcmp(d_name, ".") != 0)) {
				char path[MAX_FILENAME];
				char dpath[MAX_FILENAME];
				path_length = snprintf(path, MAX_FILENAME, "%s", filename);
				if (daos_src_file->type == DAOS && daos_dst_file->type == POSIX)
					path_length = snprintf(dpath, MAX_FILENAME, "%s", dst_filename);
				if (path_length >= MAX_FILENAME) 
					fprintf(stderr, "Path length is too long.\n");
				/* TODO create dst with write permissions, then after copying
		 		 * make sure to change permissions to match the source */
				if (daos_src_file->type == DAOS && daos_dst_file->type == POSIX) {
					rc = daos_file_mkdir(daos_dst_file, dpath, st.st_mode);
				} else {
					rc = daos_file_mkdir(daos_dst_file, path, st.st_mode);
				}
				/* Recursively call "fs_copy" with the new path. */
				rc = fs_copy(daos_src_file, daos_dst_file, path, fs_dst_prefix);
			}
		} else {
			rc = ENOTSUP;
			fprintf(stderr, "file type is not supported (%d)\n", rc);
			D_GOTO(out, rc);
		}
	}

	/* After going through all the entries, close the directory. */
	if (daos_file_closedir(daos_src_file, src_dir))
		fprintf(stderr, "Could not close '%s': %s\n",
			dir_name, strerror (errno));
out:
	return rc;
}

static int
fs_copy_connect(daos_file_t *daos_src_file,
		   daos_file_t *daos_dst_file,
		   struct cmd_args_s *ap,
		   daos_cont_info_t *src_cont_info,
		   daos_cont_info_t *dst_cont_info,
		   struct duns_attr_t *src_dattr,
		   struct duns_attr_t *dst_dattr)
{
	/* connect to source pool/conts */
	int rc = 0;
	if (daos_uuid_valid(ap->src_p_uuid)) { 
		rc = daos_pool_connect(ap->src_p_uuid, ap->sysname, ap->src_mdsrv,
				DAOS_PC_RW, &ap->pool, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to connect to destination pool: %d\n", rc);
			/* if connection to source fails, do not continue */
			D_GOTO(out, rc);
		} else {
			if (daos_uuid_valid(ap->src_c_uuid)) { 
				rc = daos_cont_open(ap->pool, ap->src_c_uuid, DAOS_COO_RW,
					&ap->cont, src_cont_info, NULL);
				if (rc != 0) {
					fprintf(stderr, "failed to open source cont: %d\n", rc);
					D_GOTO(out, rc);
				}
				rc = dfs_mount(ap->pool, ap->cont, O_RDWR, &daos_src_file->dfs);
				if (rc) {
					fprintf(stderr, "dfs mount on source failed: %s, %d\n",
						ap->src_path, rc);
					D_GOTO(out, rc);
				}
			}
		}
	} else {
		/* Resolve pool, container UUIDs from src path if needed */
        	if (ap->src_path != NULL) {
			rc = duns_resolve_path(ap->src_path, src_dattr);
			if (rc == 0) { 
				ap->type = (*src_dattr).da_type;
				uuid_copy(ap->src_p_uuid, (*src_dattr).da_puuid);
				uuid_copy(ap->src_c_uuid, (*src_dattr).da_cuuid);
				rc = daos_pool_connect(ap->src_p_uuid, ap->sysname, ap->src_mdsrv,
						DAOS_PC_RW, &ap->pool, NULL, NULL);
				if (rc != 0) {
					fprintf(stderr, "failed to connect to destination pool: %d\n", rc);
					D_GOTO(out, rc);
				}
				rc = daos_cont_open(ap->pool, ap->src_c_uuid, DAOS_COO_RW,
					&ap->cont, src_cont_info, NULL);
				rc = dfs_mount(ap->pool, ap->cont, O_RDWR, &(daos_src_file->dfs));
				if (rc) {
					fprintf(stderr, "dfs mount on source failed: %s, %d\n",
						ap->src_path, rc);
					D_GOTO(out, rc);
				}
			} else {
				daos_src_file->type = POSIX;
			}
		}
	}
	/* connect to destination pool/conts */
	if (daos_uuid_valid(ap->dst_p_uuid)) {
			rc = daos_pool_connect(ap->dst_p_uuid, ap->sysname, ap->dst_mdsrv,
			DAOS_PC_RW, &ap->dst_pool, NULL, NULL);
			if (rc != 0) {
				fprintf(stderr, "failed to connect to destination pool: %d\n", rc);
				D_GOTO(out, rc);
			}
		if (daos_uuid_valid(ap->dst_c_uuid)) { 
			rc = daos_cont_open(ap->dst_pool, ap->dst_c_uuid, DAOS_COO_RW,
				&ap->dst_cont, dst_cont_info, NULL);
			rc = dfs_mount(ap->dst_pool, ap->dst_cont, O_RDWR, &(daos_dst_file->dfs));
			if (rc) {
				fprintf(stderr, "dfs mount on destination failed: %s, %d\n",
					ap->dst_path, rc);
				D_GOTO(out, rc);
			}
		}
	} else {
		/* Resolve pool, container UUIDs from dst path if needed */
        	if (ap->dst_path != NULL) {
			rc = duns_resolve_path(ap->dst_path, dst_dattr);
			if (rc == 0) { 
				ap->type = (*dst_dattr).da_type;
				uuid_copy(ap->dst_p_uuid, (*dst_dattr).da_puuid);
				uuid_copy(ap->dst_c_uuid, (*dst_dattr).da_cuuid);
				rc = daos_pool_connect(ap->dst_p_uuid, ap->sysname, ap->dst_mdsrv,
						DAOS_PC_RW, &ap->dst_pool, NULL, NULL);
				if (rc != 0) {
					fprintf(stderr, "failed to connect to destination pool: %d\n", rc);
					D_GOTO(out, rc);
				}
				rc = daos_cont_open(ap->dst_pool, ap->dst_c_uuid, DAOS_COO_RW,
					&ap->dst_cont, dst_cont_info, NULL);
				rc = dfs_mount(ap->dst_pool, ap->dst_cont, O_RDWR, &(daos_dst_file->dfs));
				if (rc) {
					fprintf(stderr, "dfs mount on destination failed: %s, %d\n",
						ap->dst_path, rc);
					D_GOTO(out, rc);
				}
			} else {
				daos_dst_file->type = POSIX;
			}
		}
	}
	/* create DAOS POSIX destination container if it doesn't exist */
	if (daos_dst_file->type == DAOS) {
		if (daos_uuid_valid(ap->src_c_uuid) && !daos_uuid_valid(ap->dst_c_uuid)) {
			/* create POSIX destination container if it doesn't exist */
	    		if (uuid_is_null(ap->c_uuid))
				uuid_generate(ap->dst_c_uuid);
			rc = dfs_cont_create(ap->dst_pool, ap->dst_c_uuid, NULL, &(ap->dst_cont),
						&(daos_dst_file->dfs));
			if (rc != 0) {
				fprintf(stderr, "failed to create container: %s (%d)\n",
					d_errdesc(rc), rc);
				D_GOTO(out, rc);
			}
			fprintf(stdout, "Successfully created container "DF_UUIDF"\n",
				DP_UUID(ap->dst_c_uuid));
		}
	}
out:
	return rc;
}

static void
daos_file_set_defaults(daos_file_t *daos_file)
{
	/* set defaults for daos_file struct */
	daos_file->type	  = DAOS;
	daos_file->fd     = -1; 
	daos_file->offset = 0; 
	daos_file->obj    = NULL; 
	daos_file->dfs    = NULL; 
}

int
fs_copy_hdlr(struct cmd_args_s *ap)
{
	/* TODO: add check to make sure all required arguments are 
	 * provided */
	int rc = 0;
	daos_cont_info_t src_cont_info;
	daos_cont_info_t dst_cont_info;
	struct duns_attr_t dst_dattr = {0};
	struct duns_attr_t src_dattr = {0};

	/* daos_file_t abstraction for POSIX and DFS files */
	daos_file_t daos_src_file;
	daos_file_t daos_dst_file;
	daos_file_set_defaults(&daos_src_file);
	daos_file_set_defaults(&daos_dst_file);
	fs_copy_connect(&daos_src_file, &daos_dst_file, ap,
			   &src_cont_info, &dst_cont_info,
			   &src_dattr, &dst_dattr);
	/* set paths based on file type for source and destination */
	char *name = NULL, *dname = NULL;
	if (daos_src_file.type == POSIX && daos_dst_file.type == DAOS) {
		ap->dst_path = "/";
		parse_filename(ap->src_path, &name, &dname);
		dfs_set_prefix(daos_dst_file.dfs, dname);
		daos_file_mkdir(&daos_dst_file, ap->src_path, O_RDWR);
		rc = fs_copy(&daos_src_file, &daos_dst_file,
				ap->src_path, ap->dst_path);
		if (rc != 0)
			D_GOTO(out, rc);
	} else if (daos_src_file.type == DAOS && daos_dst_file.type == POSIX) {
		parse_filename(ap->dst_path, &name, &dname);
		ap->src_path = "/";
		rc = fs_copy(&daos_src_file, &daos_dst_file,
				ap->src_path, dname);
		if (rc != 0)
			D_GOTO(out, rc);
	} else if (daos_src_file.type == DAOS && daos_dst_file.type == DAOS) {
		ap->src_path = "/";
		ap->dst_path = "/";
		rc = fs_copy(&daos_src_file, &daos_dst_file,
				ap->src_path, dname);
		if (rc != 0)
			D_GOTO(out, rc);
	/* TODO: handle POSIX->POSIX case here */
	} else {
		fprintf(stderr, "Regular POSIX to POSIX copies are not supported\n");
		if (rc != 0)
			D_GOTO(out, rc);
	}

	/* umount dfs, close conts, and disconnect pools */
	if (daos_src_file.type == DAOS) {
		rc = dfs_umount(daos_src_file.dfs);
		if (rc != 0)
			fprintf(stderr, "failed to unmount source (%d)\n", rc);
		rc = daos_cont_close(ap->cont, NULL);
		if (rc != 0)
			fprintf(stderr, "failed to close source container (%d)\n", rc);
		rc = daos_pool_disconnect(ap->pool, NULL);
		if (rc != 0)
			fprintf(stderr, "failed to disconnect from source pool "DF_UUIDF
				": %s (%d)\n", DP_UUID(ap->src_p_uuid), d_errdesc(rc), rc);
	}
	if (daos_dst_file.type == DAOS) {
		rc = dfs_umount(daos_dst_file.dfs);
		if (rc != 0)
			fprintf(stderr, "failed to unmount destination (%d)\n", rc);
		rc = daos_cont_close(ap->dst_cont, NULL);
		if (rc != 0)
			fprintf(stderr, "failed to close destination container (%d)\n", rc);
		rc = daos_pool_disconnect(ap->dst_pool, NULL);
		if (rc != 0)
			fprintf(stderr, "failed to disconnect from destination pool "DF_UUIDF
				": %s (%d)\n", DP_UUID(ap->dst_p_uuid), d_errdesc(rc), rc);
	}
out:
	return rc;
}

static int
print_acl(FILE *outstream, daos_prop_t *acl_prop, bool verbose)
{
	int			rc = 0;
	struct daos_prop_entry	*entry;
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
				shard->os_ranks[j]);
	}

	daos_obj_layout_free(layout);

out:
	return rc;
}
