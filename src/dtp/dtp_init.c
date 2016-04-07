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
 * This file is part of daos_transport. It implements dtp init and finalize
 * related APIs/handling.
 */

#include <dtp_internal.h>

struct dtp_gdata dtp_gdata;
static pthread_once_t gdata_init_once = PTHREAD_ONCE_INIT;
static volatile int   gdata_init_flag;

/* internally generate a physical address string */
static int
dtp_gen_phyaddr(dtp_phy_addr_t *phy_addr)
{
	int			socketfd;
	struct sockaddr_in	tmp_socket;
	char			*addrstr;
	char			tmp_addrstr[DTP_ADDR_STR_MAX_LEN];
	struct ifaddrs		*if_addrs = NULL;
	struct ifaddrs		*ifa;
	void			*tmp_ptr;
	char			ip_str[INET_ADDRSTRLEN];
	const char		*ip_str_p = NULL;
	socklen_t		slen = sizeof(struct sockaddr);
	int			rc;

	D_ASSERT(phy_addr != NULL);

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
		D_ERROR("cannot getifaddrs, errno: %d(%s).\n",
			errno, strerror(errno));
		D_GOTO(out, rc = -DER_DTP_ADDRSTR_GEN);
	}
	D_ASSERT(if_addrs != NULL);

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
				D_ERROR("inet_ntop failed, errno: %d(%s).\n",
					errno, strerror(errno));
				freeifaddrs(if_addrs);
				D_GOTO(out, rc = -DER_DTP_ADDRSTR_GEN);
			}
			if (strcmp(ip_str_p, "127.0.0.1") == 0) {
				/* D_DEBUG(DF_TP, "bypass 127.0.0.1.\n"); */
				continue;
			}
			D_DEBUG(DF_TP, "Get %s IPv4 Address %s\n",
				ifa->ifa_name, ip_str);
			break;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			/* check it is a valid IPv6 Address */
			/*
			 * tmp_ptr =
			 * &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
			 * inet_ntop(AF_INET6, tmp_ptr, ip_str,
			 *           INET6_ADDRSTRLEN);
			 * D_DEBUG(DF_TP, "Get %s IPv6 Address %s\n",
			 *         ifa->ifa_name, ip_str);
			 */
		}
	}
	freeifaddrs(if_addrs);
	if (ip_str_p == NULL) {
		D_ERROR("no IP addr found.\n");
		D_GOTO(out, rc = -DER_DTP_ADDRSTR_GEN);
	}

	/* step 2 - get one available port number */
	socketfd = socket(AF_INET, SOCK_STREAM, 0);
	if (socketfd == -1) {
		D_ERROR("cannot create socket, errno: %d(%s).\n",
			errno, strerror(errno));
		D_GOTO(out, rc = -DER_DTP_ADDRSTR_GEN);
	}
	tmp_socket.sin_family = AF_INET;
	tmp_socket.sin_addr.s_addr = INADDR_ANY;
	tmp_socket.sin_port = 0;

	rc = bind(socketfd, (const struct sockaddr *)&tmp_socket,
		  sizeof(tmp_socket));
	if (rc != 0) {
		D_ERROR("cannot bind socket, errno: %d(%s).\n",
			errno, strerror(errno));
		close(socketfd);
		D_GOTO(out, rc = -DER_DTP_ADDRSTR_GEN);
	}

	rc = getsockname(socketfd, (struct sockaddr *)&tmp_socket, &slen);
	if (rc != 0) {
		D_ERROR("cannot create getsockname, errno: %d(%s).\n",
			errno, strerror(errno));
		close(socketfd);
		D_GOTO(out, rc = -DER_DTP_ADDRSTR_GEN);
	}
	rc = close(socketfd);
	if (rc != 0) {
		D_ERROR("cannot close socket, errno: %d(%s).\n",
			errno, strerror(errno));
		D_GOTO(out, rc = -DER_DTP_ADDRSTR_GEN);
	}

	snprintf(tmp_addrstr, DTP_ADDR_STR_MAX_LEN, "bmi+tcp://%s:%d", ip_str,
		 ntohs(tmp_socket.sin_port));
	addrstr = strndup(tmp_addrstr, DTP_ADDR_STR_MAX_LEN);
	if (addrstr != NULL) {
		D_DEBUG(DF_TP, "generated phyaddr: %s.\n", addrstr);
		*phy_addr = addrstr;
	} else {
		D_ERROR("strndup failed.\n");
		rc = -DER_NOMEM;
	}

out:
	return rc;
}


/* first step init - for initializing dtp_gdata */
static void data_init()
{
	int rc = 0;

	D_DEBUG(DF_TP, "initializing dtp_gdata...\n");

	/*
	 * avoid size mis-matching between client/server side
	 * /see dtp_proc_uuid_t().
	 */
	D_CASSERT(sizeof(uuid_t) == 16);

	DAOS_INIT_LIST_HEAD(&dtp_gdata.dg_ctx_list);

	rc = pthread_rwlock_init(&dtp_gdata.dg_rwlock, NULL);
	D_ASSERT(rc == 0);

	dtp_gdata.dg_ctx_num = 0;
	dtp_gdata.dg_refcount = 0;
	dtp_gdata.dg_inited = 0;

	gdata_init_flag = 1;
}

static int
dtp_mcl_init(dtp_phy_addr_t *addr)
{
	struct mcl_set	*tmp_set;
	int		rc, rc2;

	D_ASSERT(addr != NULL && strlen(*addr) > 0);

	dtp_gdata.dg_mcl_state = mcl_init(addr);
	if (dtp_gdata.dg_mcl_state == NULL) {
		D_ERROR("mcl_init failed.\n");
		D_GOTO(out, rc = -DER_DTP_MCL);
	}
	D_DEBUG(DF_TP, "mcl_init succeed(server %d), nspace: %s, rank: %d, "
		"univ_size: %d, num_sets: %d.\n",
		dtp_gdata.dg_server,
		dtp_gdata.dg_mcl_state->myproc.nspace,
		dtp_gdata.dg_mcl_state->myproc.rank,
		dtp_gdata.dg_mcl_state->univ_size,
		dtp_gdata.dg_mcl_state->num_sets);
	if (dtp_gdata.dg_server == true) {
		rc = mcl_startup(dtp_gdata.dg_mcl_state,
				 DTP_GLOBAL_SRV_GROUP_NAME, true,
				 &dtp_gdata.dg_mcl_srv_set);
		tmp_set = dtp_gdata.dg_mcl_srv_set;
	} else {
		rc = mcl_startup(dtp_gdata.dg_mcl_state,
				 DTP_CLI_GROUP_NAME, false,
				 &dtp_gdata.dg_mcl_cli_set);
		tmp_set = dtp_gdata.dg_mcl_cli_set;
	}
	if (rc != MCL_SUCCESS) {
		D_ERROR("mcl_startup failed(server: %d), rc: %d.\n",
			dtp_gdata.dg_server, rc);
		mcl_finalize(dtp_gdata.dg_mcl_state);
		D_GOTO(out, rc = -DER_DTP_MCL);
	}
	D_DEBUG(DF_TP, "mcl_startup succeed(server: %d), grp_name: %s, "
		"size %d, rank %d, is_local %d, is_service %d\n",
		dtp_gdata.dg_server, tmp_set->name, tmp_set->size,
		tmp_set->self, tmp_set->is_local, tmp_set->is_service);
	if (dtp_gdata.dg_server == true) {
		D_ASSERT(dtp_gdata.dg_mcl_srv_set != NULL);
	} else {
		D_ASSERT(dtp_gdata.dg_mcl_cli_set != NULL);
		/* for client, attach it to service process set. */
		rc = mcl_attach(dtp_gdata.dg_mcl_state,
				DTP_GLOBAL_SRV_GROUP_NAME,
				&dtp_gdata.dg_mcl_srv_set);
		if (rc == MCL_SUCCESS) {
			D_ASSERT(dtp_gdata.dg_mcl_srv_set != NULL);
			tmp_set = dtp_gdata.dg_mcl_srv_set;
			D_DEBUG(DF_TP, "attached to group(name: %s, size %d, "
				"rank %d, is_local %d, is_service %d).\n",
				tmp_set->name, tmp_set->size,
				tmp_set->self, tmp_set->is_local,
				tmp_set->is_service);
		} else {
			D_ERROR("failed to attach to service group, rc: %d.\n",
				rc);
			mcl_set_free(NULL, dtp_gdata.dg_mcl_cli_set);
			mcl_finalize(dtp_gdata.dg_mcl_state);
			D_GOTO(out, rc = -DER_DTP_MCL);
		}
	}
	rc = uuid_parse(DTP_GLOBAL_SRV_GRPID_STR,
			dtp_gdata.dg_srv_grp_id);
	rc2 = uuid_parse(DTP_GLOBAL_CLI_GRPID_STR,
			 dtp_gdata.dg_cli_grp_id);
	if (rc != 0 || rc2 != 0) {
		D_ERROR("uuid_parse failed rc: %d, rc2: %d.\n", rc, rc2);
		mcl_set_free(NULL, dtp_gdata.dg_mcl_cli_set);
		mcl_finalize(dtp_gdata.dg_mcl_state);
		rc = -DER_DTP_MCL;
	}

out:
	return rc;
}

static int
dtp_mcl_fini()
{
	int rc = 0;

	D_ASSERT(dtp_gdata.dg_mcl_state != NULL);
	D_ASSERT(dtp_gdata.dg_mcl_srv_set != NULL);

	mcl_set_free(dtp_gdata.dg_hg->dhg_nacla,
		     dtp_gdata.dg_mcl_srv_set);
	if (dtp_gdata.dg_server == false) {
		mcl_set_free(dtp_gdata.dg_hg->dhg_nacla,
			     dtp_gdata.dg_mcl_cli_set);
	}

	D_ASSERT(dtp_gdata.dg_mcl_state != NULL);
	rc = mcl_finalize(dtp_gdata.dg_mcl_state);
	if (rc == 0)
		D_DEBUG(DF_TP, "mcl_finalize succeed.\n");
	else
		D_ERROR("mcl_finalize failed, rc: %d.\n", rc);

	return rc;
}

int
dtp_init(bool server)
{
	dtp_phy_addr_t	addr;
	int		rc = 0, len;

	D_DEBUG(DF_TP, "Enter dtp_init.\n");

	if (gdata_init_flag == 0) {
		rc = pthread_once(&gdata_init_once, data_init);
		if (rc != 0) {
			D_ERROR("dtp_init failed, rc(%d) - %s.\n",
				rc, strerror(rc));
			D_GOTO(out, rc = -rc);
		}
	}
	D_ASSERT(gdata_init_flag == 1);

	pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);
	if (dtp_gdata.dg_inited == 0) {
		addr = (dtp_phy_addr_t)getenv(DTP_PHY_ADDR_ENV);
		if (addr == NULL || strlen(addr) == 0) {
			D_DEBUG(DF_TP, "ENV %s invalid, will generated addr.\n",
				DTP_PHY_ADDR_ENV);
			rc = dtp_gen_phyaddr(&addr);
			if (rc != 0) {
				D_ERROR("dtp_gen_phyaddr failed, rc: %d.\n",
					rc);
				D_GOTO(out, rc);
			}
			dtp_gdata.dg_self_addr = addr;
		} else {
			D_DEBUG(DF_TP, "ENV %s found, use addr %s.\n",
				DTP_PHY_ADDR_ENV, addr);
			dtp_gdata.dg_self_addr = strdup(addr);
		}

		dtp_gdata.dg_server = server;

		rc = dtp_mcl_init(&addr);
		if (rc != 0) {
			D_ERROR("dtp_mcl_init failed, rc: %d.\n", rc);
			len = strlen(dtp_gdata.dg_self_addr);
			D_FREE(dtp_gdata.dg_self_addr, len);
			D_GOTO(unlock, rc = -DER_DTP_MCL);
		}

		rc = dtp_hg_init(addr, server);
		if (rc != 0) {
			D_ERROR("dtp_hg_init failed rc: %d.\n", rc);
			dtp_mcl_fini();
			len = strlen(dtp_gdata.dg_self_addr);
			D_FREE(dtp_gdata.dg_self_addr, len);
			D_GOTO(unlock, rc);
		}

		rc = dtp_opc_map_create(DTP_OPC_MAP_BITS,
					&dtp_gdata.dg_opc_map);
		if (rc != 0) {
			D_ERROR("dtp_opc_map_create failed rc: %d.\n", rc);
			dtp_hg_fini();
			dtp_mcl_fini();
			len = strlen(dtp_gdata.dg_self_addr);
			D_FREE(dtp_gdata.dg_self_addr, len);
			D_GOTO(unlock, rc);
		}

		dtp_gdata.dg_inited = 1;
	} else {
		if (dtp_gdata.dg_server == false && server == true) {
			D_ERROR("DTP initialized as client, cannot set as "
				"server again.\n");
			D_GOTO(unlock, rc = -DER_INVAL);
		}
	}

	dtp_gdata.dg_refcount++;

unlock:
	pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
out:
	D_DEBUG(DF_TP, "Exit dtp_init, rc: %d.\n", rc);
	return rc;
}

bool
dtp_initialized()
{
	return (gdata_init_flag == 1) && (dtp_gdata.dg_inited == 1);
}

int
dtp_finalize(void)
{
	int rc = 0, len;

	D_DEBUG(DF_TP, "Enter dtp_finalize.\n");

	pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);

	if (!dtp_initialized()) {
		D_ERROR("cannot finalize before initializing.\n");
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	if (dtp_gdata.dg_ctx_num > 0) {
		D_ASSERT(!dtp_context_empty(DTP_LOCKED));
		D_ERROR("cannot finalize, current ctx_num(%d.).\n",
			dtp_gdata.dg_ctx_num);
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_GOTO(out, rc = -DER_NO_PERM);
	} else {
		D_ASSERT(dtp_context_empty(DTP_LOCKED));
	}

	dtp_gdata.dg_refcount--;
	if (dtp_gdata.dg_refcount == 0) {
		rc = dtp_mcl_fini();
		/* mcl finalize failure cause state unstable, just assert it */
		D_ASSERT(rc == 0);

		rc = dtp_hg_fini();
		if (rc != 0) {
			D_ERROR("dtp_hg_fini failed rc: %d.\n", rc);
			dtp_gdata.dg_refcount++;
			pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
			D_GOTO(out, rc);
		}

		D_ASSERT(dtp_gdata.dg_self_addr != NULL);
		len = strlen(dtp_gdata.dg_self_addr);
		D_FREE(dtp_gdata.dg_self_addr, len);
		dtp_gdata.dg_server = false;

		dtp_opc_map_destroy(dtp_gdata.dg_opc_map);

		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

		rc = pthread_rwlock_destroy(&dtp_gdata.dg_rwlock);
		if (rc != 0) {
			D_ERROR("failed to destroy dg_rwlock, rc: %d.\n", rc);
			D_GOTO(out, rc = -rc);
		}

		/* allow the same program to re-initialize */
		dtp_gdata.dg_refcount = 0;
		dtp_gdata.dg_inited = 0;
		gdata_init_flag = 0;
	} else {
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
	}

out:
	D_DEBUG(DF_TP, "Exit dtp_finalize, rc: %d.\n", rc);
	return rc;
}
