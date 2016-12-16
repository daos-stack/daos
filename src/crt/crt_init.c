/* Copyright (C) 2016 Intel Corporation
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
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
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
				/* C_DEBUG("bypass 127.0.0.1.\n"); */
				continue;
			}
			C_DEBUG("Get %s IPv4 Address %s\n",
				ifa->ifa_name, ip_str);
			break;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			/* check it is a valid IPv6 Address */
			/*
			 * tmp_ptr =
			 * &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
			 * inet_ntop(AF_INET6, tmp_ptr, ip_str,
			 *           INET6_ADDRSTRLEN);
			 * C_DEBUG("Get %s IPv6 Address %s\n",
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
		C_DEBUG("generated phyaddr: %s.\n", addrstr);
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
	unsigned	timeout;
	int		rc = 0;

	C_DEBUG("initializing crt_gdata...\n");

	/*
	 * avoid size mis-matching between client/server side
	 * /see crt_proc_uuid_t().
	 */
	C_CASSERT(sizeof(uuid_t) == 16);

	CRT_INIT_LIST_HEAD(&crt_gdata.cg_ctx_list);

	rc = pthread_rwlock_init(&crt_gdata.cg_rwlock, NULL);
	C_ASSERT(rc == 0);

	crt_gdata.cg_ctx_num = 0;
	crt_gdata.cg_refcount = 0;
	crt_gdata.cg_inited = 0;
	crt_gdata.cg_addr = NULL;
	crt_gdata.cg_verbs = false;
	crt_gdata.cg_multi_na = false;

	timeout = 0;
	crt_getenv_int("CRT_TIMEOUT", &timeout);
	if (timeout == 0 || timeout > 3600)
		crt_gdata.cg_timeout = CRT_DEFAULT_TIMEOUT_S;
	else
		crt_gdata.cg_timeout = timeout;
	C_DEBUG("set the global timeout value as %d second.\n",
		crt_gdata.cg_timeout);

	gdata_init_flag = 1;
}

int
crt_init(crt_group_id_t grpid, uint32_t flags)
{
	crt_phy_addr_t	addr = NULL, addr_env;
	struct timeval	now;
	unsigned int	seed;
	size_t		len;
	bool		server, allow_singleton = false;
	int		rc = 0;

	server = flags & CRT_FLAG_BIT_SERVER;

	if (grpid != NULL) {
		len = strlen(grpid);
		if (len == 0 || len > CRT_GROUP_ID_MAX_LEN) {
			C_PRINT_ERR("invalid grpid length %zu.\n", len);
			C_GOTO(out, rc = -CER_INVAL);
		}
		if (!server) {
			if (strcmp(grpid, CRT_DEFAULT_SRV_GRPID) == 0) {
				C_PRINT_ERR("invalid client grpid (same as "
					    "CRT_DEFAULT_SRV_GRPID).\n");
				C_GOTO(out, rc = -CER_INVAL);
			}
		} else {
			if (strcmp(grpid, CRT_DEFAULT_CLI_GRPID) == 0) {
				C_PRINT_ERR("invalid server grpid (same as "
					    "CRT_DEFAULT_CLI_GRPID).\n");
				C_GOTO(out, rc = -CER_INVAL);
			}
		}
	}

	if (gdata_init_flag == 0) {
		rc = pthread_once(&gdata_init_once, data_init);
		if (rc != 0) {
			C_PRINT_ERR("crt_init failed, rc(%d) - %s.\n",
				    rc, strerror(rc));
			C_GOTO(out, rc = -rc);
		}
	}
	C_ASSERT(gdata_init_flag == 1);

	pthread_rwlock_wrlock(&crt_gdata.cg_rwlock);
	if (crt_gdata.cg_inited == 0) {
		/* feed a seed for pseudo-random number generator */
		gettimeofday(&now, NULL);
		seed = (unsigned int)(now.tv_sec * 1000000 + now.tv_usec);
		srandom(seed);

		crt_gdata.cg_server = server;
		if (server == true)
			crt_gdata.cg_multi_na = true;

		if (!server) {
			crt_getenv_bool(CRT_ALLOW_SINGLETON_ENV,
					&allow_singleton);
			if ((flags & CRT_FLAG_BIT_SINGLETON) != 0 &&
			    allow_singleton)
				crt_gdata.cg_singleton = true;
		}

		rc = crt_log_init();
		if (rc != 0) {
			C_PRINT_ERR("crt_log_init failed, rc: %d.\n", rc);
			C_GOTO(out, rc);
		}

		addr_env = (crt_phy_addr_t)getenv(CRT_PHY_ADDR_ENV);
		if (addr_env == NULL) {
			C_DEBUG("ENV %s not found.\n", CRT_PHY_ADDR_ENV);
			goto do_init;
		} else{
			C_DEBUG("EVN %s: %s.\n",
				CRT_PHY_ADDR_ENV, addr_env);
		}
		if (strncmp(addr_env, "bmi+tcp", 7) == 0) {
			if (strcmp(addr_env, "bmi+tcp") == 0) {
				rc = crt_gen_bmi_phyaddr(&addr);
				if (rc == 0) {
					C_DEBUG("ENV %s (%s), generated "
						"a BMI phyaddr: %s.\n",
						CRT_PHY_ADDR_ENV, addr_env,
						addr);
				} else {
					C_ERROR("crt_gen_bmi_phyaddr failed, "
						"rc: %d.\n", rc);
					C_GOTO(out, rc);
				}
			} else {
				C_DEBUG("ENV %s found, use addr %s.\n",
					CRT_PHY_ADDR_ENV, addr_env);
				addr = strdup(addr_env);
				if (addr == NULL) {
					C_ERROR("strdup failed.\n");
					C_GOTO(out, rc = -CER_NOMEM);
				}
			}
			C_ASSERT(addr != NULL);
			crt_gdata.cg_multi_na = false;
		} else if (strncmp(addr_env, "cci+verbs", 9) == 0) {
			crt_gdata.cg_verbs = true;
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
		crt_gdata.cg_addr = addr;
		crt_gdata.cg_addr_len = strlen(addr);

		rc = crt_grp_init(grpid);
		if (rc != 0) {
			C_ERROR("crt_grp_init failed, rc: %d.\n", rc);
			crt_hg_fini();
			free(crt_gdata.cg_addr);
			crt_gdata.cg_addr = NULL;
			C_GOTO(unlock, rc);
		}

		rc = crt_opc_map_create(CRT_OPC_MAP_BITS);
		if (rc != 0) {
			C_ERROR("crt_opc_map_create failed rc: %d.\n", rc);
			crt_hg_fini();
			crt_grp_fini();
			free(crt_gdata.cg_addr);
			crt_gdata.cg_addr = NULL;
			C_GOTO(unlock, rc);
		}
		C_ASSERT(crt_gdata.cg_opc_map != NULL);

		crt_gdata.cg_inited = 1;
	} else {
		if (crt_gdata.cg_server == false && server == true) {
			C_ERROR("CRT initialized as client, cannot set as "
				"server again.\n");
			C_GOTO(unlock, rc = -CER_INVAL);
		}
	}

	crt_gdata.cg_refcount++;

unlock:
	pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
out:
	if (rc != 0)
		C_PRINT_ERR("crt_init failed, rc: %d.\n", rc);
	return rc;
}

bool
crt_initialized()
{
	return (gdata_init_flag == 1) && (crt_gdata.cg_inited == 1);
}

int
crt_finalize(void)
{
	int rc = 0;

	pthread_rwlock_wrlock(&crt_gdata.cg_rwlock);

	if (!crt_initialized()) {
		C_ERROR("cannot finalize before initializing.\n");
		pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
		C_GOTO(out, rc = -CER_UNINIT);
	}
	if (crt_gdata.cg_ctx_num > 0) {
		C_ASSERT(!crt_context_empty(CRT_LOCKED));
		C_ERROR("cannot finalize, current ctx_num(%d).\n",
			crt_gdata.cg_ctx_num);
		pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
		C_GOTO(out, rc = -CER_NO_PERM);
	} else {
		C_ASSERT(crt_context_empty(CRT_LOCKED));
	}

	crt_gdata.cg_refcount--;
	if (crt_gdata.cg_refcount == 0) {
		rc = crt_grp_fini();
		if (rc != 0) {
			C_ERROR("crt_grp_fini failed, rc: %d.\n", rc);
			crt_gdata.cg_refcount++;
			pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
			C_GOTO(out, rc);
		}

		rc = crt_hg_fini();
		if (rc != 0) {
			C_ERROR("crt_hg_fini failed rc: %d.\n", rc);
			crt_gdata.cg_refcount++;
			pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
			C_GOTO(out, rc);
		}

		C_ASSERT(crt_gdata.cg_addr != NULL);
		free(crt_gdata.cg_addr);
		crt_gdata.cg_addr = NULL;
		crt_gdata.cg_server = false;

		crt_opc_map_destroy(crt_gdata.cg_opc_map);

		pthread_rwlock_unlock(&crt_gdata.cg_rwlock);

		rc = pthread_rwlock_destroy(&crt_gdata.cg_rwlock);
		if (rc != 0) {
			C_ERROR("failed to destroy cg_rwlock, rc: %d.\n", rc);
			C_GOTO(out, rc = -rc);
		}

		/* allow the same program to re-initialize */
		crt_gdata.cg_refcount = 0;
		crt_gdata.cg_inited = 0;
		gdata_init_once = PTHREAD_ONCE_INIT;
		gdata_init_flag = 0;

		crt_log_fini();
	} else {
		pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
	}

out:
	if (rc != 0)
		C_PRINT_ERR("crt_finalize failed, rc: %d.\n", rc);
	return rc;
}
