/**
 * (C) Copyright 2021-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/file.h>
#include <spdk/util.h>
#include <spdk/json.h>
#include <spdk/thread.h>
#include <spdk/nvme.h>
#include <spdk/nvmf_spec.h>
#include "bio_internal.h"

/* JSON tags should match encode/decode logic in src/control/server/storage/bdev/backend_json.go */

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

static struct spdk_json_object_decoder
daos_data_decoders[] = {
	{"config", offsetof(struct json_config_ctx, config), cap_array_or_null}
};

struct
config_entry {
	char			*method;
	struct spdk_json_val	*params;
};

static struct spdk_json_object_decoder
config_entry_decoders[] = {
	{"method", offsetof(struct config_entry, method), spdk_json_decode_string},
	{"params", offsetof(struct config_entry, params), cap_object, true}
};

struct busid_range_info {
	uint8_t	begin;
	uint8_t	end;
};

/* PCI address bus-ID range to be used to filter hotplug events */
struct busid_range_info hotplug_busid_range = {};

static struct spdk_json_object_decoder
busid_range_decoders[] = {
	{"begin", offsetof(struct busid_range_info, begin), spdk_json_decode_uint8},
	{"end", offsetof(struct busid_range_info, end), spdk_json_decode_uint8},
};

struct accel_props_info {
	char		*engine;
	uint16_t	 opt_mask;
};

/* Acceleration properties to specify engine to use and optional capabilities to enable */
struct accel_props_info accel_props = {};

static struct spdk_json_object_decoder
accel_props_decoders[] = {
	{"accel_engine", offsetof(struct accel_props_info, engine), spdk_json_decode_string},
	{"accel_opts", offsetof(struct accel_props_info, opt_mask), spdk_json_decode_uint16},
};

struct rpc_srv_info {
	bool	 enable;
	char	*sock_addr;
};

/* Settings to enable an SPDK JSON-RPC server to run in current process */
struct rpc_srv_info rpc_srv_settings = {};

static struct spdk_json_object_decoder
rpc_srv_decoders[] = {
	{"enable", offsetof(struct rpc_srv_info, enable), spdk_json_decode_bool},
	{"sock_addr", offsetof(struct rpc_srv_info, sock_addr), spdk_json_decode_string},
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
	char		*traddr_tmp = NULL, *vmd_addr = NULL;
	char		*ptr;
	const char	 ch = ':';
	char		 addr_split[3];
	int		 position;
	int		 iteration;
	int		 n, rc = 0;
	int		 vmd_addr_left_len;

	D_ALLOC(vmd_addr, SPDK_NVMF_TRADDR_MAX_LEN + 1);
	if (vmd_addr == NULL)
		return -DER_NOMEM;

	strncat(vmd_addr, "0000:", SPDK_NVMF_TRADDR_MAX_LEN);
	vmd_addr_left_len = SPDK_NVMF_TRADDR_MAX_LEN - strlen(vmd_addr);

	D_STRNDUP(traddr_tmp, src, SPDK_NVMF_TRADDR_MAX_LEN);
	if (traddr_tmp == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}

	/* Only the first chunk of data from the traddr is useful */
	ptr = strchr(traddr_tmp, ch);
	if (ptr == NULL) {
		D_ERROR("Transport id not valid\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	position = ptr - traddr_tmp;
	traddr_tmp[position] = '\0';

	ptr = traddr_tmp;
	iteration = 0;
	while (*ptr != '\0') {
		n = snprintf(addr_split, sizeof(addr_split), "%s", ptr);
		if (n < 0) {
			D_ERROR("snprintf failed\n");
			D_GOTO(out, rc = -DER_INVAL);
		}
		if (vmd_addr_left_len > strnlen(addr_split, sizeof(addr_split) - 1)) {
			strncat(vmd_addr, addr_split, vmd_addr_left_len);
			vmd_addr_left_len -= strnlen(addr_split, sizeof(addr_split) - 1);
		} else {
			D_GOTO(out, rc = -DER_INVAL);
		}

		if (iteration != 0) {
			if (vmd_addr_left_len > 2) {
				strncat(vmd_addr, ".", vmd_addr_left_len);
				vmd_addr_left_len -= 1;
			} else {
				D_GOTO(out, rc = -DER_INVAL);
			}
			ptr = ptr + 3;
			/** Hack alert!  Reuse existing buffer to ensure new
			 *  string is null terminated.
			 */
			addr_split[0] = ptr[0];
			addr_split[1] = '\0';
			strncat(vmd_addr, addr_split, vmd_addr_left_len);
			vmd_addr_left_len -= 1;
			break;
		}
		if (vmd_addr_left_len > 1) {
			strncat(vmd_addr, ":", vmd_addr_left_len);
			vmd_addr_left_len -= 1;
		} else {
			D_GOTO(out, rc = -DER_INVAL);
		}
		ptr = ptr + 2;
		iteration++;
	}
	n = snprintf(dst, SPDK_NVMF_TRADDR_MAX_LEN, "%s", vmd_addr);
	if (n < 0 || n > SPDK_NVMF_TRADDR_MAX_LEN) {
		D_ERROR("snprintf failed\n");
		rc = -DER_INVAL;
	}

out:
	D_FREE(traddr_tmp);
	D_FREE(vmd_addr);
	return rc;
}

static int
opts_add_pci_addr(struct spdk_env_opts *opts, char *traddr)
{
	struct spdk_pci_addr	**list = &opts->pci_allowed;
	struct spdk_pci_addr	 *tmp1 = *list;
	struct spdk_pci_addr     *tmp2;
	size_t			  count = opts->num_pci_addr;
	int			  rc;

	rc = is_addr_in_allowlist(traddr, *list, count);
	if (rc < 0)
		return rc;
	if (rc == 1)
		return 0;

	D_REALLOC_ARRAY(tmp2, tmp1, count, count + 1);
	if (tmp2 == NULL)
		return -DER_NOMEM;

	*list = tmp2;
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

	if (file == NULL)
		/* errno is set by fopen */
		return NULL;

	data = spdk_posix_file_load(file, size);
	fclose(file);
	return data;
}

static int
read_config(const char *config_file, struct json_config_ctx *ctx)
{
	struct spdk_json_val	*values = NULL;
	void			*json = NULL;
	void			*end;
	ssize_t			 values_cnt;
	ssize_t			 rc;
	size_t			 json_size;

	json = read_file(config_file, &json_size);
	if (!json) {
		D_ERROR("Read config file %s failed: '%s'\n",
			config_file, strerror(errno));
		return -DER_INVAL;
	}

	rc = spdk_json_parse(json, json_size, NULL, 0, &end,
			     SPDK_JSON_PARSE_FLAG_ALLOW_COMMENTS);
	if (rc < 0) {
		D_ERROR("Parsing config failed: %s\n", strerror(-rc));
		D_GOTO(free_json, rc = -DER_INVAL);
	}

	values_cnt = rc;
	D_ALLOC_ARRAY(values, values_cnt);
	if (values == NULL)
		D_GOTO(free_json, rc = -DER_NOMEM);

	rc = spdk_json_parse(json, json_size, values, values_cnt, &end,
			     SPDK_JSON_PARSE_FLAG_ALLOW_COMMENTS);
	if (rc != values_cnt) {
		D_ERROR("Parsing config failed, want %zd values got %zd\n", values_cnt, rc);
		D_GOTO(free_values, rc = -DER_INVAL);
	}

	ctx->json_data = json;
	ctx->json_data_size = json_size;

	ctx->values = values;
	ctx->values_cnt = values_cnt;

	return 0;

free_values:
	D_FREE(values);
free_json:
	D_FREE(json);

	return rc;
}

static void
free_json_config_ctx(struct json_config_ctx *ctx)
{
	D_FREE(ctx->values);
	D_FREE(ctx->json_data);
	D_FREE(ctx);
}

static int
load_vmd_subsystem_config(struct json_config_ctx *ctx, bool *vmd_enabled)
{
	struct config_entry	 cfg = {};
	int			 rc;

	D_ASSERT(ctx->config_it != NULL);
	D_ASSERT(vmd_enabled != NULL);

	rc = spdk_json_decode_object(ctx->config_it, config_entry_decoders,
				     SPDK_COUNTOF(config_entry_decoders), &cfg);
	if (rc < 0) {
		D_ERROR("Failed to decode config entry: %s\n", strerror(-rc));
		return -DER_INVAL;
	}

	if (strcmp(cfg.method, NVME_CONF_ENABLE_VMD) == 0)
		*vmd_enabled = true;

	D_FREE(cfg.method);
	return 0;
}

static int
add_traddrs_from_bdev_subsys(struct json_config_ctx *ctx, bool vmd_enabled,
			     struct spdk_env_opts *opts)
{
	struct config_entry	 cfg = {};
	struct spdk_json_val	*key, *value;
	char			*traddr;
	int			 rc = 0;

	D_ASSERT(ctx->config_it != NULL);

	rc = spdk_json_decode_object(ctx->config_it, config_entry_decoders,
				     SPDK_COUNTOF(config_entry_decoders), &cfg);
	if (rc < 0) {
		D_ERROR("Failed to decode config entry: %s\n", strerror(-rc));
		return -DER_INVAL;
	}

	if (strcmp(cfg.method, NVME_CONF_ATTACH_CONTROLLER) != 0) {
		D_DEBUG(DB_MGMT, "skip config entry %s\n", cfg.method);
		goto free_method;
	}

	if (cfg.params == NULL) {
		D_ERROR("bad config entry %s with nil params\n", cfg.method);
		D_GOTO(free_method, rc = -DER_INVAL);
	}

	D_ALLOC(traddr, SPDK_NVMF_TRADDR_MAX_LEN + 1);
	if (traddr == NULL)
		D_GOTO(free_method, rc = -DER_NOMEM);

	key = spdk_json_object_first(cfg.params);

	while (key != NULL) {
		if (spdk_json_strequal(key, "traddr")) {
			value = json_value(key);
			if (!value || value->len > SPDK_NVMF_TRADDR_MAX_LEN) {
				D_ERROR("Invalid json value\n");
				D_GOTO(free_traddr, rc = -DER_INVAL);
			}
			memcpy(traddr, value->start, value->len);
			traddr[value->len] = '\0';
			D_DEBUG(DB_MGMT, "Adding transport address '%s' to SPDK allowed list",
				traddr);

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
						D_ERROR("Invalid traddr %s (rc: %d)\n", traddr, rc);
						goto free_traddr;
					}

					D_DEBUG(DB_MGMT, "\t- VMD backing address reverted to "
						"'%s'\n", traddr);
				}
			}

			rc = opts_add_pci_addr(opts, traddr);
			if (rc != 0) {
				D_ERROR("spdk env add pci: %d\n", rc);
				goto free_traddr;
			}
		}
		key = spdk_json_next(key);
	}

free_traddr:
	D_FREE(traddr);
free_method:
	D_FREE(cfg.method);

	if (rc > 0)
		rc = 0;
	return rc;
}

#define BDEV_NAME_MAX_LEN	256

static int
check_name_from_bdev_subsys(struct json_config_ctx *ctx)
{
	struct config_entry	 cfg = {};
	struct spdk_json_val	*key, *value;
	char			*name;
	int			 rc = 0;
	int			 roles = 0;

	D_ASSERT(ctx->config_it != NULL);

	rc = spdk_json_decode_object(ctx->config_it, config_entry_decoders,
				     SPDK_COUNTOF(config_entry_decoders), &cfg);
	if (rc < 0) {
		D_ERROR("Failed to decode config entry: %s\n", strerror(-rc));
		return -DER_INVAL;
	}

	if (strcmp(cfg.method, NVME_CONF_ATTACH_CONTROLLER) != 0 &&
	    strcmp(cfg.method, NVME_CONF_AIO_CREATE) != 0) {
		D_DEBUG(DB_MGMT, "skip config entry %s\n", cfg.method);
		goto free_method;
	}

	if (cfg.params == NULL) {
		D_ERROR("bad config entry %s with nil params\n", cfg.method);
		D_GOTO(free_method, rc = -DER_INVAL);
	}

	D_ALLOC(name, BDEV_NAME_MAX_LEN + 1);
	if (name == NULL)
		D_GOTO(free_method, rc = -DER_NOMEM);

	key = spdk_json_object_first(cfg.params);
	while (key != NULL) {
		value = json_value(key);
		if (spdk_json_strequal(key, "name")) {
			value = json_value(key);
			if (!value || value->len > BDEV_NAME_MAX_LEN) {
				D_ERROR("Invalid json value\n");
				D_GOTO(free_name, rc = -DER_INVAL);
			}
			memcpy(name, value->start, value->len);
			name[value->len] = '\0';

			D_DEBUG(DB_MGMT, "check bdev name: %s\n", name);
			rc = bdev_name2roles(name);
			if (rc < 0) {
				D_ERROR("bdev_name contains invalid roles: %s\n", name);
				D_GOTO(free_name, rc);
			}
			roles |= rc;
		}
		key = spdk_json_next(key);
	}

free_name:
	D_FREE(name);
free_method:
	D_FREE(cfg.method);
	return rc < 0 ?  rc : roles;
}

static int
decode_subsystem_configs(struct spdk_json_val *json_val, struct json_config_ctx *ctx)
{
	int	rc;

	D_ASSERT(json_val != NULL);
	D_ASSERT(ctx != NULL);

	/* Capture subsystem name and config array */
	rc = spdk_json_decode_object(json_val, subsystem_decoders, SPDK_COUNTOF(subsystem_decoders),
				     ctx);
	if (rc < 0) {
		D_ERROR("Failed to parse vmd subsystem: %s\n", strerror(-rc));
		return -DER_INVAL;
	}

	D_DEBUG(DB_MGMT, "subsystem '%.*s': found\n", ctx->subsystem_name->len,
		(char *)ctx->subsystem_name->start);

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);

	return 0;
}

static int
add_bdevs_to_opts(struct json_config_ctx *ctx, struct spdk_json_val *bdev_ss, bool vmd_enabled,
		  struct spdk_env_opts *opts)
{
	int	rc;

	D_ASSERT(opts != NULL);

	rc = decode_subsystem_configs(bdev_ss, ctx);
	if (rc != 0)
		return rc;

	while (ctx->config_it != NULL) {
		rc = add_traddrs_from_bdev_subsys(ctx, vmd_enabled, opts);
		if (rc != 0)
			return rc;
		/* Move on to next subsystem config*/
		ctx->config_it = spdk_json_next(ctx->config_it);
	}

	return rc;
}

static int
check_md_on_ssd_status(struct json_config_ctx *ctx, struct spdk_json_val *bdev_ss)
{
	int	rc;
	int	roles = 0;

	rc = decode_subsystem_configs(bdev_ss, ctx);
	if (rc != 0)
		return rc;

	while (ctx->config_it != NULL) {
		rc = check_name_from_bdev_subsys(ctx);
		if (rc < 0)
			return rc;
		roles |= rc;
		/* Move on to next subsystem config*/
		ctx->config_it = spdk_json_next(ctx->config_it);
	}

	return roles;
}

static int
check_vmd_status(struct json_config_ctx *ctx, struct spdk_json_val *vmd_ss, bool *vmd_enabled)
{
	int	rc;

	if (vmd_ss == NULL)
		return 0;

	D_ASSERT(vmd_enabled != NULL);

	rc = decode_subsystem_configs(vmd_ss, ctx);
	if (rc != 0)
		return rc;

	while (ctx->config_it != NULL) {
		rc = load_vmd_subsystem_config(ctx, vmd_enabled);
		if (rc != 0)
			return rc;
		/* Move on to next subsystem config*/
		ctx->config_it = spdk_json_next(ctx->config_it);
	}

	return rc;
}

/**
 * Set allowed bdev PCI addresses in provided SPDK environment options based on attach bdev RPCs
 * in the JSON config file.
 *
 * \param[IN]	nvme_conf	JSON config file path
 * \param[OUT]	opts		SPDK environment options
 * \param[OUT]	roles		global nvme bdev roles
 *
 * \returns	 Zero on success, negative on failure (DER)
 */
int
bio_add_allowed_alloc(const char *nvme_conf, struct spdk_env_opts *opts, int *roles)
{
	struct json_config_ctx	*ctx;
	struct spdk_json_val	*bdev_ss = NULL;
	struct spdk_json_val	*vmd_ss = NULL;
	bool			 vmd_enabled = false;
	int			 rc = 0;

	D_ASSERT(nvme_conf != NULL);
	D_ASSERT(opts != NULL);

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	rc = read_config(nvme_conf, ctx);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Capture subsystems array */
	rc = spdk_json_find_array(ctx->values, "subsystems", NULL, &ctx->subsystems);
	if (rc < 0) {
		D_ERROR("Failed to find subsystems key: %s\n", strerror(-rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* Get first subsystem */
	ctx->subsystems_it = spdk_json_array_first(ctx->subsystems);
	if (ctx->subsystems_it == NULL) {
		D_ERROR("Empty subsystems section\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	while (ctx->subsystems_it != NULL) {
		/* Capture subsystem name and config array */
		rc = spdk_json_decode_object(ctx->subsystems_it, subsystem_decoders,
					     SPDK_COUNTOF(subsystem_decoders), ctx);
		if (rc < 0) {
			D_ERROR("Failed to parse subsystem configuration: %s\n", strerror(-rc));
			D_GOTO(out, rc = -DER_INVAL);
		}

		if (spdk_json_strequal(ctx->subsystem_name, "bdev"))
			bdev_ss = ctx->subsystems_it;

		if (spdk_json_strequal(ctx->subsystem_name, BIO_DEV_TYPE_VMD))
			vmd_ss = ctx->subsystems_it;

		/* Move on to next subsystem */
		ctx->subsystems_it = spdk_json_next(ctx->subsystems_it);
	};

	if (bdev_ss == NULL) {
		D_ERROR("Config is missing bdev subsystem\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = check_vmd_status(ctx, vmd_ss, &vmd_enabled);
	if (rc < 0)
		goto out;

	rc = check_md_on_ssd_status(ctx, bdev_ss);
	if (rc < 0)
		goto out;
	*roles = rc;

	rc = add_bdevs_to_opts(ctx, bdev_ss, vmd_enabled, opts);
out:
	free_json_config_ctx(ctx);
	return rc;
}

static int
decode_daos_data(const char *nvme_conf, const char *method_name, struct config_entry *cfg)
{
	struct json_config_ctx	*ctx;
	struct spdk_json_val	*daos_data;
	int			 rc = 0;

	D_ASSERT(nvme_conf != NULL);
	D_ASSERT(cfg != NULL);

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	rc = read_config(nvme_conf, ctx);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Capture daos object */
	rc = spdk_json_find(ctx->values, "daos_data", NULL, &daos_data,
			    SPDK_JSON_VAL_OBJECT_BEGIN);
	if (rc < 0) {
		D_ERROR("Failed to find 'daos_data' key: %s\n", strerror(-rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* Capture config array in ctx */
	rc = spdk_json_decode_object(daos_data, daos_data_decoders,
				     SPDK_COUNTOF(daos_data_decoders), ctx);
	if (rc < 0) {
		D_ERROR("Failed to parse 'daos_data' entry: %s\n", strerror(-rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);
	if (ctx->config_it == NULL) {
		D_DEBUG(DB_MGMT, "Empty 'daos_data' section\n");
		D_GOTO(out, rc = 1); /* non-fatal */
	}

	while (ctx->config_it != NULL) {
		rc = spdk_json_decode_object(ctx->config_it, config_entry_decoders,
					     SPDK_COUNTOF(config_entry_decoders), cfg);
		if (rc < 0) {
			D_ERROR("Failed to decode 'config' entry: %s\n", strerror(-rc));
			D_GOTO(out, rc = -DER_INVAL);
		}

		if (strcmp(cfg->method, method_name) == 0)
			break;

		/* Move on to next subsystem config */
		ctx->config_it = spdk_json_next(ctx->config_it);
	}

	if (ctx->config_it == NULL) {
		D_DEBUG(DB_MGMT, "No '%s' entry\n", method_name);
		rc = 1; /* non-fatal */
	}
out:
	free_json_config_ctx(ctx);
	return rc;
}

static int
get_hotplug_busid_range(const char *nvme_conf)
{
	struct config_entry	 cfg = {};
	int			 rc;

	rc = decode_daos_data(nvme_conf, NVME_CONF_SET_HOTPLUG_RANGE, &cfg);
	if (rc != 0)
		goto out;

	rc = spdk_json_decode_object(cfg.params, busid_range_decoders,
				     SPDK_COUNTOF(busid_range_decoders),
				     &hotplug_busid_range);
	if (rc < 0) {
		D_ERROR("Failed to decode '%s' entry: %s)\n", NVME_CONF_SET_HOTPLUG_RANGE,
			strerror(-rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_DEBUG(DB_MGMT, "'%s' read from config: %X-%X\n", NVME_CONF_SET_HOTPLUG_RANGE,
		hotplug_busid_range.begin, hotplug_busid_range.end);
out:
	if (cfg.method != NULL)
		D_FREE(cfg.method);
	if (rc > 0)
		rc = 0;
	return 0;
}

static bool
hotplug_filter_fn(const struct spdk_pci_addr *addr)
{
	if (hotplug_busid_range.end == 0 || hotplug_busid_range.begin > hotplug_busid_range.end) {
		D_DEBUG(DB_MGMT, "hotplug filter accept event on bus-id %X, invalid range\n",
			addr->bus);
		return true; /* allow if no or invalid range specified */
	}

	if (addr->bus >= hotplug_busid_range.begin && addr->bus <= hotplug_busid_range.end) {
		D_DEBUG(DB_MGMT, "hotplug filter accept event on bus-id %X\n", addr->bus);
		return true;
	}

	D_DEBUG(DB_MGMT, "hotplug filter refuse event on bus-id %X\n", addr->bus);
	return false;
}

/**
 * Set hotplug bus-ID ranges in SPDK filter based on values read from JSON config file.
 *
 * \param[IN]	nvme_conf	JSON config file path
 *
 * \returns	 Zero on success, negative on failure (DER)
 */
int
bio_set_hotplug_filter(const char *nvme_conf)
{
	int	rc;

	rc = get_hotplug_busid_range(nvme_conf);
	if (rc != 0)
		return rc;

	spdk_nvme_pcie_set_hotplug_filter(hotplug_filter_fn);

	return rc;
}

/**
 * Read optional acceleration properties from JSON config file.
 *
 * \param[IN]	nvme_conf	JSON config file path
 *
 * \returns	 Zero on success, negative on failure (DER)
 */
int
bio_read_accel_props(const char *nvme_conf)
{
	struct config_entry	 cfg = {};
	int			 rc;

	rc = decode_daos_data(nvme_conf, NVME_CONF_SET_ACCEL_PROPS, &cfg);
	if (rc != 0)
		goto out;

	rc = spdk_json_decode_object(cfg.params, accel_props_decoders,
				     SPDK_COUNTOF(accel_props_decoders),
				     &accel_props);
	if (rc < 0) {
		D_ERROR("Failed to decode '%s' entry (%s)\n", NVME_CONF_SET_ACCEL_PROPS,
			strerror(-rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_DEBUG(DB_MGMT, "'%s' read from config, setting: %s, capabilities: move=%s,crc=%s\n",
	       NVME_CONF_SET_ACCEL_PROPS, accel_props.engine,
	       CHK_FLAG(accel_props.opt_mask, NVME_ACCEL_FLAG_MOVE) ? "true" : "false",
	       CHK_FLAG(accel_props.opt_mask, NVME_ACCEL_FLAG_CRC) ? "true" : "false");

	/* TODO: do something useful with acceleration engine properties */
out:
	if (cfg.method != NULL)
		D_FREE(cfg.method);
	if (rc > 0)
		rc = 0;
	return rc;
}

/**
 * Set output parameters based on JSON config settings for option SPDK JSON-RPC server.
 *
 * \param[IN]	nvme_conf	JSON config file path
 * \param[OUT]	enable		Flag to enable the RPC server
 * \param[OUT]	sock_addr	Path in which to create socket file
 *
 * \returns	 Zero on success, negative on failure (DER)
 */
int
bio_read_rpc_srv_settings(const char *nvme_conf, bool *enable, const char **sock_addr)
{
	struct config_entry	 cfg = {};
	int			 rc;

	rc = decode_daos_data(nvme_conf, NVME_CONF_SET_SPDK_RPC_SERVER, &cfg);
	if (rc != 0)
		goto out;

	rc = spdk_json_decode_object(cfg.params, rpc_srv_decoders,
				     SPDK_COUNTOF(rpc_srv_decoders),
				     &rpc_srv_settings);
	if (rc < 0) {
		D_ERROR("Failed to decode '%s' entry: %s)\n", NVME_CONF_SET_SPDK_RPC_SERVER,
			strerror(-rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	*enable = rpc_srv_settings.enable;
	*sock_addr = rpc_srv_settings.sock_addr;

	D_DEBUG(DB_MGMT, "'%s' read from config: enabled=%d, addr %s\n",
		NVME_CONF_SET_SPDK_RPC_SERVER, *enable, (char *)*sock_addr);
out:
	if (cfg.method != NULL)
		D_FREE(cfg.method);
	if (rc > 0)
		rc = 0;
	return rc;
}
