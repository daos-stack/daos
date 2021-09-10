/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/file.h>
#include <spdk/util.h>
#include <spdk/json.h>
#include <spdk/thread.h>
#include <spdk/nvmf_spec.h>
#include "bio_internal.h"

struct
json_config_ctx {
	/* Current "subsystems" array */
	struct spdk_json_val *subsystems;
	/* Current subsystem array position in "subsystems" array */
	struct spdk_json_val *subsystems_it;

	/* Current subsystem name */
	struct spdk_json_val *subsystem_name;

	/* Current "config" array */
	struct spdk_json_val *config;
	/* Current config position in "config" array */
	struct spdk_json_val *config_it;

	/* Whole configuration file read and parsed. */
	size_t json_data_size;
	char *json_data;

	size_t values_cnt;
	struct spdk_json_val *values;
};

static int
cap_string(const struct spdk_json_val *val, void *out)
{
	const struct spdk_json_val **vptr = out;

	if (val->type != SPDK_JSON_VAL_STRING) {
		return -DER_INVAL;
	}

	*vptr = val;
	return 0;
}

static int
cap_object(const struct spdk_json_val *val, void *out)
{
	const struct spdk_json_val **vptr = out;

	if (val->type != SPDK_JSON_VAL_OBJECT_BEGIN) {
		return -DER_INVAL;
	}

	*vptr = val;
	return 0;
}


static int
cap_array_or_null(const struct spdk_json_val *val, void *out)
{
	const struct spdk_json_val **vptr = out;

	if (val->type != SPDK_JSON_VAL_ARRAY_BEGIN && val->type != SPDK_JSON_VAL_NULL) {
		return -DER_INVAL;
	}

	*vptr = val;
	return 0;
}

static struct spdk_json_val *
json_value(struct spdk_json_val *key)
{
	return key->type == SPDK_JSON_VAL_NAME ? key + 1 : NULL;
}

static struct spdk_json_object_decoder
subsystem_decoders[] = {
	{"subsystem", offsetof(struct json_config_ctx, subsystem_name), cap_string},
	{"config", offsetof(struct json_config_ctx, config), cap_array_or_null}
};

struct
config_entry {
	char			*method;
	struct spdk_json_val	*params;
};

static struct spdk_json_object_decoder
jsonrpc_cmd_decoders[] = {
	{"method", offsetof(struct config_entry, method), spdk_json_decode_string},
	{"params", offsetof(struct config_entry, params), cap_object, true}
};

static int
is_addr_in_allowlist(char *pci_addr, const struct spdk_pci_addr *allowlist,
		     int num_allowlist_devices)
{
	int			i;
	struct spdk_pci_addr    tmp;

	if (spdk_pci_addr_parse(&tmp, pci_addr) != 0) {
		D_ERROR("invalid transport address %s", pci_addr);
		return -DER_INVAL;
	}

	for (i = 0; i < num_allowlist_devices; i++) {
		if (spdk_pci_addr_compare(&tmp, &allowlist[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

/*
 * Convert a transport id in the BDF form of "5d0505:01:00.0" or something
 * similar to the VMD address in the form of "0000:5d:05.5" that can be parsed
 * by DPDK.
 *
 * \param dst String to be populated as output.
 * \param src Input bdf.
 */
static int
traddr_to_vmd(char *dst, const char *src)
{
	char traddr_tmp[SPDK_NVMF_TRADDR_MAX_LEN + 1];
	char vmd_addr[SPDK_NVMF_TRADDR_MAX_LEN + 1] = "0000:";
	char *ptr;
	const char ch = ':';
	char addr_split[3];
	int position, iteration;
	int n;

	n = snprintf(traddr_tmp, SPDK_NVMF_TRADDR_MAX_LEN, "%s", src);
	if (n < 0 || n > SPDK_NVMF_TRADDR_MAX_LEN) {
		D_ERROR("snprintf failed");
		return -DER_INVAL;
	}

	/* Only the first chunk of data from the traddr is useful */
	ptr = strchr(traddr_tmp, ch);
	if (ptr == NULL) {
		D_ERROR("Transport id not valid");
		return -DER_INVAL;
	}
	position = ptr - traddr_tmp;
	traddr_tmp[position] = '\0';

	ptr = traddr_tmp;
	iteration = 0;
	while (*ptr != '\0') {
		n = snprintf(addr_split, sizeof(addr_split), "%s", ptr);
		if (n < 0) {
			D_ERROR("snprintf failed");
			return -DER_INVAL;
		}
		strcat(vmd_addr, addr_split);

		if (iteration != 0) {
			strcat(vmd_addr, ".");
			ptr = ptr + 3;
			/** Hack alert!  Reuse existing buffer to ensure new
			 *  string is null terminated.
			 */
			addr_split[0] = ptr[0];
			addr_split[1] = '\0';
			strcat(vmd_addr, addr_split);
			break;
		}
		strcat(vmd_addr, ":");
		ptr = ptr + 2;
		iteration++;
	}
	n = snprintf(dst, SPDK_NVMF_TRADDR_MAX_LEN, "%s", vmd_addr);
	if (n < 0 || n > SPDK_NVMF_TRADDR_MAX_LEN) {
		D_ERROR("snprintf failed");
		return -DER_INVAL;
	}

	return 0;
}

static int
opts_add_pci_addr(struct spdk_env_opts *opts, char *traddr)
{
	struct spdk_pci_addr	**list = &opts->pci_allowed;
	struct spdk_pci_addr     *tmp1 = *list, *tmp2;
	size_t			  count = opts->num_pci_addr;
	int			  rc;

	rc = is_addr_in_allowlist(traddr, *list, count);
	if (rc < 0)
		return rc;
	if (rc == 1)
		return 0;

	D_REALLOC_ARRAY(tmp2, tmp1, count, count + 1);
	if (tmp2 == NULL) {
		D_ERROR("realloc error");
		return -DER_NOMEM;
	}

	*list = tmp2;
	if (spdk_pci_addr_parse(*list + count, traddr) < 0) {
		D_ERROR("Invalid address %s", traddr);
		return -DER_INVAL;
	}

	opts->num_pci_addr++;
	return 0;
}

static void *
read_file(const char *filename, size_t *size)
{
	FILE *file = fopen(filename, "r");
	void *data;

	if (file == NULL) {
		/* errno is set by fopen */
		return NULL;
	}

	data = spdk_posix_file_load(file, size);
	fclose(file);
	return data;
}

static int
read_config(const char *config_file, struct json_config_ctx *ctx)
{
	struct spdk_json_val *values = NULL;
	void *json = NULL, *end;
	ssize_t values_cnt, rc;
	size_t json_size;

	json = read_file(config_file, &json_size);
	if (!json) {
		D_ERROR("Read JSON configuration file %s failed, rc: %d",
			config_file, errno);
		return -DER_INVAL;
	}

	rc = spdk_json_parse(json, json_size, NULL, 0, &end,
			     SPDK_JSON_PARSE_FLAG_ALLOW_COMMENTS);
	if (rc < 0) {
		D_ERROR("Parsing JSON configuration failed (%zd)", rc);
		rc = -DER_INVAL;
		goto err;
	}

	values_cnt = rc;
	D_ALLOC_ARRAY(values, values_cnt);
	if (values == NULL) {
		D_ERROR("Out of memory");
		rc = -DER_NOMEM;
		goto err;
	}

	rc = spdk_json_parse(json, json_size, values, values_cnt, &end,
			     SPDK_JSON_PARSE_FLAG_ALLOW_COMMENTS);
	if (rc != values_cnt) {
		D_ERROR("Parsing JSON configuration failed (%zd)", rc);
		rc = -DER_INVAL;
		goto err;
	}

	ctx->json_data = json;
	ctx->json_data_size = json_size;

	ctx->values = values;
	ctx->values_cnt = values_cnt;

	return 0;
err:
	free(json);
	D_FREE(values);
	return rc;
}

static int
load_vmd_subsystem_config(struct json_config_ctx *ctx, bool *vmd_enabled)
{
	struct config_entry	 cfg = {};

	D_ASSERT(ctx->config_it != NULL);
	D_ASSERT(vmd_enabled != NULL);

	if (spdk_json_decode_object(ctx->config_it, jsonrpc_cmd_decoders,
				    SPDK_COUNTOF(jsonrpc_cmd_decoders), &cfg)) {
		D_ERROR("Failed to decode config entry");
		return -DER_INVAL;
	}

	if (strcmp(cfg.method, "enable_vmd") == 0)
		*vmd_enabled = true;

	free(cfg.method);
	return 0;
}

static int
load_bdev_subsystem_config(struct json_config_ctx *ctx, bool vmd_enabled,
			   struct spdk_env_opts *opts)
{
	struct config_entry	 cfg = {};
	struct spdk_json_val	*key;
	char			*traddr;
	int			 rc = 0;

	D_ASSERT(ctx->config_it != NULL);

	if (spdk_json_decode_object(ctx->config_it, jsonrpc_cmd_decoders,
				    SPDK_COUNTOF(jsonrpc_cmd_decoders), &cfg)) {
		D_ERROR("Failed to decode config entry");
		if (opts->pci_allowed != NULL)
			D_FREE(opts->pci_allowed);
		return -DER_INVAL;
	}

	if ((strcmp(cfg.method, "bdev_nvme_attach_controller") != 0) || (cfg.params == NULL))
		goto out;

	key = spdk_json_object_first(cfg.params);

	while (key != NULL) {
		if (spdk_json_strequal(key, "traddr")) {
			traddr = spdk_json_strdup(json_value(key));

			D_INFO("Transport address found in JSON config: %s", traddr);

			if (vmd_enabled) {
				if (strncmp(traddr, "0", 1) != 0) {
					/*
					 * We can assume this is the transport id of the
					 * backing NVMe SSD behind the VMD. DPDK will
					 * not recognize this transport ID, instead need
					 * to pass VMD address as the whitelist param.
					 */
					rc = traddr_to_vmd(traddr, traddr);
					if (rc != 0) {
						D_ERROR("Invalid traddr: %s", traddr);
						rc = -DER_INVAL;
						free(traddr);
						goto err;
					}

					D_INFO("\tVMD backing address translated to: %s", traddr);
				}
			}

			rc = opts_add_pci_addr(opts, traddr);
			if (rc != 0) {
				D_ERROR("spdk env add pci: %d", rc);
				free(traddr);
				goto err;
			}

			free(traddr);
		}

		key = spdk_json_next(key);
	}
out:
	free(cfg.method);
	return rc;
err:
	free(cfg.method);
	if (opts->pci_allowed != NULL)
		D_FREE(opts->pci_allowed);
	return rc;
}

static int
add_bdevs_to_opts(struct spdk_json_val *bdev_ss, bool vmd_enabled, struct spdk_env_opts *opts)
{
	struct json_config_ctx	*ctx;
	int			 rc = 0;

	D_ASSERT(bdev_ss != NULL);
	D_ASSERT(opts != NULL);

	D_ALLOC(ctx, sizeof(*ctx));
	if (!ctx) {
		rc = -DER_NOMEM;
		D_ERROR("Failed to allocate context, "DF_RC"", DP_RC(rc));
		return rc;
	}

	/* Capture subsystem name and config array */
	if (spdk_json_decode_object(bdev_ss, subsystem_decoders, SPDK_COUNTOF(subsystem_decoders),
				    ctx)) {
		D_ERROR("Failed to parse subsystem configuration");
		rc = -DER_INVAL;
		goto out;
	}

	D_DEBUG(DB_MGMT, "subsystem '%.*s': found in JSON config", ctx->subsystem_name->len,
		(char *)ctx->subsystem_name->start);

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);

	while (ctx->config_it != NULL) {
		rc = load_bdev_subsystem_config(ctx, vmd_enabled, opts);
		if (rc != 0) {
			goto out;
		}

		/* Move on to next subsystem config*/
		ctx->config_it = spdk_json_next(ctx->config_it);
	}
out:
	free(ctx->json_data);
	free(ctx->values);
	D_FREE(ctx);
	return rc;
}

static int
check_vmd_status(struct spdk_json_val *vmd_ss, bool *vmd_enabled)
{
	struct json_config_ctx	*ctx;
	int			 rc = 0;

	D_ALLOC(ctx, sizeof(*ctx));
	if (!ctx) {
		rc = -DER_NOMEM;
		D_ERROR("Failed to allocate context, "DF_RC"", DP_RC(rc));
		return rc;
	}

	if (vmd_ss == NULL) {
		goto out;
	}
	D_ASSERT(vmd_enabled != NULL);

	/* Capture subsystem name and config array */
	if (spdk_json_decode_object(vmd_ss, subsystem_decoders, SPDK_COUNTOF(subsystem_decoders),
				    ctx)) {
		D_ERROR("Failed to parse subsystem configuration");
		rc = -DER_INVAL;
		goto out;
	}

	D_DEBUG(DB_MGMT, "subsystem '%.*s': found in JSON config", ctx->subsystem_name->len,
		(char *)ctx->subsystem_name->start);

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);

	while (ctx->config_it != NULL) {
		rc = load_vmd_subsystem_config(ctx, vmd_enabled);
		if (rc != 0) {
			goto out;
		}

		/* Move on to next subsystem config*/
		ctx->config_it = spdk_json_next(ctx->config_it);
	}
out:
	free(ctx->json_data);
	free(ctx->values);
	D_FREE(ctx);
	return rc;
}

int
bio_add_allowed_devices(const char *json_config_file, struct spdk_env_opts *opts)
{
	struct json_config_ctx	*ctx;
	struct spdk_json_val	*bdev_ss = NULL, *vmd_ss = NULL;
	bool			 vmd_enabled = false;
	int			 rc = 0;

	D_ASSERT(json_config_file != NULL);
	D_ASSERT(opts != NULL);

	D_ALLOC(ctx, sizeof(*ctx));
	if (!ctx) {
		rc = -DER_NOMEM;
		D_ERROR("Failed to allocate context, "DF_RC"", DP_RC(rc));
		return rc;
	}

	rc = read_config(json_config_file, ctx);
	if (rc) {
		D_ERROR("config read failed");
		goto out;
	}

	/* Capture subsystems array */
	rc = spdk_json_find_array(ctx->values, "subsystems", NULL, &ctx->subsystems);
	if (rc) {
		D_ERROR("No 'subsystems' key JSON configuration file.");
		rc = -DER_INVAL;
		goto out;
	}

	/* Get first subsystem */
	ctx->subsystems_it = spdk_json_array_first(ctx->subsystems);
	if (ctx->subsystems_it == NULL) {
		D_ERROR("Empty 'subsystems' section in JSON configuration file");
		rc = -DER_INVAL;
		goto out;
	}

	while (ctx->subsystems_it != NULL) {
		/* Capture subsystem name and config array */
		if (spdk_json_decode_object(ctx->subsystems_it, subsystem_decoders,
					    SPDK_COUNTOF(subsystem_decoders), ctx)) {
			D_ERROR("Failed to parse subsystem configuration");
			rc = -DER_INVAL;
			goto out;
		}

		if (spdk_json_strequal(ctx->subsystem_name, "bdev"))
			bdev_ss = ctx->subsystems_it;

		if (spdk_json_strequal(ctx->subsystem_name, "vmd"))
			vmd_ss = ctx->subsystems_it;

		/* Move on to next subsystem */
		ctx->subsystems_it = spdk_json_next(ctx->subsystems_it);
	};

	if (bdev_ss == NULL) {
		D_ERROR("JSON config missing bdev subsystem");
		rc = -DER_INVAL;
		goto out;
	}

	rc = check_vmd_status(vmd_ss, &vmd_enabled);
	if (rc) {
		goto out;
	}

	rc = add_bdevs_to_opts(bdev_ss, vmd_enabled, opts);
out:
	free(ctx->json_data);
	free(ctx->values);
	D_FREE(ctx);
	return rc;
}
