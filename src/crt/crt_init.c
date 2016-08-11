/**
 * (C) Copyright 2016 Intel Corporation.
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
/**
 * This file is part of CaRT. It implements CaRT init and finalize related
 * APIs/handling.
 */

#include <crt_internal.h>

struct crt_gdata crt_gdata;
static pthread_once_t gdata_init_once = PTHREAD_ONCE_INIT;
static volatile int   gdata_init_flag;

/* internally generate a physical address string */
static int
crt_gen_bmi_phyaddr(crt_phy_addr_t *phy_addr)
{
	int			socketfd;
	struct sockaddr_in	tmp_socket;
	char			*addrstr;
	char			tmp_addrstr[CRT_ADDR_STR_MAX_LEN];
	struct ifaddrs		*if_addrs = NULL;
	struct ifaddrs		*ifa;
	void			*tmp_ptr;
	char			ip_str[INET_ADDRSTRLEN];
	const char		*ip_str_p = NULL;
	socklen_t		slen = sizeof(struct sockaddr);
	int			rc;

	C_ASSERT(phy_addr != NULL);

	/*
	 * step 1 - get the IP address (cannot get it through socket, always get
	 * 0.0.0.0 by inet_ntoa(tmp_socket.sin_addr).)
	 * Using the IP as listening address is better than using hostname
	 * because:
	 * 1) for the case there are multiple NICs on one host,
	 * 2) mercury is much slow when listening on hostname (not sure why).
	 */
	rc = getifaddrs(&if_addrs);
	if (rc != 0) {
		C_ERROR("cannot getifaddrs, errno: %d(%s).\n",
			errno, strerror(errno));
		C_GOTO(out, rc = -CER_ADDRSTR_GEN);
	}
	C_ASSERT(if_addrs != NULL);

	/* TODO may from a config file to select one appropriate IP address */
	for (ifa = if_addrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		memset(ip_str, 0, INET_ADDRSTRLEN);
		if (ifa->ifa_addr->sa_family == AF_INET) {
			/* check it is a valid IPv4 Address */
			tmp_ptr =
			&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
			ip_str_p = inet_ntop(AF_INET, tmp_ptr, ip_str,
					     INET_ADDRSTRLEN);
			if (ip_str_p == NULL) {
				C_ERROR("inet_ntop failed, errno: %d(%s).\n",
					errno, strerror(errno));
				freeifaddrs(if_addrs);
				C_GOTO(out, rc = -CER_ADDRSTR_GEN);
			}
			if (strcmp(ip_str_p, "127.0.0.1") == 0) {
				/* C_DEBUG(CF_TP, "bypass 127.0.0.1.\n"); */
				continue;
			}
			C_DEBUG(CF_TP, "Get %s IPv4 Address %s\n",
				ifa->ifa_name, ip_str);
			break;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			/* check it is a valid IPv6 Address */
			/*
			 * tmp_ptr =
			 * &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
			 * inet_ntop(AF_INET6, tmp_ptr, ip_str,
			 *           INET6_ADDRSTRLEN);
			 * C_DEBUG(CF_TP, "Get %s IPv6 Address %s\n",
			 *         ifa->ifa_name, ip_str);
			 */
		}
	}
	freeifaddrs(if_addrs);
	if (ip_str_p == NULL) {
		C_ERROR("no IP addr found.\n");
		C_GOTO(out, rc = -CER_ADDRSTR_GEN);
	}

	/* step 2 - get one available port number */
	socketfd = socket(AF_INET, SOCK_STREAM, 0);
	if (socketfd == -1) {
		C_ERROR("cannot create socket, errno: %d(%s).\n",
			errno, strerror(errno));
		C_GOTO(out, rc = -CER_ADDRSTR_GEN);
	}
	tmp_socket.sin_family = AF_INET;
	tmp_socket.sin_addr.s_addr = INADDR_ANY;
	tmp_socket.sin_port = 0;

	rc = bind(socketfd, (const struct sockaddr *)&tmp_socket,
		  sizeof(tmp_socket));
	if (rc != 0) {
		C_ERROR("cannot bind socket, errno: %d(%s).\n",
			errno, strerror(errno));
		close(socketfd);
		C_GOTO(out, rc = -CER_ADDRSTR_GEN);
	}

	rc = getsockname(socketfd, (struct sockaddr *)&tmp_socket, &slen);
	if (rc != 0) {
		C_ERROR("cannot create getsockname, errno: %d(%s).\n",
			errno, strerror(errno));
		close(socketfd);
		C_GOTO(out, rc = -CER_ADDRSTR_GEN);
	}
	rc = close(socketfd);
	if (rc != 0) {
		C_ERROR("cannot close socket, errno: %d(%s).\n",
			errno, strerror(errno));
		C_GOTO(out, rc = -CER_ADDRSTR_GEN);
	}

	snprintf(tmp_addrstr, CRT_ADDR_STR_MAX_LEN, "bmi+tcp://%s:%d", ip_str,
		 ntohs(tmp_socket.sin_port));
	addrstr = strndup(tmp_addrstr, CRT_ADDR_STR_MAX_LEN);
	if (addrstr != NULL) {
		C_DEBUG(CF_TP, "generated phyaddr: %s.\n", addrstr);
		*phy_addr = addrstr;
	} else {
		C_ERROR("strndup failed.\n");
		rc = -CER_NOMEM;
	}

out:
	return rc;
}


/* first step init - for initializing crt_gdata */
static void data_init()
{
	int rc = 0;

	C_DEBUG(CF_TP, "initializing crt_gdata...\n");

	/*
	 * avoid size mis-matching between client/server side
	 * /see crt_proc_uuid_t().
	 */
	C_CASSERT(sizeof(uuid_t) == 16);

	CRT_INIT_LIST_HEAD(&crt_gdata.dg_ctx_list);

	rc = pthread_rwlock_init(&crt_gdata.dg_rwlock, NULL);
	C_ASSERT(rc == 0);

	crt_gdata.dg_ctx_num = 0;
	crt_gdata.dg_refcount = 0;
	crt_gdata.dg_inited = 0;
	crt_gdata.dg_addr = NULL;
	crt_gdata.dg_verbs = false;
	crt_gdata.dg_multi_na = false;

	gdata_init_flag = 1;
}

static int
crt_mcl_init(crt_phy_addr_t *addr)
{
	struct mcl_set	*tmp_set;
	int		rc;

	C_ASSERT(addr != NULL && strlen(*addr) > 0);

	crt_gdata.dg_mcl_state = mcl_init(addr);
	if (crt_gdata.dg_mcl_state == NULL) {
		C_ERROR("mcl_init failed.\n");
		C_GOTO(out, rc = -CER_MCL);
	}
	C_DEBUG(CF_TP, "mcl_init succeed(server %d), nspace: %s, rank: %d, "
		"univ_size: %d, self_uri: %s.\n",
		crt_gdata.dg_server,
		crt_gdata.dg_mcl_state->myproc.nspace,
		crt_gdata.dg_mcl_state->myproc.rank,
		crt_gdata.dg_mcl_state->univ_size,
		crt_gdata.dg_mcl_state->self_uri);
	if (crt_gdata.dg_server == true) {
		rc = mcl_startup(crt_gdata.dg_mcl_state,
				 crt_gdata.dg_hg->dhg_nacla,
				 CRT_GLOBAL_SRV_GROUP_NAME, true,
				 &crt_gdata.dg_mcl_srv_set);
		tmp_set = crt_gdata.dg_mcl_srv_set;
	} else {
		rc = mcl_startup(crt_gdata.dg_mcl_state,
				 crt_gdata.dg_hg->dhg_nacla,
				 CRT_CLI_GROUP_NAME, false,
				 &crt_gdata.dg_mcl_cli_set);
		tmp_set = crt_gdata.dg_mcl_cli_set;
	}
	if (rc != MCL_SUCCESS) {
		C_ERROR("mcl_startup failed(server: %d), rc: %d.\n",
			crt_gdata.dg_server, rc);
		mcl_finalize(crt_gdata.dg_mcl_state);
		C_GOTO(out, rc = -CER_MCL);
	}
	C_DEBUG(CF_TP, "mcl_startup succeed(server: %d), grp_name: %s, "
		"size %d, rank %d, is_local %d, is_service %d, self_uri: %s.\n",
		crt_gdata.dg_server, tmp_set->name, tmp_set->size,
		tmp_set->self, tmp_set->is_local, tmp_set->is_service,
		tmp_set->state->self_uri);
	if (crt_gdata.dg_server == true) {
		C_ASSERT(crt_gdata.dg_mcl_srv_set != NULL);
	} else {
		C_ASSERT(crt_gdata.dg_mcl_cli_set != NULL);
		/* for client, attach it to service process set. */
		rc = mcl_attach(crt_gdata.dg_mcl_state,
				CRT_GLOBAL_SRV_GROUP_NAME,
				&crt_gdata.dg_mcl_srv_set);
		if (rc == MCL_SUCCESS) {
			C_ASSERT(crt_gdata.dg_mcl_srv_set != NULL);
			tmp_set = crt_gdata.dg_mcl_srv_set;
			C_DEBUG(CF_TP, "attached to group(name: %s, size %d, "
				"rank %d, is_local %d, is_service %d).\n",
				tmp_set->name, tmp_set->size,
				tmp_set->self, tmp_set->is_local,
				tmp_set->is_service);
		} else {
			C_ERROR("failed to attach to service group, rc: %d.\n",
				rc);
			mcl_set_free(NULL, crt_gdata.dg_mcl_cli_set);
			mcl_finalize(crt_gdata.dg_mcl_state);
			C_GOTO(out, rc = -CER_MCL);
		}
	}
	crt_gdata.dg_srv_grp_id = CRT_GLOBAL_SRV_GROUP_NAME;
	crt_gdata.dg_cli_grp_id = CRT_CLI_GROUP_NAME;

out:
	return rc;
}

static int
crt_mcl_fini()
{
	int rc = 0;

	C_ASSERT(crt_gdata.dg_mcl_state != NULL);
	C_ASSERT(crt_gdata.dg_mcl_srv_set != NULL);

	mcl_set_free(crt_gdata.dg_hg->dhg_nacla,
		     crt_gdata.dg_mcl_srv_set);
	if (crt_gdata.dg_server == false) {
		mcl_set_free(crt_gdata.dg_hg->dhg_nacla,
			     crt_gdata.dg_mcl_cli_set);
	}

	C_ASSERT(crt_gdata.dg_mcl_state != NULL);
	rc = mcl_finalize(crt_gdata.dg_mcl_state);
	if (rc == 0)
		C_DEBUG(CF_TP, "mcl_finalize succeed.\n");
	else
		C_ERROR("mcl_finalize failed, rc: %d.\n", rc);

	return rc;
}

int
crt_init(bool server)
{
	crt_phy_addr_t	addr = NULL, addr_env;
	int		rc = 0;

	C_DEBUG(CF_TP, "Enter crt_init.\n");

	if (gdata_init_flag == 0) {
		rc = pthread_once(&gdata_init_once, data_init);
		if (rc != 0) {
			C_ERROR("crt_init failed, rc(%d) - %s.\n",
				rc, strerror(rc));
			C_GOTO(out, rc = -rc);
		}
	}
	C_ASSERT(gdata_init_flag == 1);

	pthread_rwlock_wrlock(&crt_gdata.dg_rwlock);
	if (crt_gdata.dg_inited == 0) {
		crt_gdata.dg_server = server;

		if (server == true)
			crt_gdata.dg_multi_na = true;

		addr_env = (crt_phy_addr_t)getenv(CRT_PHY_ADDR_ENV);
		if (addr_env == NULL) {
			C_DEBUG(CF_TP, "ENV %s not found.\n", CRT_PHY_ADDR_ENV);
			goto do_init;
		} else{
			C_DEBUG(CF_TP, "EVN %s: %s.\n",
				CRT_PHY_ADDR_ENV, addr_env);
		}
		if (strncmp(addr_env, "bmi+tcp", 7) == 0) {
			if (strcmp(addr_env, "bmi+tcp") == 0) {
				rc = crt_gen_bmi_phyaddr(&addr);
				if (rc == 0) {
					C_DEBUG(CF_TP, "ENV %s (%s), generated "
						"a BMI phyaddr: %s.\n",
						CRT_PHY_ADDR_ENV, addr_env,
						addr);
				} else {
					C_ERROR("crt_gen_bmi_phyaddr failed, "
						"rc: %d.\n", rc);
					C_GOTO(out, rc);
				}
			} else {
				C_DEBUG(CF_TP, "ENV %s found, use addr %s.\n",
					CRT_PHY_ADDR_ENV, addr_env);
				addr = strdup(addr_env);
				if (addr == NULL) {
					C_ERROR("strdup failed.\n");
					C_GOTO(out, rc = -CER_NOMEM);
				}
			}
			C_ASSERT(addr != NULL);
			crt_gdata.dg_multi_na = false;
		} else if (strncmp(addr_env, "cci+verbs", 9) == 0) {
			crt_gdata.dg_verbs = true;
		}

do_init:
		/*
		 * For client unset the CCI_CONFIG ENV, then client-side process
		 * will use random port number and will not conflict with server
		 * side. As when using orterun to load both server and client it
		 * possibly will lead them share the same ENV.
		 */
		if (server == false)
			unsetenv("CCI_CONFIG");

		rc = crt_hg_init(&addr, server);
		if (rc != 0) {
			C_ERROR("crt_hg_init failed rc: %d.\n", rc);
			C_GOTO(unlock, rc);
		}
		C_ASSERT(addr != NULL);
		crt_gdata.dg_addr = addr;
		crt_gdata.dg_addr_len = strlen(addr);

		rc = crt_mcl_init(&addr);
		if (rc != 0) {
			C_ERROR("crt_mcl_init failed, rc: %d.\n", rc);
			crt_hg_fini();
			C_FREE(crt_gdata.dg_addr, crt_gdata.dg_addr_len);
			C_GOTO(unlock, rc = -CER_MCL);
		}

		rc = crt_opc_map_create(CRT_OPC_MAP_BITS);
		if (rc != 0) {
			C_ERROR("crt_opc_map_create failed rc: %d.\n", rc);
			crt_hg_fini();
			crt_mcl_fini();
			C_FREE(crt_gdata.dg_addr, crt_gdata.dg_addr_len);
			C_GOTO(unlock, rc);
		}
		C_ASSERT(crt_gdata.dg_opc_map != NULL);

		crt_gdata.dg_inited = 1;
	} else {
		if (crt_gdata.dg_server == false && server == true) {
			C_ERROR("CRT initialized as client, cannot set as "
				"server again.\n");
			C_GOTO(unlock, rc = -CER_INVAL);
		}
	}

	crt_gdata.dg_refcount++;

unlock:
	pthread_rwlock_unlock(&crt_gdata.dg_rwlock);
out:
	C_DEBUG(CF_TP, "Exit crt_init, rc: %d.\n", rc);
	return rc;
}

bool
crt_initialized()
{
	return (gdata_init_flag == 1) && (crt_gdata.dg_inited == 1);
}

int
crt_finalize(void)
{
	int rc = 0;

	C_DEBUG(CF_TP, "Enter crt_finalize.\n");

	pthread_rwlock_wrlock(&crt_gdata.dg_rwlock);

	if (!crt_initialized()) {
		C_ERROR("cannot finalize before initializing.\n");
		pthread_rwlock_unlock(&crt_gdata.dg_rwlock);
		C_GOTO(out, rc = -CER_UNINIT);
	}
	if (crt_gdata.dg_ctx_num > 0) {
		C_ASSERT(!crt_context_empty(CRT_LOCKED));
		C_ERROR("cannot finalize, current ctx_num(%d).\n",
			crt_gdata.dg_ctx_num);
		pthread_rwlock_unlock(&crt_gdata.dg_rwlock);
		C_GOTO(out, rc = -CER_NO_PERM);
	} else {
		C_ASSERT(crt_context_empty(CRT_LOCKED));
	}

	crt_gdata.dg_refcount--;
	if (crt_gdata.dg_refcount == 0) {
		rc = crt_mcl_fini();
		/* mcl finalize failure cause state unstable, just assert it */
		C_ASSERT(rc == 0);

		rc = crt_hg_fini();
		if (rc != 0) {
			C_ERROR("crt_hg_fini failed rc: %d.\n", rc);
			crt_gdata.dg_refcount++;
			pthread_rwlock_unlock(&crt_gdata.dg_rwlock);
			C_GOTO(out, rc);
		}

		C_ASSERT(crt_gdata.dg_addr != NULL);
		C_FREE(crt_gdata.dg_addr, crt_gdata.dg_addr_len);
		crt_gdata.dg_server = false;

		crt_opc_map_destroy(crt_gdata.dg_opc_map);

		pthread_rwlock_unlock(&crt_gdata.dg_rwlock);

		rc = pthread_rwlock_destroy(&crt_gdata.dg_rwlock);
		if (rc != 0) {
			C_ERROR("failed to destroy dg_rwlock, rc: %d.\n", rc);
			C_GOTO(out, rc = -rc);
		}

		/* allow the same program to re-initialize */
		crt_gdata.dg_refcount = 0;
		crt_gdata.dg_inited = 0;
		gdata_init_once = PTHREAD_ONCE_INIT;
		gdata_init_flag = 0;
	} else {
		pthread_rwlock_unlock(&crt_gdata.dg_rwlock);
	}

out:
	C_DEBUG(CF_TP, "Exit crt_finalize, rc: %d.\n", rc);
	return rc;
}
