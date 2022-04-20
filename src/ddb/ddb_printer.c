/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ddb_printer.h"

#define DF_IDX "[%d]"
#define DP_IDX(idx) idx

static void
print_indent(struct ddb_ctx *ctx, int c)
{
	int i;

	for (i = 0; i < c; i++)
		ddb_print(ctx, "\t");
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
	ddb_printf(ctx, DF_IDX" '"DF_OID"' (class: %s, type: %s, groups: %d)\n",
		   DP_IDX(obj->ddbo_idx),
		   DP_OID(obj->ddbo_oid),
		   obj->ddbo_obj_class_name,
		   obj->ddbo_otype_str,
		   obj->ddbo_nr_grps);
}

static bool
can_print(struct ddb_key *key)
{
	char	*str = key->ddbk_key.iov_buf;
	uint32_t len = key->ddbk_key.iov_len;
	int	 i;

	for (i = 0 ; i < len ; i++) {
		if (str[i] == '\0')
			return true;
		if (!isprint(str[i]))
			return false;
	}
	return true;
}

void
ddb_print_key(struct ddb_ctx *ctx, struct ddb_key *key, uint32_t indent)
{
	const uint32_t	buf_len = 64;
	char		buf[buf_len];
	uint32_t	str_len = min(100, key->ddbk_key.iov_len);

	memset(buf, 0, buf_len);
	print_indent(ctx, indent);
	if (can_print(key)) {
		ddb_printf(ctx, DF_IDX" '%.*s' (%lu)\n",
			   DP_IDX(key->ddbk_idx),
			   str_len,
			   (char *)key->ddbk_key.iov_buf,
			   key->ddbk_key.iov_len);
		return;
	}

	switch (key->ddbk_key.iov_len) {
	case sizeof(uint8_t):
		sprintf(buf, "uint8:0x%x", ((uint8_t *)key->ddbk_key.iov_buf)[0]);
		break;
	case sizeof(uint16_t):
		sprintf(buf, "uint16:0x%04hx", ((uint16_t *)key->ddbk_key.iov_buf)[0]);
		break;
	case sizeof(uint32_t):
		sprintf(buf, "uint32:0x%x", ((uint32_t *)key->ddbk_key.iov_buf)[0]);
		break;
	case sizeof(uint64_t):
		sprintf(buf, "uint64:0x%lx", ((uint64_t *)key->ddbk_key.iov_buf)[0]);
		break;
	default:
	{
		int i;

		sprintf(buf, "bin(%lu):0x", key->ddbk_key.iov_len);

		for (i = 0; i < key->ddbk_key.iov_len; i++) {
			sprintf(buf, "%s%02x", buf, ((uint8_t *)key->ddbk_key.iov_buf)[i]);

			if (key->ddbk_key.iov_len > buf_len
			    && i == (buf_len / 4) - 12) {
				sprintf(buf, "%s...", buf);
				i = key->ddbk_key.iov_len - ((buf_len / 4) - 10);
			}
		}
	}
	}
	ddb_printf(ctx, DF_IDX" '{%s}'\n", DP_IDX(key->ddbk_idx), buf);
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
	ddb_printf(ctx, "Status: %s (%d)\n", entry->die_status_str, entry->die_status);
	ddb_printf(ctx, "Epoch: %lu\n", entry->die_epoch);
	ddb_printf(ctx, "Txn ID: %d\n", entry->die_tx_id);
}

void
ddb_print_dtx_committed(struct ddb_ctx *ctx, struct dv_dtx_committed_entry *entry)
{
	ddb_printf(ctx, "UUID: "DF_UUIDF"\n", DP_UUID(entry->ddtx_uuid));
	ddb_printf(ctx, "Epoch: "DF_U64"\n", entry->ddtx_epoch);
	ddb_printf(ctx, "Exist: "DF_BOOL"\n", DP_BOOL(entry->ddtx_exist));
	ddb_printf(ctx, "Invalid: "DF_BOOL"\n", DP_BOOL(entry->ddtx_invalid));
}

void
ddb_print_dtx_active(struct ddb_ctx *ctx, struct dv_dtx_active_entry *entry)
{
	ddb_printf(ctx, "UUID: "DF_UUIDF"\n", DP_UUID(entry->ddtx_uuid));
	ddb_printf(ctx, "Epoch: "DF_U64"\n", entry->ddtx_epoch);
	ddb_printf(ctx, "Exist: "DF_BOOL"\n", DP_BOOL(entry->ddtx_exist));
	ddb_printf(ctx, "Invalid: "DF_BOOL"\n", DP_BOOL(entry->ddtx_invalid));
	ddb_printf(ctx, "Reindex: "DF_BOOL"\n", DP_BOOL(entry->ddtx_reindex));
	ddb_printf(ctx, "Handle Time: "DF_U64"\n", entry->ddtx_handle_time);
	ddb_printf(ctx, "Oid Cnt: %d\n", entry->ddtx_oid_cnt);
	ddb_printf(ctx, "Start Time: "DF_U64"\n", entry->ddtx_start_time);
	ddb_printf(ctx, "Committable: "DF_BOOL"\n", DP_BOOL(entry->ddtx_committable));
	ddb_printf(ctx, "Committed: "DF_BOOL"\n", DP_BOOL(entry->ddtx_committed));
	ddb_printf(ctx, "Aborted: "DF_BOOL"\n", DP_BOOL(entry->ddtx_aborted));
	ddb_printf(ctx, "Maybe Shared: "DF_BOOL"\n", DP_BOOL(entry->ddtx_maybe_shared));
	ddb_printf(ctx, "Prepared: "DF_BOOL"\n", DP_BOOL(entry->ddtx_prepared));
	ddb_printf(ctx, "Grp Cnt: %d\n", entry->ddtx_grp_cnt);
	ddb_printf(ctx, "Ver: %d\n", entry->ddtx_ver);
	ddb_printf(ctx, "Rec Cnt: %d\n", entry->ddtx_rec_cnt);
	ddb_printf(ctx, "Mbs Flags: %d\n", entry->ddtx_mbs_flags);
	ddb_printf(ctx, "Flags: %d\n", entry->ddtx_flags);
	ddb_printf(ctx, "Oid: "DF_UOID"\n", DP_UOID(entry->ddtx_oid));
}
