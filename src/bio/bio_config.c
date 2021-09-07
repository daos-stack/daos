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

static struct spdk_json_object_decoder jsonrpc_cmd_decoders[] = {
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
		D_ERROR("invalid transport address %s\n", pci_addr);
		return -DER_INVAL;
	}

	for (i = 0; i < num_allowlist_devices; i++) {
		if (spdk_pci_addr_compare(&tmp, &allowlist[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

static int
opts_add_pci_addr(struct spdk_env_opts *opts, char *traddr)
{
	struct spdk_pci_addr	**list = &opts->pci_allowed;
	struct spdk_pci_addr     *tmp = *list;
	size_t			  count = opts->num_pci_addr;
	int			  rc;

	rc = is_addr_in_allowlist(traddr, *list, count);
	if (rc < 0)
		return rc;
	if (rc == 1)
		return 0;

	tmp = realloc(tmp, sizeof(*tmp) * (count + 1));
	if (tmp == NULL) {
		D_ERROR("realloc error\n");
		return -DER_NOMEM;
	}

	*list = tmp;
	if (spdk_pci_addr_parse(*list + count, traddr) < 0) {
		D_ERROR("Invalid address %s\n", traddr);
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
		D_ERROR("Read JSON configuration file %s failed, rc: %d\n",
			config_file, errno);
		return -DER_INVAL;
	}

	rc = spdk_json_parse(json, json_size, NULL, 0, &end,
			     SPDK_JSON_PARSE_FLAG_ALLOW_COMMENTS);
	if (rc < 0) {
		D_ERROR("Parsing JSON configuration failed (%zd)\n", rc);
		rc = -DER_INVAL;
		goto err;
	}

	values_cnt = rc;
	values = calloc(values_cnt, sizeof(struct spdk_json_val));
	if (values == NULL) {
		D_ERROR("Out of memory\n");
		rc = -DER_NOMEM;
		goto err;
	}

	rc = spdk_json_parse(json, json_size, values, values_cnt, &end,
			     SPDK_JSON_PARSE_FLAG_ALLOW_COMMENTS);
	if (rc != values_cnt) {
		D_ERROR("Parsing JSON configuration failed (%zd)\n", rc);
		rc = -DER_INVAL;
		goto err;
	}

	ctx->json_data = json;
	ctx->json_data_size = json_size;

	ctx->values = values;
	ctx->values_cnt = values_cnt;

	return 0;
err:
	D_FREE(json);
	D_FREE(values);
	return rc;
}

static int
load_subsystem_config(struct json_config_ctx *ctx, struct spdk_env_opts *opts)
{
	struct config_entry	 cfg = {};
	struct spdk_json_val	*key;
	char			*traddr;
	int			 rc = 0;

	D_ASSERT(ctx->config_it != NULL);

	if (spdk_json_decode_object(ctx->config_it, jsonrpc_cmd_decoders,
				    SPDK_COUNTOF(jsonrpc_cmd_decoders), &cfg)) {
		D_ERROR("Failed to decode config entry\n");
		return -DER_INVAL;
	}

	if ((strcmp(cfg.method, "bdev_nvme_attach_controller") != 0) || (cfg.params == NULL))
		goto out;

	key = spdk_json_object_first(cfg.params);
	while (key != NULL) {
		if (spdk_json_strequal(key, "traddr")) {
			traddr = spdk_json_strdup(json_value(key));
			D_INFO("Transport address found in JSON config: %s\n", traddr);

			rc = opts_add_pci_addr(opts, traddr);
			if (rc != 0) {
				D_ERROR("spdk env add pci: %d\n", rc);
				goto out;
			}
		}

		key = spdk_json_next(key);
	}
out:
	D_FREE(cfg.method);
	return rc;
}

int
bio_add_allowed_devices(const char *json_config_file, struct spdk_env_opts *opts)
{
	struct json_config_ctx	*ctx = calloc(1, sizeof(*ctx));
	int			 rc = 0;

	if (!ctx) {
		rc = -DER_NOMEM;
		D_ERROR("Failed to allocate context, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ASSERT(json_config_file);
	rc = read_config(json_config_file, ctx);
	if (rc) {
		D_ERROR("config read failed");
		return rc;
	}

	/* Capture subsystems array */
	rc = spdk_json_find_array(ctx->values, "subsystems", NULL, &ctx->subsystems);
	if (rc) {
		D_ERROR("No 'subsystems' key JSON configuration file.\n");
		rc = -DER_INVAL;
		goto out;
	}

	/* Get first subsystem */
	ctx->subsystems_it = spdk_json_array_first(ctx->subsystems);
	if (ctx->subsystems_it == NULL) {
		D_ERROR("'subsystems' configuration is empty\n");
		rc = -DER_INVAL;
		goto out;
	}

	while (ctx->subsystems_it != NULL) {
		/* Capture subsystem name and config array */
		if (spdk_json_decode_object(ctx->subsystems_it, subsystem_decoders,
					    SPDK_COUNTOF(subsystem_decoders), ctx)) {
			D_ERROR("Failed to parse subsystem configuration\n");
			rc = -DER_INVAL;
			goto out;
		}

		if (spdk_json_strequal(ctx->subsystem_name, "bdev"))
			break;

		/* Move on to next subsystem */
		ctx->subsystems_it = spdk_json_next(ctx->subsystems_it);
	};

	if (!spdk_json_strequal(ctx->subsystem_name, "bdev")) {
		D_WARN("JSON config missing bdev subsystem\n");
		goto out;
	}

	D_DEBUG(DB_MGMT, "subsystem '%.*s': found in JSON config\n", ctx->subsystem_name->len,
		(char *)ctx->subsystem_name->start);

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);

	while (ctx->config_it != NULL) {
		rc = load_subsystem_config(ctx, opts);
		if (rc != 0) {
			goto out;
		}

		/* Move on to next subsystem config*/
		ctx->config_it = spdk_json_next(ctx->config_it);
	}
out:
	D_FREE(ctx->json_data);
	D_FREE(ctx->values);
	D_FREE(ctx);
	return rc;
}
