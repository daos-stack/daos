/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "drpc_handler.h"
#include <daos/drpc_modules.h>

static drpc_handler_t *registry_table;

/*
 * API functions
 */
int
drpc_hdlr_init(void)
{
	D_ALLOC_ARRAY(registry_table, NUM_DRPC_MODULES);
	if (registry_table == NULL) {
		D_ERROR("Failed to allocate handler registry table\n");
		return -DER_NOMEM;
	}

	return DER_SUCCESS;
}

int
drpc_hdlr_fini(void)
{
	D_FREE(registry_table);

	return DER_SUCCESS;
}

static bool
module_id_is_valid(int module_id)
{
	return (module_id >= 0 && module_id < NUM_DRPC_MODULES);
}

int
drpc_hdlr_register(int module_id, drpc_handler_t handler)
{
	if (registry_table == NULL) {
		D_ERROR("Table not initialized\n");
		return -DER_UNINIT;
	}

	if (!module_id_is_valid(module_id)) {
		D_ERROR("Module ID %d out of range\n", module_id);
		return -DER_INVAL;
	}

	if (handler == NULL) {
		D_ERROR("Tried to register a null handler\n");
		return -DER_INVAL;
	}

	if (registry_table[module_id] != NULL) {
		D_ERROR("Tried to register module ID %d more than once\n",
				module_id);
		return -DER_EXIST;
	}

	registry_table[module_id] = handler;

	return DER_SUCCESS;
}


int
drpc_hdlr_register_all(struct dss_drpc_handler *handlers)
{
	int			rc = DER_SUCCESS;
	struct dss_drpc_handler	*current;

	if (registry_table == NULL) {
		D_ERROR("Table not initialized\n");
		return -DER_UNINIT;
	}

	if (handlers == NULL) {
		/* Nothing to do */
		return DER_SUCCESS;
	}

	/* register as many as we can */
	current = handlers;
	while (current->handler != NULL) {
		int handler_rc;

		handler_rc = drpc_hdlr_register(current->module_id,
				current->handler);
		if (handler_rc != DER_SUCCESS) {
			rc = handler_rc;
		}

		current++;
	}

	return rc;
}

drpc_handler_t
drpc_hdlr_get_handler(int module_id)
{
	drpc_handler_t handler;

	if (registry_table == NULL) {
		D_ERROR("Table not initialized\n");
		return NULL;
	}

	if (!module_id_is_valid(module_id)) {
		D_ERROR("Module ID %d out of range\n", module_id);
		return NULL;
	}

	handler = registry_table[module_id];
	if (handler == NULL) {
		D_ERROR("Handler for module %d not found\n", module_id);
	}

	return handler;
}

int
drpc_hdlr_unregister(int module_id)
{
	if (registry_table == NULL) {
		D_ERROR("Table not initialized\n");
		return -DER_UNINIT;
	}

	if (!module_id_is_valid(module_id)) {
		D_ERROR("Module ID %d out of range\n", module_id);
		return -DER_INVAL;
	}

	registry_table[module_id] = NULL;

	return DER_SUCCESS;
}

int
drpc_hdlr_unregister_all(struct dss_drpc_handler *handlers)
{
	struct dss_drpc_handler *current;

	if (registry_table == NULL) {
		D_ERROR("Table not initialized\n");
		return -DER_UNINIT;
	}

	if (handlers == NULL) {
		/* Nothing to do */
		return DER_SUCCESS;
	}

	current = handlers;
	while (current->handler != NULL) {
		drpc_hdlr_unregister(current->module_id);

		current++;
	}

	return DER_SUCCESS;
}

/*
 * Top-level handler for incoming dRPC messages. Looks up the appropriate
 * registered dRPC handler and runs it on the message.
 */
void
drpc_hdlr_process_msg(Drpc__Call *request, Drpc__Response *resp)
{
	drpc_handler_t handler;

	D_ASSERT(request != NULL);
	D_ASSERT(resp != NULL);

	handler = drpc_hdlr_get_handler(request->module);
	if (handler == NULL) {
		D_ERROR("Message for unregistered dRPC module: %d\n",
				request->module);
		resp->status = DRPC__STATUS__UNKNOWN_MODULE;
		return;
	}

	handler(request, resp);
}
