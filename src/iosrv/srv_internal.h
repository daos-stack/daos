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

/* module.c */
int dss_module_init(void);
int dss_module_fini(bool force);
int dss_module_load(const char *modname, uint64_t *mod_facs);
int dss_module_unload(const char *modname);
void dss_module_unload_all(void);
int dss_module_setup_all(void);
int dss_module_cleanup_all(void);

/* srv.c */
int dss_srv_init(int);
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

#endif /* __DAOS_SRV_INTERNAL__ */
