/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2015-2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>

#include "cxip.h"
#include "cxip_test_common.h"

static const char cxip_dom_fmt[] = "cxi%d";

static char *get_dom_name(int if_idx)
{
	char *dom;
	int ret;

	ret = asprintf(&dom, cxip_dom_fmt, if_idx);
	cr_assert(ret > 0);

	return dom;
}

TestSuite(getinfo_env_vars, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(getinfo_env_vars, default_tx_size)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;
	struct fi_info *iter;

	ret = setenv("FI_CXI_DEFAULT_TX_SIZE", "17", 1);
	cr_assert(ret == 0);

	hints = fi_allocinfo();
	cr_assert(hints);

	hints->domain_attr->mr_mode = FI_MR_ENDPOINT;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert(hints->fabric_attr->prov_name);

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 NULL, NULL, cxit_flags, hints, &info);
	cr_assert(ret == FI_SUCCESS);

	iter = info;
	while (iter) {
		cr_assert(info->tx_attr->size == 17);
		iter = iter->next;
	}

	fi_freeinfo(info);
	fi_freeinfo(hints);
}

Test(getinfo_env_vars, default_rx_size)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;
	struct fi_info *iter;

	ret = setenv("FI_CXI_DEFAULT_RX_SIZE", "17", 1);
	cr_assert(ret == 0);

	hints = fi_allocinfo();
	cr_assert(hints);

	hints->domain_attr->mr_mode = FI_MR_ENDPOINT;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert(hints->fabric_attr->prov_name);

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 NULL, NULL, cxit_flags, hints, &info);
	cr_assert(ret == FI_SUCCESS);

	iter = info;
	while (iter) {
		cr_assert(info->rx_attr->size == 17);
		iter = iter->next;
	}

	fi_freeinfo(info);
	fi_freeinfo(hints);
}

TestSuite(getinfo, .init = cxit_setup_getinfo,
	  .fini = cxit_teardown_getinfo, .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test fabric selection with provider name */
Test(getinfo, prov_name)
{
	int infos = 0;

	cxit_fi_hints->fabric_attr->prov_name = strdup(cxip_prov_name);

	cxit_create_fabric_info();
	cr_assert(cxit_fi != NULL);

	/* Make sure we have at least 1 FI for each IF */
	do {
		cr_assert(!strcmp(cxit_fi->fabric_attr->prov_name,
				  cxip_prov_name));
		infos++;
	} while ((cxit_fi = cxit_fi->next));
	cr_assert(infos >= cxit_n_ifs);
}

/* Test fabric selection with domain name */
Test(getinfo, dom_name)
{
	int infos = 0;
	struct cxip_if *if_entry;
	struct slist_entry *entry, *prev __attribute__ ((unused));

	slist_foreach(&cxip_if_list, entry, prev) {
		if_entry = container_of(entry, struct cxip_if, if_entry);
		infos = 0;

		cxit_node = get_dom_name(if_entry->info->dev_id);
		cxit_flags = FI_SOURCE;
		printf("searching %s\n", cxit_node);

		cxit_create_fabric_info();
		cr_assert(cxit_fi != NULL);

		/* Make sure we have at least 1 FI for each IF */
		do {
			cr_expect(!strcmp(cxit_fi->domain_attr->name,
					  cxit_node),
					  "%s != %s\n",
					  cxit_fi->domain_attr->name,
					  cxit_fi_hints->domain_attr->name);

			cr_assert(!strcmp(cxit_fi->fabric_attr->prov_name,
					  cxip_prov_name));

			cr_assert(!strcmp(cxit_fi->fabric_attr->name,
				  cxip_prov_name));

			infos++;
		} while ((cxit_fi = cxit_fi->next));
		cr_assert(infos >= 1);

		cxit_destroy_fabric_info();
	}
	cr_assert(infos >= 1);
}

/* Test fabric selection with fabric name */
Test(getinfo, fab_name)
{
	int infos = 0;
	struct slist_entry *entry, *prev __attribute__ ((unused));
	struct fi_info *fi;

	slist_foreach(&cxip_if_list, entry, prev) {
		infos = 0;

		cxit_fi_hints->fabric_attr->name = strdup(cxip_prov_name);

		cxit_create_fabric_info();
		cr_assert(cxit_fi != NULL);

		fi = cxit_fi;
		do {
			/* Not all providers can be trusted to filter by fabric
			 * name */
			if (strcmp(fi->fabric_attr->prov_name,
				   cxip_prov_name))
				continue;

			cr_assert(!strcmp(fi->fabric_attr->name,
					  fi->fabric_attr->name));

			infos++;
		} while ((fi = fi->next));

		cxit_destroy_fabric_info();
	}
	cr_assert(infos);
}

Test(getinfo, prov_version)
{
	cxit_fi_hints->fabric_attr->prov_name = strdup(cxip_prov_name);

	cxit_create_fabric_info();
	cr_assert(cxit_fi != NULL);
	cr_assert(cxit_fi->fabric_attr != NULL);

	cr_assert(FI_MAJOR(cxit_fi->fabric_attr->prov_version) ==
		  CXIP_MAJOR_VERSION,
		  "Major version wwrong, expected %d, version returned %d",
		  CXIP_MAJOR_VERSION,
		  FI_MAJOR(cxit_fi->fabric_attr->prov_version));
	cr_assert(FI_MINOR(cxit_fi->fabric_attr->prov_version) ==
		  CXIP_MINOR_VERSION,
		  "Minor version wwrong, expected %d, version returned %d",
		  CXIP_MINOR_VERSION,
		  FI_MINOR(cxit_fi->fabric_attr->prov_version));
}

Test(getinfo, valid_av_auth_key)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;

	hints = fi_allocinfo();
	cr_assert_not_null(hints, "fi_allocinfo failed");

	hints->caps = FI_MSG | FI_TAGGED | FI_REMOTE_COMM;
	hints->domain_attr->auth_key_size = FI_AV_AUTH_KEY;
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert_not_null(hints, "strdup failed");

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), "cxi0",
			 NULL, FI_SOURCE, hints, &info);
	cr_assert_eq(ret, FI_SUCCESS, "fi_getinfo failed: %d", ret);

	fi_freeinfo(hints);
	fi_freeinfo(info);
}

Test(getinfo, invalid_av_auth_key_not_null_domain_auth_key)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;

	hints = fi_allocinfo();
	cr_assert_not_null(hints, "fi_allocinfo failed");

	hints->caps = FI_MSG | FI_TAGGED | FI_REMOTE_COMM;
	hints->domain_attr->auth_key_size = FI_AV_AUTH_KEY;
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;
	hints->domain_attr->auth_key = (void *)hints;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert_not_null(hints, "strdup failed");

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), "cxi0",
			 NULL, FI_SOURCE, hints, &info);
	cr_assert_eq(ret, -FI_ENODATA, "fi_getinfo failed: %d", ret);

	hints->domain_attr->auth_key = NULL;

	fi_freeinfo(hints);
	fi_freeinfo(info);
}

Test(getinfo, invalid_av_auth_key_not_null_ep_auth_key)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;

	hints = fi_allocinfo();
	cr_assert_not_null(hints, "fi_allocinfo failed");

	hints->caps = FI_MSG | FI_TAGGED | FI_REMOTE_COMM;
	hints->domain_attr->auth_key_size = FI_AV_AUTH_KEY;
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;
	hints->ep_attr->auth_key = (void *)hints;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert_not_null(hints, "strdup failed");

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), "cxi0",
			 NULL, FI_SOURCE, hints, &info);
	cr_assert_eq(ret, -FI_ENODATA, "fi_getinfo failed: %d", ret);

	hints->ep_attr->auth_key = NULL;

	fi_freeinfo(hints);
	fi_freeinfo(info);
}

Test(getinfo, invalid_av_auth_key_not_zero_ep_auth_key_size)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;

	hints = fi_allocinfo();
	cr_assert_not_null(hints, "fi_allocinfo failed");

	hints->caps = FI_MSG | FI_TAGGED | FI_REMOTE_COMM;
	hints->domain_attr->auth_key_size = FI_AV_AUTH_KEY;
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;
	hints->ep_attr->auth_key_size = 1;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert_not_null(hints, "strdup failed");

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), "cxi0",
			 NULL, FI_SOURCE, hints, &info);
	cr_assert_eq(ret, -FI_ENODATA, "fi_getinfo failed: %d", ret);

	fi_freeinfo(hints);
	fi_freeinfo(info);
}

Test(getinfo, valid_multiple_auth_keys_per_ep)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;

	hints = fi_allocinfo();
	cr_assert_not_null(hints, "fi_allocinfo failed");

	hints->domain_attr->max_ep_auth_key = 2;
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;
	hints->caps = FI_MSG;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert_not_null(hints, "strdup failed");

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), "cxi0",
			 NULL, FI_SOURCE, hints, &info);
	cr_assert_eq(ret, FI_SUCCESS, "fi_getinfo failed: %d", ret);

	cr_assert_eq(info->domain_attr->max_ep_auth_key,
		     hints->domain_attr->max_ep_auth_key);

	fi_freeinfo(hints);
	fi_freeinfo(info);
}

Test(getinfo, invalid_multiple_auth_keys_per_ep)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;

	hints = fi_allocinfo();
	cr_assert_not_null(hints, "fi_allocinfo failed");

	hints->domain_attr->max_ep_auth_key = (1 << 16);
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;
	hints->caps = FI_MSG;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert_not_null(hints, "strdup failed");

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), "cxi0",
			 NULL, FI_SOURCE, hints, &info);
	cr_assert_eq(ret, -FI_ENODATA, "fi_getinfo failed: %d", ret);

	fi_freeinfo(hints);
	fi_freeinfo(info);
}

Test(getinfo, invalid_fi_directed_recv_with_multiple_auth_keys_per_ep)
{
	int ret;
	struct fi_info *hints;
	struct fi_info *info;

	hints = fi_allocinfo();
	cr_assert_not_null(hints, "fi_allocinfo failed");

	hints->domain_attr->max_ep_auth_key = 2;
	hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;
	hints->caps = FI_MSG | FI_DIRECTED_RECV;
	hints->fabric_attr->prov_name = strdup("cxi");
	cr_assert_not_null(hints, "strdup failed");

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), "cxi0",
			 NULL, FI_SOURCE, hints, &info);
	cr_assert_eq(ret, -FI_ENODATA, "fi_getinfo failed: %d", ret);

	fi_freeinfo(hints);
	fi_freeinfo(info);
}

TestSuite(getinfo_infos, .timeout = CXIT_DEFAULT_TIMEOUT);

#define MAX_INFOS	24

struct info_check {
	int mr_mode;
	uint32_t format;
	uint32_t protocol;
	size_t max_ep_auth_key;
};

Test(getinfo_infos, nohints)
{
	int num_info;
	int i;
	int info_per_if = 0;
	struct fi_info *fi_ptr;
	char *dom_name;
	char *odp;
	struct info_check infos[MAX_INFOS];
	size_t max_ep_auth_key;
	uint32_t proto;
	uint32_t format;

	cxit_init();
	cr_assert(!cxit_fi_hints, "hints not NULL");

	cxit_create_fabric_info();
	cr_assert(cxit_fi != NULL);

	for (i = 0; i < MAX_INFOS; i++) {
		infos[i].format = 0;
		infos[i].mr_mode = -1;
	}

	odp = getenv("FI_CXI_ODP");

	/* By default when no hints are specified, each interface
	 * should can have 8 HPC fi_info and 8 CS fi_info.
	 */
	for (i = 0; i < 4; i++) {
		if (i == 0 || i == 2)
			max_ep_auth_key = 1;
		else
			max_ep_auth_key = 4;

		/* Set protocol based on compatibility. Note FI_PROTO_CXI_RNR
		 * does not exist if only old address format/protocol values
		 * are used.
		 */
		if (i < 2)
			proto = FI_PROTO_CXI;
		else
			proto = FI_PROTO_CXI_RNR;

		format = FI_ADDR_CXI;
		infos[info_per_if].mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED |
					FI_MR_PROV_KEY;
		infos[info_per_if].format = format;
		infos[info_per_if].max_ep_auth_key = max_ep_auth_key;
		infos[info_per_if].protocol = proto;
		info_per_if++;

		infos[info_per_if].mr_mode = FI_MR_ENDPOINT | FI_MR_ALLOCATED;
		infos[info_per_if].format = format;
		infos[info_per_if].max_ep_auth_key = max_ep_auth_key;
		infos[info_per_if].protocol = proto;
		info_per_if++;

		/* Add ODP versions if enabled */
		if (odp && strtol(odp, NULL, 10)) {
			infos[info_per_if].format = format;
			infos[info_per_if].mr_mode = FI_MR_ENDPOINT | FI_MR_PROV_KEY;
			infos[info_per_if].max_ep_auth_key = max_ep_auth_key;
			infos[info_per_if].protocol = proto;
			info_per_if++;

			infos[info_per_if].format = format;
			infos[info_per_if].mr_mode = FI_MR_ENDPOINT;
			infos[info_per_if].max_ep_auth_key = max_ep_auth_key;
			infos[info_per_if].protocol = proto;
			info_per_if++;
		}
	}

	cr_assert(info_per_if <= MAX_INFOS, "Too many infos");

	fi_ptr = cxit_fi;

	while (fi_ptr) {
		/* Only concerned with CXI */
		if (strcmp(fi_ptr->fabric_attr->prov_name, cxip_prov_name)) {
			fi_ptr = fi_ptr->next;
			continue;
		}

		dom_name = fi_ptr->domain_attr->name;
		num_info = 0;

		/* Each info for the same NIC as the same domain name */
		while (fi_ptr) {
			/* Different interface detected */
			if (strcmp(dom_name, fi_ptr->domain_attr->name))
				break;

			num_info++;
			cr_assert(num_info <= MAX_INFOS,
				  "too many fi_info %d", num_info);

			cr_assert(infos[num_info - 1].mr_mode ==
				  fi_ptr->domain_attr->mr_mode,
				  "expected MR mode %x got %x",
				  infos[num_info - 1].mr_mode,
				  fi_ptr->domain_attr->mr_mode);

			cr_assert(infos[num_info - 1].format ==
				  fi_ptr->addr_format,
				  "expected addr_fomrat %u got %u",
				  infos[num_info - 1].format,
				  fi_ptr->addr_format);

			fi_ptr = fi_ptr->next;
		}

		cr_assert(num_info == info_per_if,
			  "Wrong number of fi_info %d got %d",
			  num_info, info_per_if);
	}
	cxit_destroy_fabric_info();
}

void getinfo_infos_hints(uint32_t proto)
{
	int num_info;
	int i;
	int info_per_if = 0;
	struct fi_info *fi_ptr;
	char *dom_name;
	char *odp;
	int odp_val;
	struct info_check infos[3];

	odp = getenv("FI_CXI_ODP");
	odp_val = !odp ? 0 : strtol(odp, NULL, 10);

	cr_assert(cxit_fi_hints == NULL, "hints not null");
	cxit_setup_getinfo_proto(proto);
	cr_assert(cxit_fi_hints != NULL, "hints still null");
	cr_assert(cxit_fi_hints->ep_attr->protocol == proto,
		  "hints proto %d expected %d failure",
		  cxit_fi_hints->ep_attr->protocol, proto);

	cxit_create_fabric_info();
	cr_assert(cxit_fi != NULL);

	for (i = 0; i < 3; i++) {
		infos[i].format = 0;
		infos[i].protocol = 0;
		infos[i].mr_mode = -1;
	}

	/* We have address format FI_ADDR_CXI */
	infos[info_per_if].mr_mode = FI_MR_ENDPOINT;
	if (!odp_val)
		infos[info_per_if].mr_mode |= FI_MR_ALLOCATED;
	if (cxit_prov_key)
		infos[info_per_if].mr_mode |= FI_MR_PROV_KEY;

	infos[info_per_if].format = FI_ADDR_CXI;
	infos[info_per_if].protocol = proto;
	info_per_if++;

	fi_ptr = cxit_fi;

	while (fi_ptr) {
		/* Should only be CXI provider */
		cr_assert(!strcmp(fi_ptr->fabric_attr->prov_name,
				  cxip_prov_name), "non-cxi provider");

		dom_name = fi_ptr->domain_attr->name;
		num_info = 0;

		/* Each info for the same NIC as the same domain name */
		while (fi_ptr) {
			/* Different interface detected */
			if (strcmp(dom_name, fi_ptr->domain_attr->name))
				break;

			num_info++;
			cr_assert(num_info <= 3, "too many fi_info %d",
				  num_info);
			cr_assert(infos[num_info - 1].mr_mode ==
				  fi_ptr->domain_attr->mr_mode,
				  "expected MR mode %x got %x",
				  infos[num_info - 1].mr_mode,
				  fi_ptr->domain_attr->mr_mode);

			cr_assert(infos[num_info - 1].format ==
				  fi_ptr->addr_format,
				  "expected addr_fomrat %u got %u",
				  infos[num_info - 1].format,
				  fi_ptr->addr_format);

			fi_ptr = fi_ptr->next;
		}

		cr_assert(num_info == info_per_if,
			  "Wrong number of fi_info %d got %d",
			  num_info, info_per_if);
	}
	cxit_teardown_fabric();
}

Test(getinfo_infos, hints_default_proto)
{
	getinfo_infos_hints(0);
}

Test(getinfo_infos, hints_proto_hpc)
{
	getinfo_infos_hints(FI_PROTO_CXI);
}

Test(getinfo_infos, hints_proto_cs)
{
	getinfo_infos_hints(FI_PROTO_CXI_RNR);
}

Test(getinfo_infos, hints_no_rma)
{
	int ret;

	cxit_setup_getinfo();
	cr_assert(cxit_fi == NULL);
	cr_assert(cxit_fi_hints != NULL);

	/* Request info with hints capabilities that do not
	 * include RMA and make sure fi_info is returned
	 * even if FI_MR_ENDPOINT is not specified.
	 */
	cxit_fi_hints->domain_attr->mr_mode = 0;
	cxit_fi_hints->caps = FI_MSG | FI_TAGGED | FI_SEND | FI_RECV;

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &cxit_fi);
	cr_assert(ret == FI_SUCCESS, "fi_getinfo()");
	cr_assert(cxit_fi != NULL, "no fi_info");

	cr_assert(cxit_fi->domain_attr->mr_mode == 0, "MR mode not 0");
	cr_assert(cxit_fi->caps & (FI_MSG | FI_TAGGED | FI_SEND | FI_RECV),
		  "caps cleared");

	fi_freeinfo(cxit_fi);
	cxit_fi = NULL;

	/* Request info with hints capabilities that do not
	 * include RMA and but do include mr_mode bits. Make
	 * sure the mr_mode bits are cleared.
	 * TODO: When common code is patched to remove FI_MR_ENDPOINT,
	 * when RMA/ATOMIC is not required, add that mode to the hints.
	 */
	cxit_fi_hints->domain_attr->mr_mode = FI_MR_ALLOCATED | FI_MR_PROV_KEY;
	cxit_fi_hints->caps = FI_MSG | FI_TAGGED | FI_SEND | FI_RECV;

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &cxit_fi);
	cr_assert(ret == FI_SUCCESS, "fi_getinfo()");
	cr_assert(cxit_fi != NULL, "no fi_info");

	cr_assert(cxit_fi->domain_attr->mr_mode == 0, "MR mode not cleared");
	cr_assert(cxit_fi->caps & (FI_MSG | FI_TAGGED | FI_SEND | FI_RECV),
		  "caps cleared");

	fi_freeinfo(cxit_fi);
	cxit_fi = NULL;

	cxit_teardown_getinfo();
}

TestSuite(fabric, .init = cxit_setup_fabric, .fini = cxit_teardown_fabric,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test basic fabric creation */
Test(fabric, simple)
{
	cxit_create_fabric();
	cr_assert(cxit_fabric != NULL);

	cxit_destroy_fabric();
}
