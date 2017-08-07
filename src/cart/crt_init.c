/* Copyright (C) 2016-2017 Intel Corporation
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
struct crt_plugin_gdata crt_plugin_gdata;

/* first step init - for initializing crt_gdata */
static void data_init(void)
{
	uint32_t	timeout;
	uint32_t	credits;
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
	crt_gdata.cg_na_plugin = CRT_NA_CCI_TCP;
	crt_gdata.cg_multi_na = false;

	timeout = 0;
	crt_getenv_int("CRT_TIMEOUT", &timeout);
	if (timeout == 0 || timeout > 3600)
		crt_gdata.cg_timeout = CRT_DEFAULT_TIMEOUT_S;
	else
		crt_gdata.cg_timeout = timeout;
	C_DEBUG("set the global timeout value as %d second.\n",
		crt_gdata.cg_timeout);

	credits = CRT_DEFAULT_CREDITS_PER_EP_CTX;
	crt_getenv_int("CRT_CREDIT_EP_CTX", &credits);
	if (credits == 0) {
		C_DEBUG("CRT_CREDIT_EP_CTX set as 0, flow control disabled.\n");
	} else if (credits > CRT_MAX_CREDITS_PER_EP_CTX) {
		C_DEBUG("ENV CRT_CREDIT_EP_CTX's value %d exceed max allowed "
			"value, use %d for flow control.\n",
			credits, CRT_MAX_CREDITS_PER_EP_CTX);
		credits = CRT_MAX_CREDITS_PER_EP_CTX;
	} else {
		C_DEBUG("CRT_CREDIT_EP_CTX set as %d for flow control.\n",
			credits);
	}
	crt_gdata.cg_credit_ep_ctx = credits;
	C_ASSERT(crt_gdata.cg_credit_ep_ctx >= 0 &&
		 crt_gdata.cg_credit_ep_ctx <= CRT_MAX_CREDITS_PER_EP_CTX);

	gdata_init_flag = 1;
}

void
crt_plugin_init(void)
{
	C_ASSERT(crt_plugin_gdata.cpg_inited == 0);

	/** init the lists */
	CRT_INIT_LIST_HEAD(&crt_plugin_gdata.cpg_prog_cbs);
	CRT_INIT_LIST_HEAD(&crt_plugin_gdata.cpg_timeout_cbs);
	CRT_INIT_LIST_HEAD(&crt_plugin_gdata.cpg_event_cbs);
	pthread_rwlock_init(&crt_plugin_gdata.cpg_prog_rwlock, NULL);
	pthread_rwlock_init(&crt_plugin_gdata.cpg_timeout_rwlock, NULL);
	pthread_rwlock_init(&crt_plugin_gdata.cpg_event_rwlock, NULL);
	crt_plugin_pmix_init();
	crt_plugin_gdata.cpg_inited = 1;
}

int
crt_init(crt_group_id_t grpid, uint32_t flags)
{
	crt_phy_addr_t	addr = NULL, addr_env;
	struct timeval	now;
	unsigned int	seed;
	bool		server;
	int		rc = 0;

	server = flags & CRT_FLAG_BIT_SERVER;

	/* crt_log_init is reference counted */
	rc = crt_log_init();
	if (rc != 0) {
		C_PRINT_ERR("crt_log_init failed, rc: %d.\n", rc);
		return rc;
	}

	if (grpid != NULL) {
		if (crt_validate_grpid(grpid) != 0) {
			C_ERROR("grpid contains invalid characters "
				"or is too long\n");
			C_GOTO(out, rc = -CER_INVAL);
		}
		if (!server) {
			if (strcmp(grpid, CRT_DEFAULT_SRV_GRPID) == 0) {
				C_ERROR("invalid client grpid (same as "
					"CRT_DEFAULT_SRV_GRPID).\n");
				C_GOTO(out, rc = -CER_INVAL);
			}
		} else {
			if (strcmp(grpid, CRT_DEFAULT_CLI_GRPID) == 0) {
				C_ERROR("invalid server grpid (same as "
					"CRT_DEFAULT_CLI_GRPID).\n");
				C_GOTO(out, rc = -CER_INVAL);
			}
		}
	}

	if (gdata_init_flag == 0) {
		rc = pthread_once(&gdata_init_once, data_init);
		if (rc != 0) {
			C_ERROR("crt_init failed, rc(%d) - %s.\n",
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

		if ((flags & CRT_FLAG_BIT_SINGLETON) != 0)
			crt_gdata.cg_singleton = true;

		if (crt_plugin_gdata.cpg_inited == 0)
			crt_plugin_init();

		addr_env = (crt_phy_addr_t)getenv(CRT_PHY_ADDR_ENV);
		if (addr_env == NULL) {
			C_DEBUG("ENV %s not found.\n", CRT_PHY_ADDR_ENV);
			goto do_init;
		} else{
			C_DEBUG("EVN %s: %s.\n",
				CRT_PHY_ADDR_ENV, addr_env);
		}

		if (strncmp(addr_env, "cci+verbs", 9) == 0) {
			crt_gdata.cg_na_plugin = CRT_NA_CCI_VERBS;
		} else if (strncmp(addr_env, "ofi+", 4) == 0) {
			if (strncmp(addr_env, "ofi+sockets", 11) == 0) {
				crt_gdata.cg_na_plugin = CRT_NA_OFI_SOCKETS;
			} else if (strncmp(addr_env, "ofi+verbs", 9) == 0) {
				crt_gdata.cg_na_plugin = CRT_NA_OFI_VERBS;
			} else if (strncmp(addr_env, "ofi+psm2", 8) == 0) {
				crt_gdata.cg_na_plugin = CRT_NA_OFI_PSM2;
			} else if (strncmp(addr_env, "ofi+gni", 7) == 0) {
				crt_gdata.cg_na_plugin = CRT_NA_OFI_GNI;
			} else {
				C_ERROR("invalid CRT_PHY_ADDR_STR %s.\n",
					addr_env);
				C_GOTO(out, rc = -CER_INVAL);
			}
			rc = crt_na_ofi_config_init();
			if (rc != 0) {
				C_ERROR("crt_na_ofi_config_init failed, "
					"rc: %d.\n", rc);
				C_GOTO(out, rc);
			}
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

		crt_self_test_init();

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
	if (rc != 0) {
		C_ERROR("crt_init failed, rc: %d.\n", rc);
		crt_log_fini();
	}
	return rc;
}

bool
crt_initialized()
{
	return (gdata_init_flag == 1) && (crt_gdata.cg_inited == 1);
}

void
crt_plugin_fini(void)
{
	crt_list_t			*curr_node;
	crt_list_t			*tmp_node;
	struct crt_prog_cb_priv		*prog_cb_priv;
	struct crt_timeout_cb_priv	*timeout_cb_priv;
	struct crt_event_cb_priv	*event_cb_priv;

	C_ASSERT(crt_plugin_gdata.cpg_inited == 1);

	crt_list_for_each_safe(curr_node, tmp_node,
			       &crt_plugin_gdata.cpg_prog_cbs) {
		crt_list_del(curr_node);
		prog_cb_priv = container_of(curr_node, struct crt_prog_cb_priv,
					    cpcp_link);
		C_FREE_PTR(prog_cb_priv);
	}
	crt_list_for_each_safe(curr_node, tmp_node,
			       &crt_plugin_gdata.cpg_timeout_cbs) {
		crt_list_del(curr_node);
		timeout_cb_priv =
			container_of(curr_node, struct crt_timeout_cb_priv,
				     ctcp_link);
		C_FREE_PTR(timeout_cb_priv);
	}
	crt_list_for_each_safe(curr_node, tmp_node,
			       &crt_plugin_gdata.cpg_event_cbs) {
		crt_list_del(curr_node);
		event_cb_priv =
			container_of(curr_node, struct crt_event_cb_priv,
				     cecp_link);
		C_FREE(event_cb_priv->cecp_codes,
		       event_cb_priv->cecp_ncodes
		       *sizeof(*event_cb_priv->cecp_codes));
		C_FREE_PTR(event_cb_priv);
	}
	pthread_rwlock_destroy(&crt_plugin_gdata.cpg_prog_rwlock);
	pthread_rwlock_destroy(&crt_plugin_gdata.cpg_timeout_rwlock);
	pthread_rwlock_destroy(&crt_plugin_gdata.cpg_event_rwlock);
	crt_plugin_pmix_fini();
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

		if (crt_plugin_gdata.cpg_inited == 1)
			crt_plugin_fini();
		if (crt_gdata.cg_na_plugin == CRT_NA_OFI_SOCKETS)
			crt_na_ofi_config_fini();
	} else {
		pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
	}

out:
	if (rc != 0)
		C_ERROR("crt_finalize failed, rc: %d.\n", rc);

	/* crt_log_fini is reference counted */
	crt_log_fini();

	return rc;
}

/* global NA OFI plugin configuration */
struct na_ofi_config crt_na_ofi_conf;

static inline na_bool_t is_integer_str(char *str)
{
	char *p;

	p = str;
	if (p == NULL || strlen(p) == 0)
		return NA_FALSE;

	while (*p != '\0') {
		if (*p <= '9' && *p >= '0') {
			p++;
			continue;
		} else {
			return NA_FALSE;
		}
	}

	return NA_TRUE;
}

static inline int
crt_get_port(int *port)
{
	int			socketfd;
	struct sockaddr_in	tmp_socket;
	socklen_t		slen = sizeof(struct sockaddr);
	int			rc;

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

	C_ASSERT(port != NULL);
	*port = ntohs(tmp_socket.sin_port);
	C_DEBUG("get a port: %d.\n", *port);

out:
	return rc;
}

int crt_na_ofi_config_init(void)
{
	char *port_str;
	char *interface;
	int port;
	struct ifaddrs *if_addrs = NULL;
	struct ifaddrs *ifa = NULL;
	void *tmp_ptr;
	const char *ip_str = NULL;
	int rc = 0;

	interface = getenv("OFI_INTERFACE");
	if (interface != NULL && strlen(interface) > 0) {
		crt_na_ofi_conf.noc_interface = strdup(interface);
		if (crt_na_ofi_conf.noc_interface == NULL) {
			C_ERROR("cannot allocate memory for noc_interface.");
			C_GOTO(out, rc = -CER_NOMEM);
		}
	} else {
		crt_na_ofi_conf.noc_interface = NULL;
		C_ERROR("ENV OFI_INTERFACE not set.");
		C_GOTO(out, rc = -CER_INVAL);
	}

	rc = getifaddrs(&if_addrs);
	if (rc != 0) {
		C_ERROR("cannot getifaddrs, errno: %d(%s).\n",
			     errno, strerror(errno));
		C_GOTO(out, rc = -CER_PROTO);
	}

	for (ifa = if_addrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (strcmp(ifa->ifa_name, crt_na_ofi_conf.noc_interface))
			continue;
		if (ifa->ifa_addr == NULL)
			continue;
		memset(crt_na_ofi_conf.noc_ip_str, 0, INET_ADDRSTRLEN);
		if (ifa->ifa_addr->sa_family == AF_INET) {
			/* check it is a valid IPv4 Address */
			tmp_ptr =
			&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
			ip_str = inet_ntop(AF_INET, tmp_ptr,
					   crt_na_ofi_conf.noc_ip_str,
					   INET_ADDRSTRLEN);
			if (ip_str == NULL) {
				C_ERROR("inet_ntop failed, errno: %d(%s).\n",
					errno, strerror(errno));
				freeifaddrs(if_addrs);
				C_GOTO(out, rc = -CER_PROTO);
			}
			/*
			 * C_DEBUG("Get interface %s IPv4 Address %s\n",
			 * ifa->ifa_name, na_ofi_conf.noc_ip_str);
			 */
			break;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			/* check it is a valid IPv6 Address */
			/*
			 * tmp_ptr =
			 * &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
			 * inet_ntop(AF_INET6, tmp_ptr, na_ofi_conf.noc_ip_str,
			 *           INET6_ADDRSTRLEN);
			 * C_DEBUG("Get %s IPv6 Address %s\n",
			 *         ifa->ifa_name, na_ofi_conf.noc_ip_str);
			 */
		}
	}
	freeifaddrs(if_addrs);
	if (ip_str == NULL) {
		C_ERROR("no IP addr found.\n");
		C_GOTO(out, rc = -CER_PROTO);
	}

	rc = crt_get_port(&port);
	if (rc != 0) {
		C_ERROR("crt_get_port failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}

	port_str = getenv("OFI_PORT");
	if (crt_is_service() && port_str != NULL && strlen(port_str) > 0) {
		if (!is_integer_str(port_str)) {
			C_DEBUG("ignore invalid OFI_PORT %s.", port_str);
		} else {
			port = atoi(port_str);
			C_DEBUG("OFI_PORT %d, use it as service port.\n", port);
		}
	}
	crt_na_ofi_conf.noc_port = port;

out:
	return rc;
}

void crt_na_ofi_config_fini(void)
{
	if (crt_na_ofi_conf.noc_interface != NULL) {
		free(crt_na_ofi_conf.noc_interface);
		crt_na_ofi_conf.noc_interface = NULL;
	}
	crt_na_ofi_conf.noc_port = 0;
}
