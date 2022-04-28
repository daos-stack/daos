/**
 * (C) Copyright 2021-2022 Intel Corporation.
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

/* JSON tags should match encodecoding logic in src/control/server/storage/bdev/backend_json.go */

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
		rc = -DER_NOMEM;
		goto out;
	}

	/* Only the first chunk of data from the traddr is useful */
	ptr = strchr(traddr_tmp, ch);
	if (ptr == NULL) {
		D_ERROR("Transport id not valid\n");
		rc = -DER_INVAL;
		goto out;
	}
	position = ptr - traddr_tmp;
	traddr_tmp[position] = '\0';

	ptr = traddr_tmp;
	iteration = 0;
	while (*ptr != '\0') {
		n = snprintf(addr_split, sizeof(addr_split), "%s", ptr);
		if (n < 0) {
			D_ERROR("snprintf failed\n");
			rc = -DER_INVAL;
			goto out;
		}
		if (vmd_addr_left_len > strnlen(addr_split, sizeof(addr_split) - 1)) {
			strncat(vmd_addr, addr_split, vmd_addr_left_len);
			vmd_addr_left_len -= strnlen(addr_split, sizeof(addr_split) - 1);
		} else {
			rc = -DER_INVAL;
			goto out;
		}

		if (iteration != 0) {
			if (vmd_addr_left_len > 2) {
				strncat(vmd_addr, ".", vmd_addr_left_len);
				vmd_addr_left_len -= 1;
			} else {
				rc = -DER_INVAL;
				goto out;
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
			rc = -DER_INVAL;
			goto out;
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
		D_ERROR("Parsing config failed (%zd)\n", rc);
		rc = -DER_INVAL;
		goto err;
	}

	values_cnt = rc;
	D_ALLOC_ARRAY(values, values_cnt);
	if (values == NULL) {
		rc = -DER_NOMEM;
		goto err;
	}

	rc = spdk_json_parse(json, json_size, values, values_cnt, &end,
			     SPDK_JSON_PARSE_FLAG_ALLOW_COMMENTS);
	if (rc != values_cnt) {
		D_ERROR("Parsing config failed (%zd)\n", rc);
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
	int			 rc = 0;

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

	free(cfg.method);
	return 0;
}

static int
load_bdev_subsystem_config(struct json_config_ctx *ctx, bool vmd_enabled,
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

	D_ALLOC(traddr, SPDK_NVMF_TRADDR_MAX_LEN + 1);
	if (traddr == NULL)
		return -DER_NOMEM;

	if ((strcmp(cfg.method, NVME_CONF_ATTACH_CONTROLLER) != 0) || (cfg.params == NULL))
		goto out;

	key = spdk_json_object_first(cfg.params);

	while (key != NULL) {
		if (spdk_json_strequal(key, "traddr")) {

			value = json_value(key);
			if (!value || value->len > SPDK_NVMF_TRADDR_MAX_LEN) {
				D_ERROR("Invalid json value\n");
				rc = -DER_INVAL;
				goto out;
			}
			memcpy(traddr, value->start, value->len);
			traddr[value->len] = '\0';
			D_INFO("Adding transport address '%s' to SPDK allowed list", traddr);

			if (vmd_enabled) {
				if (strncmp(traddr, "0", 1) != 0) {
					/*
					 * We can assume this is the transport id of the
					 * backing NVMe SSD behind the VMD. DPDK will
					 * not recognize this transport ID, instead need
					 * to pass VMD address as the whitelist param.
					 */
					rc = traddr_to_vmd(traddr, traddr);
					if (rc < 0) {
						D_ERROR("Invalid traddr: %s\n", traddr);
						rc = -DER_INVAL;
						goto out;
					}

					D_INFO("\t- VMD backing address reverted to '%s'\n",
						traddr);
				}
			}

			rc = opts_add_pci_addr(opts, traddr);
			if (rc < 0) {
				D_ERROR("spdk env add pci: %d\n", rc);
				goto out;
			}

		}

		key = spdk_json_next(key);
	}
out:
	D_FREE(traddr);
	free(cfg.method);
	return rc;
}

static int
add_bdevs_to_opts(struct json_config_ctx *ctx, struct spdk_json_val *bdev_ss, bool vmd_enabled,
		  struct spdk_env_opts *opts)
{
	int	rc = 0;

	D_ASSERT(bdev_ss != NULL);
	D_ASSERT(opts != NULL);

	/* Capture subsystem name and config array */
	rc = spdk_json_decode_object(bdev_ss, subsystem_decoders, SPDK_COUNTOF(subsystem_decoders),
				     ctx);
	if (rc < 0) {
		D_ERROR("Failed to parse bdev subsystem: %s\n", strerror(-rc));
		rc = -DER_INVAL;
		goto out;
	}

	D_DEBUG(DB_MGMT, "subsystem '%.*s': found\n", ctx->subsystem_name->len,
		(char *)ctx->subsystem_name->start);

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);

	while (ctx->config_it != NULL) {
		rc = load_bdev_subsystem_config(ctx, vmd_enabled, opts);
		if (rc < 0)
			goto out;

		/* Move on to next subsystem config*/
		ctx->config_it = spdk_json_next(ctx->config_it);
	}
out:
	return rc;
}

static int
check_vmd_status(struct json_config_ctx *ctx, struct spdk_json_val *vmd_ss, bool *vmd_enabled)
{
	int	rc = 0;

	if (vmd_ss == NULL)
		goto out;

	D_ASSERT(vmd_enabled != NULL);

	/* Capture subsystem name and config array */
	rc = spdk_json_decode_object(vmd_ss, subsystem_decoders, SPDK_COUNTOF(subsystem_decoders),
				     ctx);
	if (rc < 0) {
		D_ERROR("Failed to parse vmd subsystem: %s\n", strerror(-rc));
		rc = -DER_INVAL;
		goto out;
	}

	D_DEBUG(DB_MGMT, "subsystem '%.*s': found\n", ctx->subsystem_name->len,
		(char *)ctx->subsystem_name->start);

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);

	while (ctx->config_it != NULL) {
		rc = load_vmd_subsystem_config(ctx, vmd_enabled);
		if (rc < 0)
			goto out;

		/* Move on to next subsystem config*/
		ctx->config_it = spdk_json_next(ctx->config_it);
	}
out:
	return rc;
}

int
bio_add_allowed_alloc(const char *nvme_conf, struct spdk_env_opts *opts)
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
	if (rc < 0)
		goto out;

	/* Capture subsystems array */
	rc = spdk_json_find_array(ctx->values, "subsystems", NULL, &ctx->subsystems);
	if (rc < 0) {
		D_ERROR("Failed to find subsystems key: %s\n", strerror(-rc));
		rc = -DER_INVAL;
		goto out;
	}

	/* Get first subsystem */
	ctx->subsystems_it = spdk_json_array_first(ctx->subsystems);
	if (ctx->subsystems_it == NULL) {
		D_ERROR("Empty subsystems section\n");
		rc = -DER_INVAL;
		goto out;
	}

	while (ctx->subsystems_it != NULL) {
		/* Capture subsystem name and config array */
		rc = spdk_json_decode_object(ctx->subsystems_it, subsystem_decoders,
					     SPDK_COUNTOF(subsystem_decoders), ctx);
		if (rc < 0) {
			D_ERROR("Failed to parse subsystem configuration: %s\n", strerror(-rc));
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
		D_ERROR("Config is missing bdev subsystem\n");
		rc = -DER_INVAL;
		goto out;
	}

	rc = check_vmd_status(ctx, vmd_ss, &vmd_enabled);
	if (rc < 0)
		goto out;

	rc = add_bdevs_to_opts(ctx, bdev_ss, vmd_enabled, opts);
out:
	free(ctx->json_data);
	free(ctx->values);
	D_FREE(ctx);
	return rc;
}

static int
get_hotplug_busid_range(const char *nvme_conf)
{
	struct json_config_ctx	*ctx;
	struct spdk_json_val	*daos_data;
	struct config_entry	 cfg = {};
	int			 rc = 0;

	D_ASSERT(nvme_conf != NULL);

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	rc = read_config(nvme_conf, ctx);
	if (rc < 0) {
		D_ERROR("No config file\n");
		goto out;
	}

	/* Capture daos object */
	rc = spdk_json_find(ctx->values, "daos_data", NULL, &daos_data,
			    SPDK_JSON_VAL_OBJECT_BEGIN);
	if (rc < 0) {
		D_ERROR("Failed to find 'daos_data' key: %s\n", strerror(-rc));
		rc = -DER_INVAL;
		goto out;
	}

	/* Capture config array in ctx */
	rc = spdk_json_decode_object(daos_data, daos_data_decoders,
				     SPDK_COUNTOF(daos_data_decoders), ctx);
	if (rc < 0) {
		D_ERROR("Failed to parse 'daos_data' entry: %s\n", strerror(-rc));
		rc = -DER_INVAL;
		goto out;
	}

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);
	if (ctx->config_it == NULL) {
		D_DEBUG(DB_MGMT, "Empty 'daos_data' section\n");
		goto out; /* non-fatal */
	}

	while (ctx->config_it != NULL) {
		rc = spdk_json_decode_object(ctx->config_it, config_entry_decoders,
					    SPDK_COUNTOF(config_entry_decoders), &cfg);
		if (rc < 0) {
			D_ERROR("Failed to decode 'config' entry: %s\n", strerror(-rc));
			rc = -DER_INVAL;
			goto out;
		}

		if (strcmp(cfg.method, NVME_CONF_SET_HOTPLUG_RANGE) == 0)
			break;

		/* Move on to next subsystem config */
		ctx->config_it = spdk_json_next(ctx->config_it);
	}

	if (ctx->config_it == NULL) {
		D_DEBUG(DB_MGMT, "No '%s' entry\n", NVME_CONF_SET_HOTPLUG_RANGE);
		goto out; /* non-fatal */
	}

	rc = spdk_json_decode_object(cfg.params, busid_range_decoders,
				     SPDK_COUNTOF(busid_range_decoders),
				     &hotplug_busid_range);
	if (rc < 0) {
		D_ERROR("Failed to decode '%s' entry (rc: %d)\n", NVME_CONF_SET_HOTPLUG_RANGE, rc);
		rc = -DER_INVAL;
		goto out;
	}

	D_INFO("'%s' read from config: %X-%X\n", NVME_CONF_SET_HOTPLUG_RANGE,
		hotplug_busid_range.begin, hotplug_busid_range.end);
out:
	free(ctx->json_data);
	free(ctx->values);
	D_FREE(ctx);
	return rc;
}

static bool
hotplug_filter_fn(const struct spdk_pci_addr *addr)
{
	if (hotplug_busid_range.end == 0 || hotplug_busid_range.begin > hotplug_busid_range.end) {
		D_INFO("hotplug filter accept event on bus-id %X, invalid range\n", addr->bus);
		return true; /* allow if no or invalid range specified */
	}

	if (addr->bus >= hotplug_busid_range.begin && addr->bus <= hotplug_busid_range.end) {
		D_INFO("hotplug filter accept event on bus-id %X\n", addr->bus);
		return true;
	}

	D_INFO("hotplug filter refuse event on bus-id %X\n", addr->bus);
	return false;
}

int
bio_set_hotplug_filter(const char *nvme_conf) {
	int	rc = 0;

	rc = get_hotplug_busid_range(nvme_conf);
	if (rc < 0) {
		return rc;
	}

	spdk_nvme_pcie_set_hotplug_filter(hotplug_filter_fn);

	return rc;
}

static int
get_accel_props(const char *nvme_conf)
{
	struct json_config_ctx	*ctx;
	struct spdk_json_val	*daos_data;
	struct config_entry	 cfg = {};
	int			 rc = 0;

	D_ASSERT(nvme_conf != NULL);

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	rc = read_config(nvme_conf, ctx);
	if (rc < 0) {
		D_ERROR("No config file\n");
		goto out;
	}

	/* Capture daos object */
	rc = spdk_json_find(ctx->values, "daos_data", NULL, &daos_data,
			    SPDK_JSON_VAL_OBJECT_BEGIN);
	if (rc < 0) {
		D_ERROR("Failed to find 'daos_data' key: %s\n", strerror(-rc));
		rc = -DER_INVAL;
		goto out;
	}

	/* Capture config array in ctx */
	rc = spdk_json_decode_object(daos_data, daos_data_decoders,
				     SPDK_COUNTOF(daos_data_decoders), ctx);
	if (rc < 0) {
		D_ERROR("Failed to parse 'daos_data' entry: %s\n", strerror(-rc));
		rc = -DER_INVAL;
		goto out;
	}

	/* Get 'config' array first configuration entry */
	ctx->config_it = spdk_json_array_first(ctx->config);
	if (ctx->config_it == NULL) {
		D_DEBUG(DB_MGMT, "Empty 'daos_data' section\n");
		goto out; /* non-fatal */
	}

	while (ctx->config_it != NULL) {
		rc = spdk_json_decode_object(ctx->config_it, config_entry_decoders,
					     SPDK_COUNTOF(config_entry_decoders), &cfg);
		if (rc < 0) {
			D_ERROR("Failed to decode 'config' entry: %s\n", strerror(-rc));
			rc = -DER_INVAL;
			goto out;
		}

		if (strcmp(cfg.method, NVME_CONF_SET_ACCEL_PROPS) == 0)
			break;

		/* Move on to next subsystem config */
		ctx->config_it = spdk_json_next(ctx->config_it);
	}

	if (ctx->config_it == NULL) {
		D_DEBUG(DB_MGMT, "No '%s' entry\n", NVME_CONF_SET_ACCEL_PROPS);
		goto out; /* non-fatal */
	}

	rc = spdk_json_decode_object(cfg.params, accel_props_decoders,
				     SPDK_COUNTOF(accel_props_decoders),
				     &accel_props);
	if (rc < 0) {
		D_ERROR("Failed to decode '%s' entry (rc: %d)\n", NVME_CONF_SET_ACCEL_PROPS, rc);
		rc = -DER_INVAL;
		goto out;
	}

	D_INFO("'%s' read from config, setting: %s, capabilities: move=%s,crc=%s\n",
		NVME_CONF_SET_ACCEL_PROPS, accel_props.engine,
		CHK_FLAG(accel_props.opt_mask, NVME_ACCEL_FLAG_MOVE) ? "true" : "false",
		CHK_FLAG(accel_props.opt_mask, NVME_ACCEL_FLAG_CRC) ? "true" : "false");
out:
	free(ctx->json_data);
	free(ctx->values);
	D_FREE(ctx);
	return rc;
}

int
bio_read_accel_props(const char *nvme_conf) {
	/* TODO: do something useful with acceleration engine properties */
	return get_accel_props(nvme_conf);
}

