/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos/debug.h>
#include <daos/object.h>

#include "daos_types.h"
#include "daos_api.h"
#include "daos_fs.h"
#include "daos_uns.h"

#include "daos_hdlr.h"

static int
parse_acl_file(const char *path, struct daos_acl **acl);

/* TODO: implement these pool op functions
 * int pool_stat_hdlr(struct cmd_args_s *ap);
 */

int
pool_get_prop_hdlr(struct cmd_args_s *ap)
{
	daos_prop_t			*prop_query;
	struct daos_prop_entry		*entry;
	int				rc = 0;
	int				rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_GET_PROP);

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       ap->mdsrv, DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	prop_query = daos_prop_alloc(0);
	if (prop_query == NULL)
		D_GOTO(out_disconnect, rc = -DER_NOMEM);

	rc = daos_pool_query(ap->pool, NULL, NULL, prop_query, NULL);
	if (rc != 0) {
		fprintf(stderr, "pool query failed for properties: %d\n", rc);
		D_GOTO(out_disconnect, rc);
	}

	D_PRINT("Pool properties :\n");

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_LABEL);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "label property not found\n");
		D_GOTO(out_disconnect, rc = -DER_INVAL);
	}
	D_PRINT("label -> %s\n", entry->dpe_str);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SPACE_RB);
	if (entry == NULL) {
		fprintf(stderr, "rebuild space ratio property not found\n");
		D_GOTO(out_disconnect, rc = -DER_INVAL);
	}
	D_PRINT("rebuild space ratio -> "DF_U64"\n", entry->dpe_val);

	/* not set properties should get default value */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SELF_HEAL);
	if (entry == NULL) {
		fprintf(stderr, "self-heal property not found\n");
		D_GOTO(out_disconnect, rc = -DER_INVAL);
	}
	D_PRINT("self-heal -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_RECLAIM);
	if (entry == NULL) {
		fprintf(stderr, "reclaim property not found\n");
		D_GOTO(out_disconnect, rc = -DER_INVAL);
	}
	D_PRINT("reclaim -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL) {
		fprintf(stderr, "acl property not found\n");
		D_GOTO(out_disconnect, rc = -DER_INVAL);
	}
	D_PRINT("acl ->\n");
	daos_acl_dump(entry->dpe_val_ptr);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "owner property not found\n");
		D_GOTO(out_disconnect, rc = -DER_INVAL);
	}
	D_PRINT("owner -> %s\n", entry->dpe_str);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "owner-group property not found\n");
		D_GOTO(out_disconnect, rc = -DER_INVAL);
	}
	D_PRINT("owner-group -> %s\n", entry->dpe_str);

out_disconnect:
	daos_prop_free(prop_query);

	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);

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
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	value_size = strlen(ap->value_str);
	rc = daos_pool_set_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str,
				(const void * const*)&ap->value_str,
				(const size_t *)&value_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "pool set attr failed: %d\n", rc);
		D_GOTO(out_disconnect, rc);
	}

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);

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
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	/* evaluate required size to get attr */
	attr_size = 0;
	rc = daos_pool_get_attr(ap->pool, 1,
				(const char * const*)&ap->attrname_str, NULL,
				&attr_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "pool get attr failed: %d\n", rc);
		D_GOTO(out_disconnect, rc);
	}

	D_PRINT("Pool's %s attribute value: ", ap->attrname_str);
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
		fprintf(stderr, "pool get attr failed: %d\n", rc);
		D_GOTO(out_disconnect, rc);
	}

	if (expected_size < attr_size)
		fprintf(stderr, "size required to get attributes has raised, value has been truncated\n");
	D_PRINT("%s\n", buf);

out_disconnect:
	if (buf != NULL)
		D_FREE(buf);

	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);

	if (rc == 0)
		rc = rc2;
out:
	return rc;

}

int
pool_list_attrs_hdlr(struct cmd_args_s *ap)
{
	size_t				 total_size, expected_size, cur = 0,
					 len;
	char				*buf = NULL;
	int				rc = 0;
	int				rc2;

	assert(ap != NULL);
	assert(ap->p_op == POOL_LIST_ATTRS);

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       ap->mdsrv, DAOS_PC_RO, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	/* evaluate required size to get all attrs */
	total_size = 0;
	rc = daos_pool_list_attr(ap->pool, NULL, &total_size, NULL);
	if (rc != 0) {
		fprintf(stderr, "pool list attr failed: %d\n", rc);
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
		fprintf(stderr, "pool list attr failed: %d\n", rc);
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
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);

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
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	/* Issue first API call to get current number of containers */
	rc = daos_pool_list_cont(ap->pool, &ncont, NULL /* cbuf */,
				 NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "pool get ncont failed: %d\n", rc);
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
	if (conts == NULL)
		D_GOTO(out_disconnect, rc = -DER_NOMEM);

	rc = daos_pool_list_cont(ap->pool, &ncont, conts, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "pool list containers failed: %d\n", rc);
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
	if (rc2 != 0)
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);

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
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pinfo.pi_bits = DPI_ALL;
	rc = daos_pool_query(ap->pool, NULL, &pinfo, NULL, NULL);
	if (rc != 0) {
		fprintf(stderr, "pool query failed: %d\n", rc);
		D_GOTO(out_disconnect, rc);
	}
	D_PRINT("Pool "DF_UUIDF", ntarget=%u, disabled=%u\n",
		DP_UUID(pinfo.pi_uuid), pinfo.pi_ntargets,
		pinfo.pi_ndisabled);

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
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);

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
 * int cont_set_prop_hdlr()
 * int cont_del_attr_hdlr()
 * int cont_rollback_hdlr()
 */

int
cont_list_snaps_hdlr(struct cmd_args_s *ap)
{
	daos_epoch_t *buf = NULL;
	daos_anchor_t anchor;
	int rc, i, snaps_count, expected_count;

	/* evaluate size for listing */
	snaps_count = 0;
	memset(&anchor, 0, sizeof(anchor));
	rc = daos_cont_list_snap(ap->cont, &snaps_count, NULL, NULL, &anchor,
				 NULL);
	if (rc != 0) {
		fprintf(stderr, "cont list snaps failed: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_PRINT("Container's snapshots :\n");
	if (daos_anchor_is_eof < 0) {
		fprintf(stderr, "invalid number of snapshots returned\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (snaps_count == 0) {
		D_PRINT("no snapshots\n");
		D_GOTO(out, rc);
	}

	D_ALLOC_ARRAY(buf, snaps_count);
	if (buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	expected_count = snaps_count;
	memset(&anchor, 0, sizeof(anchor));
	rc = daos_cont_list_snap(ap->cont, &snaps_count, buf, NULL, &anchor,
				 NULL);
	if (rc != 0) {
		fprintf(stderr, "cont list snaps failed: %d\n", rc);
		D_GOTO(out, rc);
	}
	if (expected_count < snaps_count)
		fprintf(stderr, "size required to gather all snapshots has raised, list has been truncated\n");

	for (i = 0; i < min(expected_count, snaps_count); i++)
		D_PRINT(DF_U64" ", buf[i]);
	D_PRINT("\n");

out:
	if (buf != NULL)
		D_FREE(buf);

	return rc;
}

int
cont_create_snap_hdlr(struct cmd_args_s *ap)
{
	int rc;

	rc = daos_cont_create_snap(ap->cont, &ap->epc, ap->snapname_str, NULL);
	if (rc != 0) {
		fprintf(stderr, "cont create snap failed: %d\n", rc);
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
		fprintf(stderr, "cont destroy snap failed: %d\n", rc);
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
		fprintf(stderr, "cont set attr failed: %d\n", rc);
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
		fprintf(stderr, "cont get attr failed: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_PRINT("Container's %s attribute value: ", ap->attrname_str);
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
		fprintf(stderr, "cont get attr failed: %d\n", rc);
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
		fprintf(stderr, "cont list attr failed: %d\n", rc);
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
		fprintf(stderr, "cont list attr failed: %d\n", rc);
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

/* cont_get_prop_hdlr() - get container properties */
int
cont_get_prop_hdlr(struct cmd_args_s *ap)
{
	daos_prop_t		*prop_query;
	struct daos_prop_entry	*entry;
	char			type[10] = {};
	int			rc = 0;

	prop_query = daos_prop_alloc(0);
	if (prop_query == NULL)
		return -DER_NOMEM;

	rc = daos_cont_query(ap->cont, NULL, prop_query, NULL);
	if (rc) {
		fprintf(stderr, "Container query failed, result: %d\n", rc);
		D_GOTO(err_out, rc);
	}

	D_PRINT("Container properties :\n");

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_LABEL);
	if (entry == NULL || entry->dpe_str == NULL) {
		fprintf(stderr, "label property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("label -> %s\n", entry->dpe_str);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL) {
		fprintf(stderr, "layout type property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	daos_unparse_ctype(entry->dpe_val, type);
	D_PRINT("layout type -> "DF_U64"/%s\n", entry->dpe_val, type);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_LAYOUT_VER);
	if (entry == NULL) {
		fprintf(stderr, "layout version property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("layout version -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM);
	if (entry == NULL) {
		fprintf(stderr, "checksum type property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("checksum type -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM_CHUNK_SIZE);
	if (entry == NULL) {
		fprintf(stderr, "checksum chunk-size property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("checksum chunk-size -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM_SERVER_VERIFY);
	if (entry == NULL) {
		fprintf(stderr, "checksum verification on server property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("checksum verification on server -> "DF_U64"\n",
		entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_REDUN_FAC);
	if (entry == NULL) {
		fprintf(stderr, "redundancy factor property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("redundancy factor -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_REDUN_LVL);
	if (entry == NULL) {
		fprintf(stderr, "redundancy level property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("redundancy level -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_SNAPSHOT_MAX);
	if (entry == NULL) {
		fprintf(stderr, "max snapshots property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("max snapshots -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL) {
		fprintf(stderr, "acl property not found\n");
		/* not an error */
	} else {
		D_PRINT("acl ->\n");
		daos_acl_dump(entry->dpe_val_ptr);
	}

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_COMPRESS);
	if (entry == NULL) {
		fprintf(stderr, "compression type property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("compression type -> "DF_U64"\n", entry->dpe_val);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_ENCRYPT);
	if (entry == NULL) {
		fprintf(stderr, "encryption type property not found\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}
	D_PRINT("encryption type -> "DF_U64"\n", entry->dpe_val);

err_out:
	daos_prop_free(prop_query);
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
		fprintf(stderr, "failed to create container: %d\n", rc);
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
		fprintf(stderr, "duns_create_path() error: rc=%d\n", rc);
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
				fprintf(stderr, "dfs_mount failed (%d)\n", rc);
				D_GOTO(err_out, rc);
			}

			dfs_query(dfs, &attr);
			daos_oclass_id2name(attr.da_oclass_id, oclass);
			printf("Object Class:\t%s\n", oclass);
			printf("Chunk Size:\t%zu\n", attr.da_chunk_size);

			rc = dfs_umount(dfs);
			if (rc) {
				fprintf(stderr, "dfs_umount failed (%d)\n", rc);
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
			fprintf(stderr, "duns_destroy_path() failed %s (%d)\n",
				ap->path, rc);
		else
			fprintf(stdout, "Successfully destroyed path %s\n",
				ap->path);
		return rc;
	}

	/* TODO: when API supports, change arg 3 to ap->force_destroy. */
	rc = daos_cont_destroy(ap->pool, ap->c_uuid, 1, NULL);
	if (rc != 0)
		fprintf(stderr, "failed to destroy container: %d\n", rc);
	else
		fprintf(stdout, "Successfully destroyed container "
				DF_UUIDF"\n", DP_UUID(ap->c_uuid));

	return rc;
}

static int
print_acl(FILE *outstream, daos_prop_t *acl_prop, bool verbose)
{
	int			rc = 0;
	struct daos_prop_entry	*entry;
	struct daos_acl		*acl = NULL;
	char			**acl_str = NULL;
	size_t			nr_acl_str;
	char			verbose_str[DAOS_ACL_MAX_ACE_STR_LEN * 2];
	size_t			i;

	/*
	 * Validate the ACL before we start printing anything out.
	 */
	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_ACL);
	if (entry != NULL && entry->dpe_val_ptr != NULL) {
		acl = entry->dpe_val_ptr;
		rc = daos_acl_to_strs(acl, &acl_str, &nr_acl_str);
		if (rc != 0) {
			fprintf(stderr,
				"Invalid ACL cannot be displayed\n");
			return rc;
		}
	}

	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_OWNER);
	if (entry != NULL && entry->dpe_str != NULL)
		fprintf(outstream, "# Owner: %s\n", entry->dpe_str);

	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_OWNER_GROUP);
	if (entry != NULL && entry->dpe_str != NULL)
		fprintf(outstream, "# Owner-Group: %s\n", entry->dpe_str);

	fprintf(outstream, "# Entries:\n");

	if (acl == NULL || acl->dal_len == 0) {
		fprintf(outstream, "#   None\n");
		return 0;
	}

	for (i = 0; i < nr_acl_str; i++) {
		if (verbose) {
			rc = daos_ace_str_get_verbose(acl_str[i], verbose_str,
						      sizeof(verbose_str));
			/*
			 * If the ACE is invalid, we'll still print it out -
			 * we just can't parse it to any helpful verbose string.
			 */
			if (rc != -DER_INVAL)
				fprintf(outstream, "# %s\n", verbose_str);
		}
		fprintf(outstream, "%s\n", acl_str[i]);
	}

	return 0;
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
		fprintf(stderr, "failed to get ACL for container: %d\n", rc);
	} else {
		rc = print_acl(outstream, prop, ap->verbose);
		if (ap->outfile)
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
				trimmed, d_errstr(rc), rc);
			D_GOTO(parse_err, rc);
		}

		rc = daos_acl_add_ace(&tmp_acl, ace);
		daos_ace_free(ace);
		if (rc != 0) {
			fprintf(stderr, "Error parsing ACL file: %s (%d)\n",
				d_errstr(rc), rc);
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
		fprintf(stderr,
			"failed to overwrite ACL for container: %d\n", rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(stderr,
			"overwrite appeared to succeed, but cannot fetch ACL "
			"for confirmation: %d\n", rc);
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
			fprintf(stderr, "failed to parse entry: %d\n", rc);
			return rc;
		}

		acl = daos_acl_create(&ace, 1);
		daos_ace_free(ace);
		if (acl == NULL) {
			fprintf(stderr, "failed to make ACL from entry: %d\n",
				rc);
			return rc;
		}
	}

	rc = daos_cont_update_acl(ap->cont, acl, NULL);
	daos_acl_free(acl);
	if (rc != 0) {
		fprintf(stderr,
			"failed to update ACL for container: %d\n", rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(stderr,
			"update appeared to succeed, but cannot fetch ACL "
			"for confirmation: %d\n", rc);
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
		fprintf(stderr, "unable to parse principal string '%s': %d\n",
			ap->principal, rc);
		return rc;
	}

	rc = daos_cont_delete_acl(ap->cont, type, name, NULL);
	D_FREE(name);
	if (rc != 0) {
		fprintf(stderr,
			"failed to delete ACL entry for container: %d\n", rc);
		return rc;
	}

	rc = daos_cont_get_acl(ap->cont, &prop_out, NULL);
	if (rc != 0) {
		fprintf(stderr,
			"delete appeared to succeed, but cannot fetch ACL "
			"for confirmation: %d\n", rc);
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
		fprintf(stderr,
			"failed to set owner for container: %d\n", rc);
		return rc;
	}

	fprintf(stdout, "successfully updated owner for container\n");
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
		fprintf(stderr, "daos_obj_layout_get failed, rc: %d\n", rc);
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
