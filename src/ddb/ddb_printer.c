/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ddb_printer.h"

static void
print_indent(struct ddb_ctx *ctx, int c)
{
	int i;

	for (i = 0; i < c; i++)
		ddb_print(ctx, " ");
}

bool
ddb_can_print(d_iov_t *iov)
{
	char	*str = iov->iov_buf;
	uint32_t len = iov->iov_len;
	int	 i;

	for (i = 0 ; i < len ; i++) {
		if (str[i] == '\0')
			return true;
		if (!isprint(str[i]) && str[i] != '\n' && str[i] != '\r')
			return false;
	}
	return true;
}

/*
 * Converts contents of an iov to something that is more printable.
 *
 * Returns number of characters that would have been written if buf_len was long
 * enough, not including null terminator
 */
int
ddb_iov_to_printable_buf(d_iov_t *iov, char buf[], uint32_t buf_len)
{
	if (iov->iov_len == 0 || iov->iov_buf == NULL)
		return 0;

	if (ddb_can_print(iov))
		return snprintf(buf, buf_len, "%.*s", (int)iov->iov_len, (char *)iov->iov_buf);

	switch (iov->iov_len) {
	case sizeof(uint8_t):
		return snprintf(buf, buf_len, "uint8:0x%x", ((uint8_t *)iov->iov_buf)[0]);
	case sizeof(uint16_t):
		return snprintf(buf, buf_len, "uint16:0x%04hx", ((uint16_t *)iov->iov_buf)[0]);
	case sizeof(uint32_t):
		return snprintf(buf, buf_len, "uint32:0x%x", ((uint32_t *)iov->iov_buf)[0]);
	case sizeof(uint64_t):
		return snprintf(buf, buf_len, "uint64:0x%lx", ((uint64_t *)iov->iov_buf)[0]);
	default:
	{
		char		tmp_buf[32];
		uint32_t	new_len;
		uint32_t	result = 0;
		int		i;

		result += snprintf(buf, buf_len, "bin(%lu):0x", iov->iov_len);

		for (i = 0; i < iov->iov_len; i++) {
			new_len = snprintf(tmp_buf, ARRAY_SIZE(tmp_buf), "%02x",
					   ((uint8_t *)iov->iov_buf)[i]);
			if (new_len + result > buf_len) {
				/* Buffer not big enough */
				result += new_len;
			} else {
				result += sprintf(buf + result, "%s", tmp_buf);
			}
		}

		if (result > buf_len) {
			buf[buf_len - 1] = '\0';
			buf[buf_len - 2] = '.';
			buf[buf_len - 3] = '.';
			buf[buf_len - 4] = '.';

		}
		return result;
	}
	}
}

void
ddb_print_cont(struct ddb_ctx *ctx, struct ddb_cont *cont)
{
	ddb_printf(ctx, DF_IDX" "DF_UUIDF"\n", DP_IDX(cont->ddbc_idx),
		   DP_UUID(cont->ddbc_cont_uuid));
}

void
ddb_print_obj(struct ddb_ctx *ctx, struct ddb_obj *obj, uint32_t indent)
{
	print_indent(ctx, indent);
	ddb_printf(ctx, DF_IDX" '"DF_OID"' (type: %s, groups: %d)\n",
		   DP_IDX(obj->ddbo_idx),
		   DP_OID(obj->ddbo_oid),
		   obj->ddbo_otype_str,
		   obj->ddbo_nr_grps);
}

void
ddb_print_key(struct ddb_ctx *ctx, struct ddb_key *key, uint32_t indent)
{
	const uint32_t	buf_len = 64;
	char		buf[buf_len];

	memset(buf, 0, buf_len);

	ddb_iov_to_printable_buf(&key->ddbk_key, buf, buf_len);

	print_indent(ctx, indent);
	if (ddb_can_print(&key->ddbk_key)) {
		ddb_printf(ctx, DF_IDX" '%s' (%lu)%s\n",
			   DP_IDX(key->ddbk_idx),
			   buf,
			   key->ddbk_key.iov_len,
			   key->ddbk_child_type == VOS_ITER_SINGLE ? " (SV)" :
			   key->ddbk_child_type == VOS_ITER_RECX ? " (ARRAY)" : "");
		return;
	}

	ddb_printf(ctx, DF_IDX" {%s}%s\n", DP_IDX(key->ddbk_idx), buf,
		   key->ddbk_child_type == VOS_ITER_SINGLE ? " (SV)" :
		   key->ddbk_child_type == VOS_ITER_RECX ? " (ARRAY)" : "");
}

void
ddb_print_sv(struct ddb_ctx *ctx, struct ddb_sv *sv, uint32_t indent)
{
	print_indent(ctx, indent);
	ddb_printf(ctx, DF_IDX" Single Value (Length: "DF_U64" bytes)\n",
		   sv->ddbs_idx,
		   sv->ddbs_record_size);
}

void
ddb_print_array(struct ddb_ctx *ctx, struct ddb_array *array, uint32_t indent)
{
	print_indent(ctx, indent);
	ddb_printf(ctx, DF_IDX" Array Value (Length: "DF_U64" records, Record Indexes: "
			"{"DF_U64"-"DF_U64"}, Record Size: "DF_U64")\n",
		   array->ddba_idx,
		   array->ddba_recx.rx_nr,
		   array->ddba_recx.rx_idx,
		   array->ddba_recx.rx_idx + array->ddba_recx.rx_nr - 1,
		   array->ddba_record_size);
}

void
ddb_print_path(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp, uint32_t indent)
{
	print_indent(ctx, indent);
	itp_print_full(ctx, itp);
	ddb_print(ctx, "\n");
}

void
ddb_bytes_hr(uint64_t bytes, char *buf, uint32_t buf_len)
{
	int			i = 0;
	static const char	*const units[] = {"B", "KB", "MB", "GB", "TB"};

	while (bytes >= 1024) {
		bytes /= 1024;
		i++;
	}
	snprintf(buf, buf_len, "%lu%s", bytes, units[i]);
}

static void
print_bytes(struct ddb_ctx *ctx, char *prefix, uint64_t bytes)
{
	char buf[32];

	ddb_bytes_hr(bytes, buf, ARRAY_SIZE(buf));
	ddb_printf(ctx, "%s: %s\n", prefix, buf);
}

void
ddb_print_superblock(struct ddb_ctx *ctx, struct ddb_superblock *sb)
{
	ddb_printf(ctx, "Pool UUID: "DF_UUIDF"\n", DP_UUID(sb->dsb_id));
	ddb_printf(ctx, "Format Version: %d\n", sb->dsb_durable_format_version);
	ddb_printf(ctx, "Containers: %lu\n", sb->dsb_cont_nr);
	print_bytes(ctx, "SCM Size", sb->dsb_scm_sz);
	print_bytes(ctx, "NVME Size", sb->dsb_nvme_sz);
	print_bytes(ctx, "Block Size", sb->dsb_blk_sz);
	ddb_printf(ctx, "Reserved Blocks: %d\n", sb->dsb_hdr_blks);
	print_bytes(ctx, "Block Device Capacity", sb->dsb_tot_blks);
}

void
ddb_print_ilog_entry(struct ddb_ctx *ctx, struct ddb_ilog_entry *entry)
{
	ddb_printf(ctx, "Index: %d\n", entry->die_idx);
	ddb_printf(ctx, "\tStatus: %s (%d)\n", entry->die_status_str, entry->die_status);
	ddb_printf(ctx, "\tEpoch: %lu\n", entry->die_epoch);
	ddb_printf(ctx, "\tTxn ID: %d\n", entry->die_tx_id);
}

void
ddb_print_dtx_committed(struct ddb_ctx *ctx, struct dv_dtx_committed_entry *entry)
{
	ddb_printf(ctx, "ID: "DF_DTIF"\n", DP_DTI(&entry->ddtx_id));
	ddb_printf(ctx, "\tEpoch: "DF_U64"\n", entry->ddtx_epoch);
}

void
ddb_print_dtx_active(struct ddb_ctx *ctx, struct dv_dtx_active_entry *entry)
{
	ddb_printf(ctx, "ID: "DF_DTIF"\n", DP_DTI(&entry->ddtx_id));
	ddb_printf(ctx, "\tEpoch: "DF_U64"\n", entry->ddtx_epoch);
	ddb_printf(ctx, "\tHandle Time: "DF_U64"\n", entry->ddtx_handle_time);
	ddb_printf(ctx, "\tGrp Cnt: %d\n", entry->ddtx_grp_cnt);
	ddb_printf(ctx, "\tVer: %d\n", entry->ddtx_ver);
	ddb_printf(ctx, "\tRec Cnt: %d\n", entry->ddtx_rec_cnt);
	ddb_printf(ctx, "\tMbs Flags: %d\n", entry->ddtx_mbs_flags);
	ddb_printf(ctx, "\tFlags: %d\n", entry->ddtx_flags);
	ddb_printf(ctx, "\tOid: "DF_UOID"\n", DP_UOID(entry->ddtx_oid));
}
