/* Automatically generated with structures
 * expanded from CRT_RPC_DECLARE() macros
 *
 * Copyright (C) 2016-2020 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 * 4. All publications or advertising materials mentioning features or use of
 *    this software are asked, but not required, to acknowledge that it was
 *    developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

struct crt_ctl_ep_ls_in {
	crt_group_id_t cel_grp_id;
	d_rank_t cel_rank;
};

struct crt_ctl_ep_ls_out {
	d_iov_t cel_addr_str;
	int32_t cel_ctx_num;
	int32_t cel_rc;
};

struct crt_ctl_fi_attr_set_in {
	uint32_t fa_fault_id;
	uint32_t fa_interval;
	uint64_t fa_max_faults;
	uint32_t fa_err_code;
	uint32_t fa_probability_x;
	uint32_t fa_probability_y;
	d_string_t fa_argument;
};

struct crt_ctl_fi_attr_set_out {
	int32_t fa_ret;
};

struct crt_ctl_fi_toggle_in {
	_Bool op;
};

struct crt_ctl_fi_toggle_out {
	int32_t rc;
};

struct crt_ctl_get_host_in {
	crt_group_id_t cel_grp_id;
	d_rank_t cel_rank;
};

struct crt_ctl_get_host_out {
	d_iov_t cgh_hostname;
	int32_t cgh_rc;
};

struct crt_ctl_get_pid_in {
	crt_group_id_t cel_grp_id;
	d_rank_t cel_rank;
};

struct crt_ctl_get_pid_out {
	int32_t cgp_pid;
	int32_t cgp_rc;
};

struct crt_ctl_get_uri_cache_in {
	crt_group_id_t cel_grp_id;
	d_rank_t cel_rank;
};

struct crt_ctl_get_uri_cache_out {
	struct {
	uint64_t ca_count;
	struct crt_grp_cache *ca_arrays;
	} cguc_grp_cache;
	int32_t cguc_rc;
};

struct crt_iv_fetch_in {
	uint32_t ifi_ivns_id;
	uint32_t pad1;
	crt_group_id_t ifi_ivns_group;
	d_iov_t ifi_key;
	crt_bulk_t ifi_value_bulk;
	int32_t ifi_class_id;
	d_rank_t ifi_root_node;
};

struct crt_iv_fetch_out {
	int32_t ifo_rc;
};

struct crt_iv_sync_in {
	uint32_t ivs_ivns_id;
	uint32_t pad1;
	crt_group_id_t ivs_ivns_group;
	d_iov_t ivs_key;
	d_iov_t ivs_sync_type;
	uint32_t ivs_class_id;
};

struct crt_iv_sync_out {
	int32_t rc;
};

struct crt_iv_update_in {
	uint32_t ivu_ivns_id;
	uint32_t pad1;
	crt_group_id_t ivu_ivns_group;
	d_iov_t ivu_key;
	d_iov_t ivu_sync_type;
	crt_bulk_t ivu_iv_value_bulk;
	d_rank_t ivu_root_node;
	d_rank_t ivu_caller_node;
	uint32_t ivu_class_id;
	uint32_t padding;
};

struct crt_iv_update_out {
	uint64_t rc;
};

struct crt_proto_query_in {
	d_iov_t pq_ver;
	int32_t pq_ver_count;
	uint32_t pq_base_opc;
};

struct crt_proto_query_out {
	uint32_t pq_ver;
	int32_t pq_rc;
};

struct crt_rpc_swim_in {
	swim_id_t src;
	struct {
	uint64_t ca_count;
	struct swim_member_update *ca_arrays;
	} upds;
};

struct crt_rpc_swim_wack_in {
	swim_id_t src;
	struct {
	uint64_t ca_count;
	struct swim_member_update *ca_arrays;
	} upds;
};

struct crt_st_both_bulk_in {
	uint64_t unused1;
	crt_bulk_t unused2;
};

struct crt_st_both_iov_in {
	uint64_t unused1;
	d_iov_t unused2;
};

struct crt_st_both_iov_out {
	d_iov_t unused1;
};

struct crt_st_close_session_in {
	uint64_t unused1;
};

struct crt_st_open_session_in {
	uint32_t unused1;
	uint32_t unused2;
	uint32_t unused3;
	uint32_t unused4;
};

struct crt_st_open_session_out {
	uint64_t unused1;
};

struct crt_st_send_bulk_reply_iov_in {
	uint64_t unused1;
	crt_bulk_t unused2;
};

struct crt_st_send_bulk_reply_iov_out {
	d_iov_t unused1;
};

struct crt_st_send_id_reply_iov_in {
	uint64_t unused1;
};

struct crt_st_send_id_reply_iov_out {
	d_iov_t unused1;
};

struct crt_st_send_iov_reply_bulk_in {
	uint64_t unused1;
	d_iov_t unused2;
	crt_bulk_t unused3;
};

struct crt_st_send_iov_reply_empty_in {
	uint64_t unused1;
	d_iov_t unused2;
};

struct crt_st_start_in {
	crt_group_id_t unused1;
	d_iov_t unused2;
	uint32_t unused3;
	uint32_t unused4;
	uint32_t unused5;
	uint32_t unused6;
	uint32_t unused7;
};

struct crt_st_start_out {
	int32_t unused1;
};

struct crt_st_status_req_in {
	crt_bulk_t unused1;
};

struct crt_st_status_req_out {
	uint64_t test_duration_ns;
	uint32_t num_remaining;
	int32_t status;
};

struct crt_uri_lookup_in {
	crt_group_id_t ul_grp_id;
	d_rank_t ul_rank;
	uint32_t ul_tag;
};

struct crt_uri_lookup_out {
	crt_phy_addr_t ul_uri;
	int32_t ul_rc;
};

