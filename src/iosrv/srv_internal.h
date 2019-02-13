/*
 * (C) Copyright 2016-2018 Intel Corporation.
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
#ifndef __DAOS_SRV_INTERNAL__
#define __DAOS_SRV_INTERNAL__

#include <daos_srv/daos_server.h>

/** Server node topology */
extern hwloc_topology_t	dss_topo;
/** core depth of the topology */
extern int		dss_core_depth;
/** number of physical cores, w/o hyper-threading */
extern int		dss_core_nr;
/** start offset index of the first core for service XS */
extern int		dss_core_offset;

/** Number of offload XS per target (1 or 2)*/
extern unsigned int	dss_tgt_offload_xs_nr;
/** number of system XS */
extern unsigned int	dss_sys_xs_nr;

/* module.c */
int dss_module_init(void);
int dss_module_fini(bool force);
int dss_module_load(const char *modname, uint64_t *mod_facs);
int dss_module_unload(const char *modname);
void dss_module_unload_all(void);
int dss_module_setup_all(void);
int dss_module_cleanup_all(void);

/* srv.c */
int dss_srv_init(void);
int dss_srv_fini(bool force);
void dss_dump_ABT_state(void);

/* tls.c */
void dss_tls_fini(struct dss_thread_local_storage *dtls);
struct dss_thread_local_storage *dss_tls_init(int tag);

/* server_iv.c */
int ds_iv_init(void);
int ds_iv_fini(void);

/* system.c */
int dss_sys_map_load(const char *path, crt_group_id_t grpid, d_rank_t self_rank,
		     int ntags);

/** To schedule ULT on caller's self XS */
#define DSS_XS_SELF		(-1)
/** Number of XS for each VOS target (main XS and its offload XS) */
#define DSS_XS_NR_PER_TGT	(dss_tgt_offload_xs_nr + 1)
/** Total number of XS */
#define DSS_XS_NR_TOTAL						\
	(dss_tgt_nr * DSS_XS_NR_PER_TGT + dss_sys_xs_nr)
/** main XS id of (vos) tgt_id */
#define DSS_MAIN_XS_ID(tgt_id)					\
	(((tgt_id) * DSS_XS_NR_PER_TGT) + dss_sys_xs_nr)
/**
 * The offset of the XS in the target's XS set -
 * 0 is the main XS, and 1 is the first offload XS and so on.
 */
#define DSS_XS_OFFSET_IN_TGT(xs_id)				\
	(((xs_id) - dss_sys_xs_nr) % DSS_XS_NR_PER_TGT)

/**
 * Get the service xstream xs_id to schedule the ULT for specific ULT type and
 * target index.
 *
 * \param[in]	req_type	ULT type (enum daos_ult_type)
 * \param[in]	tgt_idx		target VOS index (main xstream index)
 *
 * \return			XS (xstream) xs_id to be used for the ULT
 */
static inline int
dss_tgt2xs(int ult_type, int tgt_id)
{
	D_ASSERT(tgt_id >= 0 && tgt_id < dss_tgt_nr);

	switch (ult_type) {
	case DSS_ULT_SELF:
		return DSS_XS_SELF;
	case DSS_ULT_IOFW:
		return (DSS_MAIN_XS_ID(tgt_id) + 1) % DSS_XS_NR_TOTAL;
	case DSS_ULT_EC:
	case DSS_ULT_CHECKSUM:
	case DSS_ULT_COMPRESS:
		return DSS_MAIN_XS_ID(tgt_id) + dss_tgt_offload_xs_nr;
	case DSS_ULT_POOL_SRV:
	case DSS_ULT_DRPC:
		return 0;
	case DSS_ULT_REBUILD:
	case DSS_ULT_AGGREGATE:
		return DSS_MAIN_XS_ID(tgt_id);
	default:
		D_ASSERTF(0, "bad ult_type %d.\n", ult_type);
		return -DER_INVAL;
	}
}

/**
 * get the VOS target ID of xstream.
 *
 * \param[in]	xs_id	xstream ID
 *
 * \return		VOS target ID (-1 for system XS).
 */
static inline int
dss_xs2tgt(int xs_id)
{
	D_ASSERTF(xs_id >= 0 && xs_id < DSS_XS_NR_TOTAL,
		  "invalid xs_id %d, dss_tgt_nr %d, "
		  "dss_tgt_offload_xs_nr %d.\n",
		  xs_id, dss_tgt_nr, dss_tgt_offload_xs_nr);
	if (xs_id < dss_sys_xs_nr)
		return -1;

	return (xs_id - dss_sys_xs_nr) / DSS_XS_NR_PER_TGT;
}

#endif /* __DAOS_SRV_INTERNAL__ */
