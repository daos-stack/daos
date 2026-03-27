/**
 * Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(ddb)

#include <stdbool.h>
#include <string.h>

#include <json-c/json.h>
#include <daos/debug.h>
#include <gurt/debug.h>

#include "ddb_common.h"

/**
 * SPDK, in all known applications—including daos_engine—is initialized only once during the
 * lifetime of a process. DDB uses SPDK differently, allowing the user to initialize and
 * re‑initialize SPDK multiple times within the same process. However, SPDK does not fully support
 * re‑initialization. At the moment, this issue appears only when the SPDK configuration uses the
 * VMD subsystem. When a VMD‑enabled SPDK configuration is in use, two conditions must be respected:
 * - The VMD‑enabled configuration must be used during the first SPDK initialization. The VMD
 * subsystem and DPDK will not be initialized on subsequent SPDK re‑initializations.
 * - After a VMD‑enabled configuration has been used, the user cannot re‑initialize SPDK—whether
 * with VMD enabled or disabled. The internal state of the VMD subsystem and DPDK becomes unsafe to
 * use, even if the next SPDK configuration does not include VMD.
 *
 * This compilation unit enforces these rules to prevent the user from triggering the unsupported
 * sequence and prevent DDB from crashing.
 *
 * You may remove this code once the underlying issue is properly resolved.
 */

#define VOS_NVME_CONF        "daos_nvme.conf"

#define KEY_SUBSYSTEMS       "subsystems"
#define KEY_SUBSYSTEM_NAME   "subsystem"
#define KEY_SUBSYSTEM_CONFIG "config"
#define KEY_METHOD_NAME      "method"
#define NAME_VMD             "vmd"
#define METHOD_ENABLE_VMD    "enable_vmd"

static bool
is_vmd_enabled(struct json_object *vmd_subsystem)
{
	struct json_object *config;
	struct json_object *method;
	struct json_object *name;
	int                 methods_num;
	int                 rc;

	rc = json_object_object_get_ex(vmd_subsystem, KEY_SUBSYSTEM_CONFIG, &config);
	D_ASSERTF(rc == 1, "VMD subsystem does not have a '%s' key.\n", KEY_SUBSYSTEM_CONFIG);

	methods_num = json_object_array_length(config);
	for (int i = 0; i < methods_num; i++) {
		method = json_object_array_get_idx(config, i);
		D_ASSERT(method != NULL);

		rc = json_object_object_get_ex(method, KEY_METHOD_NAME, &name);
		D_ASSERTF(rc == 1, "Config[%d] does not have a '%s' key.\n", i, KEY_METHOD_NAME);

		if (strncmp(json_object_get_string(name), METHOD_ENABLE_VMD,
			    sizeof(METHOD_ENABLE_VMD)) == 0) {
			return true;
		}
	}

	return false;
}

static struct json_object *
get_vmd_subsystem(struct json_object *subsystems)
{
	struct json_object *subsystem;
	struct json_object *name;
	int                 subsystems_num = json_object_array_length(subsystems);
	int                 rc;

	for (int i = 0; i < subsystems_num; i++) {
		subsystem = json_object_array_get_idx(subsystems, i);
		D_ASSERT(subsystem != NULL);

		rc = json_object_object_get_ex(subsystem, KEY_SUBSYSTEM_NAME, &name);
		D_ASSERTF(rc == 1, "Subsystem[%d] does not have a '%s' key.\n", i,
			  KEY_SUBSYSTEM_NAME);

		if (strncmp(json_object_get_string(name), NAME_VMD, sizeof(NAME_VMD)) == 0) {
			return subsystem;
		}
	}

	return NULL;
}

static bool
vmd_subsystem_required(const char *db_path)
{
	bool                is_required = false;
	struct json_object *root;
	struct json_object *subsystems;
	struct json_object *vmd_subsystem;
	char               *nvme_conf;
	int                 rc;

	D_ASPRINTF(nvme_conf, "%s/%s", db_path, VOS_NVME_CONF);
	D_ASSERT(nvme_conf != NULL);

	root = json_object_from_file(nvme_conf);
	D_ASSERTF(root != NULL, "%s\n", json_util_get_last_err());

	rc = json_object_object_get_ex(root, KEY_SUBSYSTEMS, &subsystems);
	D_ASSERTF(rc == 1, "File %s does not have '%s' key\n", nvme_conf, KEY_SUBSYSTEMS);

	vmd_subsystem = get_vmd_subsystem(subsystems);
	if (vmd_subsystem != NULL) {
		is_required = is_vmd_enabled(vmd_subsystem);
	}

	json_object_put(root);

	return is_required;
}

#define SPDK_INIT_AFTER_VMD_USED_MSG                                                               \
	"SPDK cannot be re‑initialized after the VMD subsystem has been initialized. Please "    \
	"restart the DDB process and try again.\n"
#define VMD_INIT_AFTER_SPDK_USED_MSG                                                                \
	"The VMD subsystem cannot be correctly initialized during an SPDK re‑initialization "     \
	"sequence. Ensure that the VMD‑backed pool is the first pool opened for the lifetime of " \
	"the DDB process. Please restart the DDB process and try again.\n"

bool
vmd_wa_can_proceed(struct ddb_ctx *ctx, const char *db_path)
{
	static bool spdk_used_once = false;
	static bool vmd_used_once  = false;
	bool        vmd_required;

	if (vmd_used_once) {
		ddb_error(ctx, SPDK_INIT_AFTER_VMD_USED_MSG);
		return false;
	}

	vmd_required = vmd_subsystem_required(db_path);

	if (vmd_required) {
		if (spdk_used_once) {
			ddb_error(ctx, VMD_INIT_AFTER_SPDK_USED_MSG);
			return false;
		}
		vmd_used_once = true;
	}

	spdk_used_once = true;

	return true;
}
