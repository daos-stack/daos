/*
 * Copyright (c) 2016 UChicago Argonne, LLC
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * @file
 *
 * SWIM:
 * Scalable Weakly-consistent Infection-style Process Group Membership Protocol.
 */
#ifndef __SWIM_H__
#define __SWIM_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWIM_ID_INVALID ((uint64_t)-1)

typedef uint64_t swim_id_t;

enum swim_member_status {
	SWIM_MEMBER_ALIVE = 0,
	SWIM_MEMBER_SUSPECT,
	SWIM_MEMBER_DEAD,
	SWIM_MEMBER_INACTIVE
};

/** This chars should represent values of enum swim_member_status to print */
#define SWIM_STATUS_CHARS "ASDI"

/** SWIM state associated with each group member */
struct swim_member_state {
	uint64_t                sms_incarnation; /**< incarnation number */
	enum swim_member_status sms_status;      /**< status of member */
	uint32_t                sms_delay;       /**< SWIM message transfer
						      network duration */
};

struct swim_member_update {
	uint64_t                 smu_id;
	struct swim_member_state smu_state;
};

/** opaque SWIM context type */
struct swim_context;

/** @defgroup SWIM SWIM API */

/** @addtogroup SWIM
 * @{
 */
/** SWIM callbacks for integrating with an overlying group management layer */
struct swim_ops {
	/**
	 * Send a SWIM request to other group member.
	 *
	 * @param[in]  ctx	SWIM context pointer from swim_init()
	 * @param[in]  id	IDs of selected target for message
	 * @param[in]  to	IDs of target to message send to
	 * @param[in]  upds	SWIM updates to other group member
	 * @param[in]  nupds	the count of SWIM updates
	 * @returns		0 on success, negative error ID otherwise
	 */
	int (*send_request)(struct swim_context *ctx, swim_id_t id, swim_id_t to,
			    struct swim_member_update *upds, size_t nupds);

	/**
	 * Send a SWIM reply to other group member.
	 *
	 * @param[in]  ctx	SWIM context pointer from swim_init()
	 * @param[in]  from	IDs of target from IREQ received
	 * @param[in]  to	IDs of suspected target to message send to
	 * @param[in]  rc	Error code to reply
	 * @param[in]  args	Additional arguments
	 * @returns		0 on success, negative error ID otherwise
	 */
	int (*send_reply)(struct swim_context *ctx, swim_id_t from, swim_id_t to, int rc,
			  void *args);

	/**
	 * Retrieve a (non-dead) random group member from the group
	 * management layer to send a direct ping request to.
	 *
	 * NOTE: to ensure time-bounded detection of faulty members,
	 * round-robin selection of members is required.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @returns           ID of selected direct ping target,
	 *                    SWIM_ID_INVALID if no available target
	 */
	swim_id_t (*get_dping_target)(struct swim_context *ctx);

	/**
	 * Retrieve a set of (non-dead) random group members from the group
	 * management layer to send indirect ping requests to.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @returns           ID of selected indirect ping target,
	 *                    SWIM_ID_INVALID if no available target
	 */
	swim_id_t (*get_iping_target)(struct swim_context *ctx);

	/**
	 * Get the SWIM protocol state corresponding to a given member ID.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @param[in]  id     member ID to query
	 * @param[out] state  pointer to given member's SWIM state
	 * @returns           0 on success, negative error ID otherwise
	 */
	int (*get_member_state)(struct swim_context *ctx, swim_id_t id,
				struct swim_member_state *state);

	/**
	 * Set the SWIM protocol state corresponding to a given member ID.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @param[in]  id     member ID to set
	 * @param[in]  state  pointer to given member's SWIM state
	 * @returns           0 on success, negative error ID otherwise
	 */
	int (*set_member_state)(struct swim_context *ctx, swim_id_t id,
				struct swim_member_state *state);

	/**
	 * Set new incarnation number of a given member.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @param[in]  id     member ID
	 * @param[in]  state  pointer to given member's SWIM state
	 */
	void (*new_incarnation)(struct swim_context *ctx, swim_id_t id,
				struct swim_member_state *state);
};

/**
 * Initialize the SWIM protocol.
 *
 * @param[in]  self_id   Self member ID
 * @param[in]  swim_ops  SWIM callbacks to group management layer
 * @param[in]  data      Private data which associated with group members
 * @returns              SWIM context pointer on success, NULL otherwise
 */
struct swim_context *
swim_init(swim_id_t self_id, struct swim_ops *swim_ops, void *data);

/**
 * Finalize the SWIM protocol.
 *
 * @param[in]  ctx  SWIM context pointer from swim_init()
 */
void
swim_fini(struct swim_context *ctx);

/**
 * Get private data which associated with group members.
 * Originally it's passed to swim_init().
 *
 * @param[in]  ctx  SWIM context pointer from swim_init()
 * @returns         Private data which associated with group members,
 *                  NULL if not set
 */
void *
swim_data(struct swim_context *ctx);

/**
 * Get self member ID.
 *
 * @param[in]  ctx  SWIM context pointer from swim_init()
 * @returns         self ID,
 *                  SWIM_ID_INVALID if not set
 */
swim_id_t
swim_self_get(struct swim_context *ctx);

/**
 * Set self member ID.
 *
 * @param[in]  ctx     SWIM context pointer from swim_init()
 * @param[in]  self_id Self member ID
 */
void
swim_self_set(struct swim_context *ctx, swim_id_t self_id);

/**
 * Parse a SWIM message from other group member.
 *
 * @param[in]  ctx	SWIM context pointer from swim_init()
 * @param[in]  from	IDs of selected target for message
 * @param[in]  upds	SWIM updates from other group member
 * @param[in]  nupds	the count of SWIM updates
 * @returns		0 on success, negative error ID otherwise
 */
int
swim_updates_parse(struct swim_context *ctx, swim_id_t from, struct swim_member_update *upds,
		   size_t nupds);

/**
 * Prepare a SWIM message for other group member.
 *
 * @param[in]  ctx	SWIM context pointer from swim_init()
 * @param[in]  id	IDs of selected target for message
 * @param[in]  to	IDs of message to send to
 * @param[out] pupds	Pointer to SWIM updates from other group member
 * @param[out] pnupds	Pointer to the count of SWIM updates
 * @returns		0 on success, negative error ID otherwise
 */
int
swim_updates_prepare(struct swim_context *ctx, swim_id_t id, swim_id_t to,
		     struct swim_member_update **pupds, size_t *pnupds);

/**
 * Send a SWIM message for other group member.
 *
 * @param[in]  ctx	SWIM context pointer from swim_init()
 * @param[in]  id	IDs of selected target for message
 * @param[in]  to	IDs of message to send to
 * @returns		0 on success, negative error ID otherwise
 */
int
swim_updates_send(struct swim_context *ctx, swim_id_t id, swim_id_t to);

/**
 * Store information about ipinged member for subsequent reply or timeout
 *
 * @param[in]  ctx	SWIM context pointer from swim_init()
 * @param[in]  from_id	IDs of member from this iping request
 * @param[in]  to_id	IDs of message to send to
 * @param[in]  args	the data passed to reply or timeout handler
 * @returns		0 on success, negative error ID otherwise
 */
int
swim_ipings_suspend(struct swim_context *ctx, swim_id_t from_id, swim_id_t to_id, void *args);

/**
 * Reply to stored member about status of iping
 *
 * @param[in]  ctx	SWIM context pointer from swim_init()
 * @param[in]  from_id	IDs of member from this iping request
 * @param[in]  ret_rc	result of iping
 * @returns		0 on success, negative error ID otherwise
 */
int
swim_ipings_reply(struct swim_context *ctx, swim_id_t from_id, int ret_rc);

/**
 * Progress the state machine of SWIM protocol.
 *
 * @param[in]  ctx     SWIM context pointer from swim_init()
 * @param[in]  timeout The amount of time in milliseconds available for
 *                     processing. If timeout <= 0 then returns immediately or
 *                     after the state change (progress minimal required
 *                     changes).
 * @returns            0 on success, negative error ID otherwise
 */
int
swim_progress(struct swim_context *ctx, int64_t timeout);

/**
 * Update the state machine of SWIM protocol with unexpected network glitch.
 *
 * @param[in]  ctx   SWIM context pointer from swim_init()
 * @param[in]  id    The SWIM member to whom shift timeouts
 * @param[in]  delay The amount of time in milliseconds by which
 *                   ALL timeouts will be shifted.
 * @returns          0 on success, negative error ID otherwise
 */
int
swim_net_glitch_update(struct swim_context *ctx, swim_id_t id, uint64_t delay);

/**
 * Notify SWIM about new remote member.
 *
 * @param[in]  ctx	SWIM context pointer from swim_init()
 * @param[in]  id	IDs of new member in inactive state
 * @returns		0 on success, negative error ID otherwise
 */
int
swim_member_new_remote(struct swim_context *ctx, swim_id_t id);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __SWIM_H__ */
