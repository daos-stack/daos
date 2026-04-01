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

#define JSON_TRUE            ((json_bool)1)

static int
is_vmd_enabled(struct ddb_ctx *ctx, struct json_object *vmd_subsystem, bool *enabled)
{
	struct json_object *config;
	struct json_object *name;
	int                 methods_num;
	int                 rc;

	rc = json_object_object_get_ex(vmd_subsystem, KEY_SUBSYSTEM_CONFIG, &config);
	if (rc != JSON_TRUE) {
		ddb_errorf(ctx, "VMD subsystem does not have a '%s' key.\n", KEY_SUBSYSTEM_CONFIG);
		return -DER_PROTO;
	}

	methods_num = json_object_array_length(config);
	for (int i = 0; i < methods_num; i++) {
		struct json_object *method = json_object_array_get_idx(config, i);
		D_ASSERT(method != NULL);

		rc = json_object_object_get_ex(method, KEY_METHOD_NAME, &name);
		if (rc != JSON_TRUE) {
			ddb_errorf(ctx, "Config[%d] does not have a '%s' key.\n", i,
				   KEY_METHOD_NAME);
			return -DER_PROTO;
		}

		if (strncmp(json_object_get_string(name), METHOD_ENABLE_VMD,
			    sizeof(METHOD_ENABLE_VMD)) == 0) {
			*enabled = true;
			return DER_SUCCESS;
		}
	}

	*enabled = false;

	return DER_SUCCESS;
}

static int
get_vmd_subsystem(struct ddb_ctx *ctx, struct json_object *subsystems,
		  struct json_object **vmd_subsystem)
{
	int subsystems_num = json_object_array_length(subsystems);

	for (int i = 0; i < subsystems_num; i++) {
		struct json_object *subsystem = json_object_array_get_idx(subsystems, i);
		struct json_object *name;
		int                 rc;

		D_ASSERT(subsystem != NULL);

		rc = json_object_object_get_ex(subsystem, KEY_SUBSYSTEM_NAME, &name);
		if (rc != JSON_TRUE) {
			ddb_errorf(ctx, "Subsystem[%d] does not have a '%s' key.\n", i,
				   KEY_SUBSYSTEM_NAME);
			return -DER_PROTO;
		}

		if (strncmp(json_object_get_string(name), NAME_VMD, sizeof(NAME_VMD)) == 0) {
			*vmd_subsystem = subsystem;
			return DER_SUCCESS;
		}
	}

	*vmd_subsystem = NULL;

	return DER_SUCCESS;
}

static int
vmd_subsystem_required(struct ddb_ctx *ctx, const char *db_path, bool *is_required)
{
	struct json_object *root;
	struct json_object *subsystems;
	struct json_object *vmd_subsystem;
	char               *nvme_conf;
	int                 rc;

	D_ASPRINTF(nvme_conf, "%s/%s", db_path, VOS_NVME_CONF);
	if (nvme_conf == NULL) {
		return -DER_NOMEM;
	}

	if (access(nvme_conf, F_OK) != 0) {
		*is_required = false;
		D_FREE(nvme_conf);
		return DER_SUCCESS;
	}

	root = json_object_from_file(nvme_conf);
	if (root == NULL) {
		ddb_errorf(ctx, "Cannot open %s file: %s\n", nvme_conf, json_util_get_last_err());
		D_FREE(nvme_conf);
		return -DER_PROTO;
	}

	rc = json_object_object_get_ex(root, KEY_SUBSYSTEMS, &subsystems);
	if (rc != JSON_TRUE) {
		ddb_errorf(ctx, "File %s does not have '%s' key\n", nvme_conf, KEY_SUBSYSTEMS);
		rc = -DER_PROTO;
	} else {
		rc = get_vmd_subsystem(ctx, subsystems, &vmd_subsystem);
		if (rc == DER_SUCCESS) {
			if (vmd_subsystem != NULL) {
				rc = is_vmd_enabled(ctx, vmd_subsystem, is_required);
			} else {
				*is_required = false;
			}
		}
	}

	json_object_put(root);

	D_FREE(nvme_conf);

	return rc;
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
	int         rc;

	if (vmd_used_once) {
		ddb_error(ctx, SPDK_INIT_AFTER_VMD_USED_MSG);
		return false;
	}

	rc = vmd_subsystem_required(ctx, db_path, &vmd_required);
	if (rc != DER_SUCCESS) {
		/** assume the most restrictive scenario to prevent DDB from crashing */
		vmd_required = true;
	}

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
