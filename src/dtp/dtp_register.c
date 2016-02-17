/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of daos_transport. It implements the RPC register related
 * APIs and internal handling.
 */

#include <dtp_internal.h>

int
dtp_opc_map_create(unsigned int bits, struct dtp_opc_map **opc_map)
{
	struct dtp_opc_map    *map = NULL;
	int                   rc = 0, i;

	D_ASSERT(opc_map != NULL);

	D_ALLOC_PTR(map);
	if (map == NULL)
		return -DER_NOMEM;

	map->dom_pid = getpid();
	map->dom_bits = bits;
	D_ALLOC(map->dom_hash, sizeof(map->dom_hash[0]) * (1 << bits));
	if (map->dom_hash == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	for (i = 0; i < (1 << bits); i++)
		DAOS_INIT_LIST_HEAD(&map->dom_hash[i]);

	rc = pthread_rwlock_init(&map->dom_rwlock, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create mutex for dtp opc map.\n");
		D_GOTO(out, rc = -rc);
	}
	map->dom_lock_init = 1;

	*opc_map = map;
out:
	if (rc != 0)
		dtp_opc_map_destroy(map);
	return rc;
}

void
dtp_opc_map_destroy(struct dtp_opc_map *map)
{
	struct dtp_opc_info *info;
	int                  i;

	D_ASSERT(map != NULL);
	if (map->dom_hash == NULL)
		goto skip;

	for (i = 0; i < (1 << map->dom_bits); i++) {
		while (!daos_list_empty(&map->dom_hash[i])) {
			info = daos_list_entry(map->dom_hash[i].next,
					       struct dtp_opc_info, doi_link);
			daos_list_del_init(&info->doi_link);
			/*
			D_DEBUG(DF_TP, "deleted opc: 0x%x from map(hash %d).\n",
				info->doi_opc, i);
			*/
			D_FREE_PTR(info);
		}
	}
	D_FREE(map->dom_hash, sizeof(map->dom_hash[0]) * map->dom_bits);

skip:
	if (map->dom_lock_init && map->dom_pid == getpid())
		pthread_rwlock_destroy(&map->dom_rwlock);

	D_FREE_PTR(map);
}

static inline unsigned int
dtp_opc_hash(struct dtp_opc_map *map, dtp_opcode_t opc)
{
	D_ASSERT(map != NULL);
	return opc & ((1U << map->dom_bits) - 1);
}

static inline void
dtp_opc_info_init(struct dtp_opc_info *info)
{
	D_ASSERT(info != NULL);
	DAOS_INIT_LIST_HEAD(&info->doi_link);
	/* D_ALLOC zeroed the content already. */
	/*
	info->doi_opc = 0;
	info->doi_proc_init = 0;
	info->doi_rpc_init = 0;
	info->doi_inproc_cb = NULL;
	info->doi_outproc_cb = NULL;
	info->doi_input_size = 0;
	info->doi_output_size = 0;
	info->doi_rpc_cb = NULL;
	*/
}

struct dtp_opc_info *
dtp_opc_lookup(struct dtp_opc_map *map, dtp_opcode_t opc, int locked)
{
	struct dtp_opc_info *info = NULL;
	unsigned int         hash;

	hash = dtp_opc_hash(map, opc);

	if (locked == 0)
		pthread_rwlock_rdlock(&map->dom_rwlock);

	daos_list_for_each_entry(info, &map->dom_hash[hash], doi_link) {
		if (info->doi_opc == opc) {
			if (locked == 0)
				pthread_rwlock_unlock(&map->dom_rwlock);
			return info;
		}
		if (info->doi_opc > opc)
			break;
	}

	if (locked == 0)
		pthread_rwlock_unlock(&map->dom_rwlock);

	return NULL;
}

static int
dtp_opc_reg(struct dtp_opc_map *map, dtp_opcode_t opc,
	    dtp_proc_cb_t in_proc_cb, dtp_proc_cb_t out_proc_cb,
	    daos_size_t input_size, daos_size_t output_size,
	    dtp_rpc_cb_t rpc_cb, int ignore_rpccb, int locked)
{
	struct dtp_opc_info *info = NULL, *new_info;
	unsigned int         hash;
	int                  rc = 0;

	hash = dtp_opc_hash(map, opc);

	if (locked == 0)
		pthread_rwlock_wrlock(&map->dom_rwlock);

	daos_list_for_each_entry(info, &map->dom_hash[hash], doi_link) {
		if (info->doi_opc == opc) {
			/*
			D_DEBUG(DF_TP, "re-reg, opc 0x%x.\n", opc);
			*/
			info->doi_inproc_cb = in_proc_cb;
			info->doi_outproc_cb = out_proc_cb;
			if (info->doi_input_size != input_size) {
				D_DEBUG(DF_TP, "opc 0x%x, update input_size "
					"from "DF_U64" to "DF_U64".\n", opc,
					info->doi_input_size, input_size);
				info->doi_input_size = input_size;
			}
			if (info->doi_output_size != output_size) {
				D_DEBUG(DF_TP, "opc 0x%x, update output_size "
					"from "DF_U64" to "DF_U64".\n", opc,
					info->doi_output_size, output_size);
				info->doi_output_size = output_size;
			}
			if (ignore_rpccb == 0) {
				/*
				D_DEBUG(DF_TP, "re-reg_srv, opc 0x%x.\n", opc);
				*/
				info->doi_rpc_cb = rpc_cb;
				info->doi_rpc_init = 1;
			}
			D_GOTO(out, rc = 0);
		}
		if (info->doi_opc > opc)
			break;
	}

	D_ALLOC_PTR(new_info);
	if (new_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dtp_opc_info_init(new_info);
	new_info->doi_opc = opc;
	new_info->doi_inproc_cb = in_proc_cb;
	new_info->doi_outproc_cb = out_proc_cb;
	new_info->doi_input_size = input_size;
	new_info->doi_output_size = output_size;
	new_info->doi_proc_init = 1;
	if (ignore_rpccb == 0) {
		new_info->doi_rpc_cb = rpc_cb;
		new_info->doi_rpc_init = 1;
	}
	daos_list_add_tail(&new_info->doi_link, &info->doi_link);

out:
	if (locked == 0)
		pthread_rwlock_unlock(&map->dom_rwlock);
	return rc;
}

static int
dtp_rpc_reg_internal(dtp_opcode_t opc, dtp_proc_cb_t in_proc_cb,
		     dtp_proc_cb_t out_proc_cb, daos_size_t input_size,
		     daos_size_t output_size, dtp_rpc_cb_t rpc_handler,
		     int ignore_rpccb)
{
	int	rc = 0;

	if (input_size > DTP_MAX_INPUT_SIZE ||
	    output_size > DTP_MAX_OUTPUT_SIZE) {
		D_ERROR("input_size "DF_U64" or output_size "DF_U64" "
			"too large.\n", input_size, output_size);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dtp_opc_reg(dtp_gdata.dg_opc_map, opc, in_proc_cb, out_proc_cb,
			 input_size, output_size, rpc_handler, ignore_rpccb,
			 DTP_UNLOCK);
	if (rc != 0) {
		D_ERROR("rpc (opcode: %d) register failed, rc: %d.\n", opc, rc);
		D_GOTO(out, rc);
	}

	rc = dtp_hg_reg(opc, (dtp_proc_cb_t)dtp_proc_in_common,
			(dtp_proc_cb_t)dtp_proc_out_common,
			(dtp_hg_rpc_cb_t)dtp_rpc_handler_common);
	if (rc != 0)
		D_ERROR("dtp_hg_reg(opc: 0x%x), failed rc: %d.\n", opc, rc);

out:
	return rc;
}

int
dtp_rpc_reg(dtp_opcode_t opc, dtp_proc_cb_t in_proc_cb,
	    dtp_proc_cb_t out_proc_cb, daos_size_t input_size,
	    daos_size_t output_size)
{
	return dtp_rpc_reg_internal(opc, in_proc_cb, out_proc_cb, input_size,
				    output_size, NULL, 1);
}

int
dtp_rpc_srv_reg(dtp_opcode_t opc, dtp_proc_cb_t in_proc_cb,
		dtp_proc_cb_t out_proc_cb, daos_size_t input_size,
		daos_size_t output_size, dtp_rpc_cb_t rpc_handler)
{
	return dtp_rpc_reg_internal(opc, in_proc_cb, out_proc_cb, input_size,
				    output_size, rpc_handler, 0);
}

